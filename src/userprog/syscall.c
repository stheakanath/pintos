#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);

/*Possible system calls*/
static void halt (void);
static void exit (int);
static pid_t exec (const char *);
static int wait (pid_t);
static bool create (const char *, unsigned);
static bool remove (const char *);
static int open (const char *);
static int filesize (int);
static int read (int, void *, unsigned);
static int write (int, const void *, unsigned);
static void seek (int, unsigned);
static unsigned tell (int);
static void close (int);
static mapid_t mmap (int, void *);
static void munmap (mapid_t);

static bool is_valid_vaddr(const void *);
static uint32_t *esp;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&fileLock);
  list_init(&open_file_list);
}

static void
syscall_handler (struct intr_frame *f) 
{
  esp = f->esp;

  if (!is_valid_pointer (esp) || !is_valid_pointer (esp + 1) ||
                 !is_valid_pointer (esp + 2) || !is_valid_pointer (esp + 3))
  {
    exit (-1);
  }
  else
  {
    int syscall_number = *esp;
    switch (syscall_number)
      {
      case SYS_HALT:
        halt ();
        break;
      case SYS_EXIT:
        exit (*(esp + 1));
        break;
      case SYS_EXEC:
        f->eax = exec ((char *) *(esp + 1));
        break;
      case SYS_WAIT:
        f->eax = wait (*(esp + 1));
        break;
      case SYS_CREATE:
        f->eax = create ((char *) *(esp + 1), *(esp + 2));
        break;
      case SYS_REMOVE:
        f->eax = remove ((char *) *(esp + 1));
        break;
      case SYS_OPEN:
        f->eax = open ((char *) *(esp + 1));
        break;
      case SYS_FILESIZE:
        f->eax = filesize (*(esp + 1));
        break;
      case SYS_READ:
        f->eax = read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;
      case SYS_WRITE:
        f->eax = write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;
      case SYS_SEEK:
        seek (*(esp + 1), *(esp + 2));
        break;
      case SYS_TELL:
        f->eax = tell (*(esp + 1));
        break;
      case SYS_CLOSE:
        close (*(esp + 1));
        break;
      case SYS_MMAP:
        f->eax = mmap (*(esp + 1), (void *) *(esp + 2));
        break;
      case SYS_MUNMAP:
        munmap (*(esp + 1));
        break;
      default:
        break;
      }
  }
}

static void 
halt (void) {
  shutdown_power_off();
}

static void 
exit (int status) 
{
  struct child *child;
  struct thread *cur;
  struct thread *parent;
  cur = thread_current();
  printf("%s: exit(%d)\n", cur->name, status);
  parent = get_thread(cur->pid);
  if (parent != NULL)
  {
    struct list_elem *e;
    e = list_tail(&parent->children);
    while ((e = list_prev(e)) != list_head(&parent->children))
    {
      child = list_entry(e, struct child, elem_child);
      if (child->cid == cur->tid)
      {
        lock_acquire (&parent->child_lock);
        child->exit_call = true;
        child->exit_status = status;
        lock_release (&parent->child_lock);
      }
    }
  }
  thread_exit();
}

static pid_t exec (const char *cmd_line)
{    
  tid_t tid;
  struct thread *cur;
  if (!is_valid_pointer(cmd_line))
  {

    exit(-1);
  }
  cur = thread_current();
  cur->child_load_success = 0;
  tid = process_execute(cmd_line);
  lock_acquire(&cur->child_lock);
  while (cur->child_load_success == 0)
    cond_wait(&cur->child_cond, &cur->child_lock);
  if (cur->child_load_success == -1)
    tid = -1;
  lock_release(&cur->child_lock);
  return tid;
}

static int wait (pid_t pid)
{
  return process_wait(pid);
}

static bool create (const char *file, unsigned initial_size)
{
  bool status;

  if (!is_valid_pointer(file))
    exit(-1);
  lock_acquire(&fileLock);
  status = filesys_create(file, initial_size);
  lock_release(&fileLock);
  return status;
}

static bool remove (const char *file)
{
  bool status;

  if (!is_valid_pointer(file))
    exit(-1);
  lock_acquire(&fileLock);
  status = filesys_remove(file);
  lock_release(&fileLock);
  return status;
}

static int open (const char *file)
{

  struct file *f;
  struct file_descriptor *fd;
  int status = -1;

  if (!is_valid_pointer(file))
    exit(-1);
  lock_acquire(&fileLock);
  f = filesys_open(file);
  if (f != NULL)
  {
    fd = calloc (1, sizeof *fd);
    fd-> fd_num = fd_allocation();
    fd-> file_owner = thread_current()->tid;
    fd-> file = f;
    list_push_back (&open_file_list, &fd->file_elem);
    status = fd->fd_num;
  } 
  lock_release(&fileLock); 
  return status;
}


static int filesize (int fd)
{  
  struct file_descriptor *struct_fd;
  int status = -1;

  lock_acquire (&fileLock); 
  struct_fd = get_current_file(fd);
  if (struct_fd != NULL)
    status = file_length (struct_fd->file);
  lock_release (&fileLock);

  return status;
}

static int read (int fd, void *buffer, unsigned size)
{
  struct file_descriptor *struct_fd;
  int status = 0; 
  struct thread *t = thread_current();
  
  unsigned size_of_buffer = size;
  void * buffer_ptr = buffer;

  while(buffer_ptr != NULL)
  {
    if(!is_valid_vaddr(buffer_ptr))
      exit(-1);
    if(pagedir_get_page(t->pagedir, buffer_ptr)==NULL)
    {
      struct supple_page_table_entry *entry;
      entry = find_supple_entry(&t->spt, pg_round_down(buffer_ptr));
      if(entry != NULL && !entry->is_loaded)
        load_data(entry);
      else if (entry == NULL && buffer_ptr >= (esp-32))
        increase_stack (buffer_ptr);
      else 
        exit(-1);
   
    }
   
    if(size_of_buffer ==0)
    {
      buffer_ptr = NULL;
    }
    else if (PGSIZE <= size_of_buffer)
    {
      size_of_buffer -= PGSIZE;
      buffer_ptr += PGSIZE;
    }
    else
    {
      size_of_buffer =0;
      buffer_ptr = buffer + size -1;
    }
  }
  lock_acquire(&fileLock);
  if (fd == STDOUT_FILENO)
  {
      status =  -1;
  }
  else if (fd == STDIN_FILENO)
  {
      uint8_t c;
      unsigned counter = size;
      uint8_t *buf = buffer;
      while (counter > 1 && (c = input_getc()) != 0)
        {
          *buf = c;
          buffer++;
          counter--; 
        }
      *buf = 0;
      status = size - counter;
  }
  else
  {
      struct_fd = get_current_file(fd);
      if (struct_fd != NULL)
        status = file_read(struct_fd->file, buffer, size);
  }

  lock_release (&fileLock);
  return status;
}


static int write (int fd, const void *buffer, unsigned size)
{
  struct file_descriptor *struct_fd;  
  int status = 0;
  unsigned size_of_buffer = size;
  void * buffer_ptr = buffer;
  while (buffer_ptr != NULL)
  {
    if (!is_valid_pointer(buffer_ptr))
      exit(-1);
    if (size_of_buffer >PGSIZE)
    {
      size_of_buffer -= PGSIZE;
      buffer_ptr += PGSIZE;
    }
    else if (size_of_buffer == 0)
    {
      buffer_ptr = NULL;
    }
    else 
    {
      size_of_buffer = 0;
      buffer_ptr = buffer + size - 1;
    }
 
  }
  lock_acquire (&fileLock); 

  if (fd == STDIN_FILENO)
  {
      status = -1;
  }

  else if (fd == STDOUT_FILENO)
  {
      putbuf (buffer, size);
      status = size;
  } 
  else
  {
    struct_fd = get_current_file (fd);
    if (struct_fd != NULL)
      status = file_write (struct_fd->file, buffer, size);
  }
  lock_release (&fileLock);
  return status;
}

static void seek (int fd, unsigned position)
{
  struct file_descriptor *struct_fd;
  
  lock_acquire (&fileLock); 
  struct_fd = get_current_file (fd);
  if (struct_fd != NULL)
    file_seek (struct_fd->file, position);
  lock_release (&fileLock);
}

static unsigned tell (int fd)
{
  struct file_descriptor *struct_fd;
  int status = 0;
  lock_acquire (&fileLock); 
  struct_fd = get_current_file (fd);
  if (struct_fd != NULL)
    status = file_tell (struct_fd->file);
  lock_release (&fileLock);
  return status;
}

static void close (int fd)
{
  struct file_descriptor *struct_fd;

  lock_acquire (&fileLock); 

  struct_fd = get_current_file (fd);
  if (struct_fd != NULL && struct_fd->file_owner == thread_current ()->tid)
    close_current_file (fd);

  lock_release (&fileLock);
}

bool is_valid_pointer(const void *pointer)
{
  struct thread *current;
  current = thread_current();
  if (pointer != NULL && is_user_vaddr (pointer) &&
      pagedir_get_page (current->pagedir, pointer) != NULL)
    return true;
  else
    return false;
}
static bool is_valid_vaddr(const void * pointer)
{
  return pointer !=NULL && is_user_vaddr(pointer);

}

int fd_allocation()
{
  static int fd_current = 1;
  return ++fd_current;
}

struct file_descriptor *get_current_file (int fd)
{
  struct list_elem *e;
  struct file_descriptor *fd_struct; 
  e = list_tail (&open_file_list);
  while ((e = list_prev (e)) != list_head (&open_file_list)) 
  {
    fd_struct = list_entry (e, struct file_descriptor, file_elem);
    if (fd_struct->fd_num == fd)
	return fd_struct;
  }
  return NULL;
}

void close_current_file (int fd)
{
  struct list_elem *e;
  struct list_elem *prev;
  struct file_descriptor *fd_struct; 
  e = list_end (&open_file_list);
  while (e != list_head (&open_file_list)) 
    {
      prev = list_prev (e);
      fd_struct = list_entry (e, struct file_descriptor, file_elem);
      if (fd_struct->fd_num == fd)
	    {
	      list_remove (e);
        file_close (fd_struct->file);
	      free (fd_struct);
	      return ;
     	}
      e = prev;
    }
  return ;
}

mapid_t mmap (int file_desc, void *address)
{
  struct thread *cur = thread_current();
  int fail = -1;
  
  if(address == NULL || (pg_ofs(address)!=0 )|| address == 0x0)
    return fail;
  if(file_desc==0 ||file_desc==1)
    return fail;
    
  struct file_descriptor *fd = get_current_file(file_desc);
  
  if(fd==NULL)
    return fail;
  
  int32_t length;
  length = file_length(fd->file);
  if(length <=0)
    return fail;
    
  int ofs = 0;
  while (ofs<length)
  {
    if(find_supple_entry(&cur->spt, address +ofs))
      return fail;
    if (pagedir_get_page(cur->pagedir,address +ofs))
      return fail;
      
      ofs += PGSIZE;
  }
  
  lock_acquire (&fileLock);
  struct file *f = file_reopen(fd->file);
  lock_release(&fileLock);
  if(f== NULL)
    return fail;
  else
    return add_mmf(address, f, length);
}
void munmap (mapid_t map)
{
  remove_mmfs(map);
}


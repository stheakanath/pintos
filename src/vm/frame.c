#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"  
#include "vm/frame.h"

static struct lock frame_lock;

/* addding and deleting from frames */
static bool insert_frame (void *);
static void delete_frame (void *);

//lookup function to find frames
static struct frame *get_frame (void *);

void
init_vm_frame ()
{
  list_init (&frames);
  lock_init (&frame_lock);
}
void *allocate_frame (enum palloc_flags pflags)
{
  void *frame = NULL;

  /* allocating a page */
  if (pflags & PAL_USER)
    {
      if (pflags & PAL_ZERO)
        frame = palloc_get_page (PAL_USER | PAL_ZERO);
      else
        frame = palloc_get_page (PAL_USER);
    }

  /* if it succeeds, add to frames list
     otherwise fail the allocator for now */
  if (frame != NULL)
    insert_frame (frame);
  else
    PANIC ("NO FRAME");

  return frame;
}
void free_vm_frame (void *frame)
{
  //delete the frame from the table
  delete_frame(frame);
  //free the memory
  palloc_free_page (frame);
  
  
}
void set_frame_usr (void* frame, uint32_t *page_table_entry, void *paddr)
{ 
  struct frame *frame_vm;
  frame_vm = get_frame (frame);
  if (frame_vm != NULL)
    {
      frame_vm->page_table_entry = page_table_entry;
      frame_vm->uvpaddr = paddr;
    }
}


static bool insert_frame (void *frame)
{
  struct frame* vm_frame;
  struct thread *t = thread_current();
  vm_frame = calloc(1, sizeof *vm_frame);
  
  if(vm_frame ==NULL)
    return false;
    
  vm_frame->thread = t->tid;
  vm_frame->frame = frame;
  
  lock_acquire(&frame_lock);
  list_push_back(&frames, &vm_frame->frame_elem);
  lock_release(&frame_lock);
  
  return true;
}
static void delete_frame (void *frame)
{
  struct frame* vm_frame;
  struct list_elem *frame_elem =list_head (&frames) ;
  
  lock_acquire(&frame_lock);
  while ((frame_elem = list_next(frame_elem)) != list_tail(&frames))
  {
    vm_frame = list_entry(frame_elem, struct frame, frame_elem);
    if(vm_frame->frame == frame)
    {
      list_remove (frame_elem);
      free(vm_frame);
      break;
    }  
  }
  lock_release(&frame_lock);

}
static struct frame *get_frame (void *frame)
{  
  struct frame* vm_frame;
  struct list_elem *frame_elem =list_head (&frames) ;
  
  lock_acquire(&frame_lock);
  while ((frame_elem = list_next(frame_elem)) != list_tail(&frames))
  {
    vm_frame = list_entry(frame_elem, struct frame, frame_elem);
    if(vm_frame->frame == frame)
      break;
    vm_frame = NULL;
      
  }
  lock_release(&frame_lock);
  
  return vm_frame;
}

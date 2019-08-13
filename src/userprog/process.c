#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

// ELF types in printf(). 
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

// p_type
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

// p_flags
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

struct Elf32_Ehdr // Executable header, beginning of an ELF binary
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

// Program header, There are e_phnum of these, starting at file offset e_phoff   
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/*******************************************************************/
// APIs















/*******************************************************************/

// kernel parent thread !!!!!!
// kernel user synch -> start_process(), wait until ELF finishes loading
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;


  // make a copy, otherwise race between caller and load()
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);


  // kernel spawn new user thread to run ELF
  // load ELF + interrupt switch to start running
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 


  // kernel wait for child thread start_process() to finish !!!!!
  // process_wait() could access load_ELF_status
  struct thread *child_thread = tid_to_thread(tid);
  sema_down(&child_thread->sema_load_elf);

  return tid;
}





// parent children double synchronization
// parent wait for children die before it exits + child wait till parent receives its exit_status
// e.g. wait(exec("elf_file")) -> child thread runs ELF, parent thread runs wait()
int
process_wait (tid_t child_tid) 
{
	struct thread *child_thread, *parent_thread;
	parent_thread = thread_current();
	child_thread = tid_to_thread(child_tid);

	// 1. all reasons parent does not need to wait for child
	// -> wrong child_tid 
	// -> no parent child relationship
  // -> wait() twice error
	if (child_thread == NULL || child_thread->parent != parent_thread || child_thread->waited)
		return -1;
	// -> child error status
  // -> child already exited
	if (child_thread->load_ELF_status != 0 || child_thread->exited == true)
		return child_thread->load_ELF_status;

  // 2. parent children double synchronization
	sema_down(&child_thread->sema_blocked_parent); // parent_thread -> child.sema.waiters[]
	// <---- restart point, child is exiting, lets get its load_ELF_status
	int ret = child_thread->load_ELF_status; // child wont exit until parent get return status from 
	sema_up(&child_thread->sema_blocked_child); // let child exit
	child_thread->waited = true; // wait() twice error
	return ret;
}





// parent children double synchronization + free resources
void
process_exit (void)
{
	struct thread *child_thread = thread_current();
	uint32_t *pd;
  
  // 1. child unblock parent to get its exit_status
	while (!list_empty(&child_thread->sema_blocked_parent.waiters))
		sema_up(&child_thread->sema_blocked_parent); // parent from child's sema
			 
	// 2. child block itself for parent to finish get its exit_status
	if (child_thread->parent != NULL)
		sema_down(&child_thread->sema_blocked_child);
	// <---- restart point after parent gets it return status

  // 3. free resources, clean up
  // TODO
  // thread->fd_list, thread->mmap_list, children_list->threads ??
  // dir_close(cur->cwd), vm_supt_destroy ??

  child_thread->exited = true; // parent wont wait() on exited child
  
	if (child_thread->elf_file != NULL) 
		file_allow_write(child_thread->elf_file);

  // destroy current thread's pagedir, switch to kernel only pagedir
  pd = child_thread->pagedir;
  if (pd != NULL) 
    {
      child_thread->pagedir = NULL; // timer interrupt can't switch back
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}





void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}




/*******************************************************************/
// major helpers
















/*******************************************************************/

// child thread in user space !!!!!
// threaded function spawned by kernel to run ELF
static void
start_process (void *file_name_)
{
  char *file_name = file_name_; // cmdline
  struct intr_frame if_;
  bool success;
  struct thread *user_thread = thread_current();


  // 1. init() interrupt_frame{} 
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;


  // 2. load() ELF + alloc() stack and index() at if_.esp
  // + init() pagedir, supt + notify kernel + deny_write(elf)
  success = load (file_name, &if_.eip, &if_.esp);
  palloc_free_page (file_name);
  // notify kernel that has been waiting for load() "success" 
  if (!success) {
    user_thread->load_ELF_status = -1; // error
    sema_up(&user_thread->sema_load_elf); 
    thread_exit ();
  } else {
    user_thread->load_ELF_status = 0; // success
    sema_up(&user_thread->sema_load_elf);
    user_thread->elf_file = filesys_open(file_name);
	  file_deny_write(user_thread->elf_file); // +1 deny_write_cnt
  }
    

  // 3. w() kernel args from intr_frame{} to user_stack 
  // TODO
  // malloc() ??
  char **cmdline_tokens = (char**) palloc_get_page(0); 
  char* token;
  char* save_ptr;
  int argc = 0;
  for (token = strtok_r(file_name, " ", &save_ptr); token != NULL;
      token = strtok_r(NULL, " ", &save_ptr))
  {
    cmdline_tokens[argc++] = token;
  }
  push_cmdline_to_stack(cmdline_tokens, argc,  &if_.esp);


  // 4. kernel "interrupt switch" to user ps  
  // start ps by simulating a return from interrupt i.e. jmp intr_exit(&if)
  // intr_exit() passes intr_frame{}/stack_frame to user ps 
  // pop to segment registers : intr_frame{}->%esp == cmdline stored on user_stack
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}







// 1. init() pagedir + supt + file
// 2. read() ELF header onto stack 
// 3. read() ELF into PA 
// 4. allocate page for user stack, index() at if_.esp
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;


  // 1. init() pagedir + supt + file
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  // 2. read() ELF header onto stack // 2. read() ELF header onto stack // 2. read() ELF header onto stack // 2. read() ELF header onto stack // 2. read() ELF header onto stack // 2. read() ELF header onto stack // 2. read() ELF header onto stack 
  // executable header
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  // program header
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
    
  // 3. read() ELF into PA 
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
  // 4. allocate page for user stack and index() at if_.esp
  if (!setup_stack (esp))
    goto done;
  *eip = (void (*) (void)) ehdr.e_entry; // start addr
  success = true;

 done:
  file_close (file);
  return success;
}






// read() ELF into PA 
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}






// allocate page for stack
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}


static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}







// last step in start_process() before intr_exit(intr_frame) n starts running ELF
static void
push_cmdline_to_stack (char* cmdline_tokens[], int argc, void **esp)
{
  ASSERT(argc >= 0);

  int i, len = 0;
  void* argv_addr[argc];
  for (i = 0; i < argc; i++) {
    len = strlen(cmdline_tokens[i]) + 1;
    *esp -= len;
    memcpy(*esp, cmdline_tokens[i], len);
    argv_addr[i] = *esp;
  }

  // word align
  *esp = (void*)((unsigned int)(*esp) & 0xfffffffc);

  // last null
  *esp -= 4; // 4 bytes
  *((uint32_t*) *esp) = 0;

  // setting **esp with argvs
  for (i = argc - 1; i >= 0; i--) {
    *esp -= 4;
    *((void**) *esp) = argv_addr[i];
  }

  // setting **argv (addr of stack, esp)
  *esp -= 4;
  *((void**) *esp) = (*esp + 4);

  // setting argc
  *esp -= 4;
  *((int*) *esp) = argc;

  // setting ret addr
  *esp -= 4;
  *((int*) *esp) = 0;

}


/*******************************************************************/
// helpers














/*******************************************************************/
/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}




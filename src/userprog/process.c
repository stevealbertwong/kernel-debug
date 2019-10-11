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
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"

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
static void push_cmdline_to_stack (char* cmdline_tokens[], int argc, void **esp);
static bool setup_stack (void **esp);
static bool install_page (void *upage, void *kpage, bool writable);

/*******************************************************************/
// PARENT KERNEL THREAD















/*******************************************************************/

/**
 * kernel pool parent thread
 * wait til start_process() done loading elf
 * 
 * 1. parse() elf_file out of full_cmdline 
 * 2. spawn child user thread start_process() to "assembly start" ELF
 * 3. wait() until finish loading ELF
 * 
 * TODO 
 * palloc(), init() pcb 
 */ 
tid_t
process_execute (const char *full_cmdline) // kernel parent thread !!!!!!
{
  char *full_cmdline_copy, *elf_file;
  char *strtoken_ptr = NULL;
  tid_t tid;

  // 1. parse() elf_file out of full_cmdline   
  full_cmdline_copy = palloc_get_page (0); // copy, otherwise race between process_execute() and start_process()
  if (full_cmdline_copy == NULL){
    palloc_free_page(full_cmdline_copy);
    return TID_ERROR;
  }
  strlcpy (full_cmdline_copy, full_cmdline, PGSIZE);

  elf_file = palloc_get_page (0);
  if (elf_file == NULL){
    palloc_free_page(full_cmdline_copy);
    palloc_free_page(elf_file);
    return TID_ERROR;
  }
  strlcpy (elf_file, full_cmdline, PGSIZE);
  elf_file = strtok_r(elf_file, " ", &strtoken_ptr); // parse() elf_file out of full_cmdline 

  // 2. spawn child user thread start_process() to "assembly start" ELF
  // load ELF + interrupt switch to start running
  tid = thread_create (elf_file, PRI_DEFAULT, start_process, full_cmdline_copy);
  if (tid == TID_ERROR){
    PANIC("process_execute() start_process() failed \n");
    palloc_free_page(full_cmdline_copy);
    palloc_free_page (elf_file); 
    return tid;
  }  

  // 3. wait() child start_process() done load ELF -> elf_exit_status
  // kernel_thread{} ready_list[] -> sema_load_elf[]
  struct thread *elf_thread = tid_to_thread(tid);
  // printf("process.c process_execute() before sema_down, tid: %d\n", elf_thread->tid);
  sema_down(&elf_thread->sema_load_elf); // wait child start_process()
  // printf("process.c process_execute() after sema_down \n");  
  
	if (elf_thread->elf_exit_status == -1){
    PANIC("process_execute() elf_thread->elf_exit_status == -1 \n");
    palloc_free_page(full_cmdline_copy);
    palloc_free_page (elf_file); 
    tid = TID_ERROR;
  }

  return tid;
}



/**
 * parent process_wait(process_exec()) 
 * parent wait() child exit() double sync
 */ 

// parent wait for child die before it exits + child wait till parent receives its exit_status
// e.g. wait(exec("elf_file")) -> child thread runs ELF, parent thread runs wait()
int
process_wait (tid_t child_tid) // child_tid == child thread's pid 
{
	// printf("process.c process_wait() starts running \n");
  struct thread *elf_thread, *parent_thread;
	parent_thread = thread_current();
	elf_thread = tid_to_thread(child_tid);

	// 1. elf_exit_status exception cases: all reasons parent does not need to wait for child
	// -> wrong child_tid / no parent child relationship / wait() twice error
	if (elf_thread == NULL || elf_thread->parent != parent_thread || elf_thread->waited){
    PANIC("process.c process_wait() system error \n");
    return -1;
  }
		
  elf_thread->waited = true; // -> child error status / child already exited
	if (elf_thread->elf_exit_status != 0 || elf_thread->exited == true){
    PANIC("process.c process_wait() exec() elf code already exited before wait() \n");
    return elf_thread->elf_exit_status;
  }

  // 2. parent wait(exec()) waits child elf code calls exit()
  // printf("process.c process_wait() gets to sema_down() and starts waiting for exec() elf code \n");
  sema_down(&elf_thread->sema_elf_call_exit); // parent_thread block itself -> child.sema.waiters[]
	
  // <---- restart point, child is exiting, lets get its elf_exit_status
	int ret = elf_thread->elf_exit_status; // child wont exit until parent get return status from 
	// printf("process.c process_wait() gets to sema_down() about to sema_up and finish \n");
  sema_up(&elf_thread->sema_elf_exit_status); // unblock child, let child exit
	elf_thread->waited = true; // prevent wait() twice error
	
  return ret;
}


/**
 * TODO !!!!!
 * parent children double synch (child "notify" parent)
 * notify == put parent thd back to ready_list
 * 
 * child elf code block itself for parent process_wait() get its exit_status
 * 
 * future projects
 * thread->fd_list, thread->mmap_list, children_list->threads ??
 * dir_close(cur->cwd), vm_supt_destroy ??
 * 
 * 
 * block itself first ?? so elf won't exit before wait ??
 */ 
void
process_exit (void)
{	
  struct thread *child_thread = thread_current();
	uint32_t *pd;
  if (child_thread->elf_file != NULL){
    file_allow_write(child_thread->elf_file);
    file_close(child_thread->elf_file);
  }

  // 1. child unblock parent to get its exit_status
	while (!list_empty(&child_thread->sema_elf_call_exit.waiters)){
    // printf("process.c process_exit() before sema_up, tid: %d\n", child_thread->tid);
    sema_up(&child_thread->sema_elf_call_exit);
    // printf("process.c process_exit() after sema_up \n");
  }
  
  child_thread->exited = true; // parent wont wait() on exited child

	// 1. child elf code block itself for parent process_wait() get its exit_status
	if (child_thread->parent != NULL){
    // printf("process.c process_exit() before sema_down, tid: %d\n", child_thread->tid);
		sema_down(&child_thread->sema_elf_exit_status);
    // printf("process.c process_exit() after sema_down \n");
  }

	// <---- child's restart point after parent gets it return status
  
  // 2. palloc_free() vm data structure, elf code(eip), stack n cmdline(esp)
  // destroy current thread's pagedir, switch to kernel only pagedir
  pd = child_thread->pagedir;
  if (pd != NULL) 
    {
      child_thread->pagedir = NULL; // timer interrupt can't switch back
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}






/*******************************************************************/
// CHILD KERNEL THREAD -> USER THREAD 
// load_ELF(), push_args(), intr_frame{}, intr_switch()
















/*******************************************************************/
/**
 * kernel pool child thread
 * 
 * starting point of child kernel thread
 * spawned by parent kernel thread process_execute() to "assembly start" ELF
 * 
 */ 
static void
start_process (void *full_cmdline)
{
  struct intr_frame if_;
  bool success = false;
  struct thread *elf_thread = thread_current();

  // 1. parse() full_cmdline into elf_file
  char *elf_file = full_cmdline;
  char **cmdline_tokens = (char**) palloc_get_page(0);
  if (cmdline_tokens == NULL){
    palloc_free_page(cmdline_tokens);
    // PANIC("start_process() no cmdline tokens \n");
    system_call_exit(-1);
  } 
  char* token;
  char* strtok_ptr;
  int argc = 0;
  for (token = strtok_r(elf_file, " ", &strtok_ptr); token != NULL;
      token = strtok_r(NULL, " ", &strtok_ptr))
  {
    cmdline_tokens[argc++] = token;
  }
  // 2. populates() new intr_frame{} 
  // intr_fra{} index() user stack, passed as arg, when "assembly start" ps
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;


  // 3. palloc(), load() ELF + palloc() stack, index() at if_.esp
  // + init() pagedir, supt + notify kernel + deny_write(elf)
  success = load (elf_file, &if_.eip, &if_.esp);

  // 4.1 free kernel process_execute() thread, quit kernel start_process() thread
  if (!success) { // load failed e.g. filename is null
    PANIC("start_process() load() failed \n");
    elf_thread->elf_exit_status = -1; // error
    sema_up(&elf_thread->sema_load_elf); // parent kernel thread back to ready_list
    palloc_free_page(cmdline_tokens);    
    system_call_exit(-1);
  } else { // if success
    // 4.2 unblock kernel_thread after load_elf() + push kernel args to user_stack 
    push_cmdline_to_stack(cmdline_tokens, argc,  &if_.esp);
    elf_thread->elf_exit_status = 0;
    elf_thread->elf_file = filesys_open(elf_file);
	  file_deny_write(elf_thread->elf_file); // +1 deny_write_cnt
    // printf("process.c start_process() before sema_up, tid: %d\n", elf_thread->tid);
    sema_up(&elf_thread->sema_load_elf); // notify parent process_execute()
    // printf("process.c start_process() after sema_up \n");
  }
  
  // 6. kernel "interrupt switch" to user ps  
  // "assembly start" ps by simulating a return from interrupt i.e. jmp intr_exit(&if)
  // intr_exit() passes intr_frame{}/stack_frame to user ps 
  // pop to segment registers : intr_frame{}->%esp == cmdline stored on user_stack
  printf("process.c start_process() change control to elf code !!!!!!!!!!!!!!!!!!! \n");
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");

  palloc_free_page(cmdline_tokens);
  if(elf_file) palloc_free_page(elf_file);
  NOT_REACHED ();
}




/**
 * 
 * 1. init() pagedir + supt + file
 * 2. read() ELF header onto stack 
 * 3. read() ELF into PA 
 * 4. allocate page for user stack, index() at if_.esp
 */ 
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
  if (t->pagedir == NULL){
    PANIC("process.c pagedir_create() failed !!! \n");
    goto done;
  } 
#ifdef VM
  // not in thread_create(): only user thread with elf has supt
  // not in init_thread(): vm_supt_init() contains malloc(), X before "booting completes"  
  t->supt = vm_supt_init();
  if(!t->supt){
    PANIC("load() vm_supt_init() failed \n");
  }
#endif
    
  process_activate ();
  file = filesys_open (file_name); // BUG!!!! 
  if (file == NULL) 
    {
      PANIC ("load: %s: open failed\n", file_name);
      goto done; 
    }

  // 2. read() ELF header onto kernel stack 
  // executable header
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      PANIC ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  // program header
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file)){
        PANIC("process.c file_length() failed !!! \n");
        goto done;

      }        
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr){
        PANIC("process.c file_read() failed !!! \n");
        goto done;
      }
        
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
    
  // 3. based on header, palloc() kpage for code/text/bss + read() ELF into PA 
              // printf("load_segment() begin \n");   
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable)){
                                   PANIC("process.c load_segment() failed !!! \n");
                                   goto done;
                                 }                              
            }
          else{
            PANIC("process.c validate_segment() failed !!! \n");
            goto done;
          }
          break;
        }
    }
  // 4. palloc() user stack and index() at if_.esp
  if (!setup_stack (esp)){
    PANIC("process.c setup_stack() failed !!! \n");
    goto done;
  }    

  *eip = (void (*) (void)) ehdr.e_entry; // start addr
  success = true;

 done:
  file_close (file);
  if(!success){
    PANIC("load() failed \n");
  }
  return success;
}


/**
 * KEY FUNCTION !!!!!!! push arguments !!!!!!!!
 * called by child user thread start_process()
 * 
 * according to x86 calling convention 
 * called before intr_exit(intr_frame) to "assembly start" ELF 
 * esp == VA
 */ 
static void
push_cmdline_to_stack (char* cmdline_tokens[], int argc, void **esp)
{
  ASSERT(argc >= 0);

  // push cmdline_tokens to user stack
  int i, len = 0;
  void* argv_addr[argc];
  for (i = 0; i < argc; i++) {
    len = strlen(cmdline_tokens[i]) + 1;
    *esp -= len; // "-ve increment" esp
    memcpy(*esp, cmdline_tokens[i], len); // memcpy works on VA
    argv_addr[i] = *esp; // each cmdtoken's stack addr
  }

  // word align with 0 paddings
  *esp = (void*)((unsigned int)(*esp) & 0xfffffffc);

  // last null before argv[]
  *esp -= 4; // 4 bytes
  *((uint32_t*) *esp) = 0;
  
  // push argv[]
  for (i = argc - 1; i >= 0; i--) {
    *esp -= 4;
    *((void**) *esp) = argv_addr[i];
  }

  // push argv
  *esp -= 4;
  *((void**) *esp) = (*esp + 4);

  // push argc
  *esp -= 4;
  *((int*) *esp) = argc;

  // push null as ret addr
  *esp -= 4;
  *((int*) *esp) = 0;
  
  // hex_dump((uintptr_t)*esp, *esp, sizeof(char) * 56, true);
}


/*******************************************************************/
//key helpers
















/*******************************************************************/
/**
 * read() ELF into PA 
 * called by load(), upage is from ELF program header 
 * 
 * 1. palloc() kpage
 * 2. read() file from disk into kpage
 * 3. u() pagedir kpage upage mapping
 */ 
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
#ifdef VM

      // 1. fill unused kpage w 0s      
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      if(pagedir_get_page(thread_current()->pagedir, upage) != NULL){
        PANIC("load_segment() elf code VA already exists! \n");
      }
      // 2, lazy load (involves file_read())
      bool success = vm_supt_install_filesystem(thread_current()->supt, upage,
            file, ofs, page_read_bytes, page_zero_bytes, writable);
      if(!success){
        PANIC("load_segment() vm_supt_install_filesystem failed \n");
      }
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
#else
      // 1. fill unused kpage w 0s      
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      // 2. palloc() kpage from user pool
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        {        
        PANIC("load_segment() palloc_get_page() failed \n");  
        return false;
        }      
      
      // 3. read() file from disk into kpage
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          PANIC("load_segment() file_read() failed \n");  
          palloc_free_page (kpage);
          return false; 
        }
      // 4. zeros out extra space in page
      memset (kpage + page_read_bytes, 0, page_zero_bytes);
      
      // 5. u() pagedir
      if (!install_page (upage, kpage, writable)) 
        {
          PANIC("load_segment() install_page() failed \n");  
          palloc_free_page (kpage);
          return false; 
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
#endif 

    }  
  return true;


}

/**
 * allocate() 1 page(zeroed) for user_stack at top of user virtual memory
 * called by load()
 * 
 * 1. palloc() user stack 
 * 2. u() pagedir, supt
 */ 
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

#ifdef VM
  // user stack is at 1st segment below PHYS_BASE (PHYS_BASE - PGSIZE)
  kpage = vm_palloc_kpage (PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
  if(!kpage){
    PANIC("setup_stack() vm_palloc_kpage() failed \n");
  }
  // pin during installing supt: when evict there will be a spte to u() 
  if(!vm_supt_install_frame (thread_current()->supt, PHYS_BASE - PGSIZE, kpage)){
    PANIC("setup_stack() vm_supt_install_frame() failed \n");
  }
  // RMB to unpin() everytime after vm_palloc_kpage()  
  vm_unpin_kpage(kpage); 
  
  // ALTERNATIVELY: 
  // vm_supt_install_zero_page (thread_current()->supt, fault_page);
  // vm_load_page(thread_current()->supt, thread_current()->pagedir, fault_page);
  // return true;
  
#else
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
#endif

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

/**
 * update VM data structure e.g. pagedir, supt for new upage kpage mapping
 * called by setup_stack()
 * 
 */ 
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));

}




/*******************************************************************/
// helpers














/*******************************************************************/

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




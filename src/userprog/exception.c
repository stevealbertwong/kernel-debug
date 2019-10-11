#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/frame.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/*****************************************************************
// IMPORTANT!!!













******************************************************************/
/**
 * stack grow !!!
 * called by TLB miss interrupt
 * 
 * 1. get faulty VA + error code from cr2 register
 * 2. determine pagefault/segfault
 * 2.1. segfault
 *    - user mode access kernel addr e.g. malicious test code try to bypass syscall to access kernel space with user elf code
 *    - null pointer
 *    - not present flag
 *    - absolute limit on stack size 8MB
 *    - failed both pagefault test  
 * 2.2 pagefault
 *    - TEST: thread_current()->supt{} contains VA entry
 *       - load page from disk using supt
 *       - casued by swapped out/lazyload filesystem
 *    - TEST: valid stack access, next contiguous memory page, within 32 bytes of stack ptr
 *       - stack growth
 *       - casued by ELF code upages that are not lazyloaded
 * 
 * NOTE: possible stack grow scenarios: 
 * - PUSH and PUSHA cause fault 4 and 32 bytes below stack pointer
 * - elf may allocate stack space by decrementing stack ptr
 * then write to a stack m bytes above current stack pointer
 *    SUB $n, %esp
 *    MOV ..., m(%esp) 
 * 
 */ 
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present pagedir page cuasing page fault interrupt, false: writing read-only pagedir page causing page fault interrupt. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */
  
  printf("page_fault() is called !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
  // 1. get faulty VA + error code from cr2 register
  // it may point to code/data, not necessarily instruction (intr_frame->eip)
  asm ("movl %%cr2, %0" : "=r" (fault_addr));
  intr_enable (); // turn interrupts back on -> were off to read CR2 before it changed
  page_fault_cnt++;
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

#ifdef VM
  // 2. determine segfault/pagefault
  // 2.1 segfault obvious fault_addr
  // user mode access kernel addr / not present flag / null pointer  
  if ((is_kernel_vaddr(fault_addr) && user)){
      PANIC("pagefault() segfault() user mode access kernel addr \n");
      system_call_exit(-1);     
  }
  if(!not_present){ // not non-present pagedir page(allows stack growth), but read only pagedir page(kills immediately)
      PANIC("pagefault() segfault() faulty addr not present in pagedir \n");
      system_call_exit(-1);
  } 
  if(!fault_addr){
      PANIC("pagefault() segfault() faulty addr is null \n");
      system_call_exit(-1);
  }

   // 2.2 pagefault tests
   // obtain user elf's stack pointer (from intr_frame->esp or thread_current()->current_esp)   
   // if page fault in user mode, faulty addr stored in intr_frame
   // if page fault in kernel mode, faulty addr stored at current esp at the beginning of system call
   // void* esp = user ? f->esp : thread_current()->current_esp;
   void* esp = f->esp;

   // supt's key == 4KB page == VA needs to round down to page
   void* fault_page = (void*) pg_round_down(fault_addr);   

   // 2.2.1 TEST: check if VA already regitered in supt 
   if(vm_supt_search_supt(thread_current()->supt, fault_page)){
      printf("pagefault() supt has entry: status %d \n ", vm_supt_search_supt(thread_current()->supt, fault_page)->status);
      vm_load_kpage_using_supt(thread_current()->supt, thread_current()->pagedir, fault_page);
      return; // succeeds

   // 2.2.2 TEST: if not in supt, check if valid stack access
   } else {
      printf("pagefault() supt DOES NOT has entry \n ");

      if(!((PHYS_BASE - 0x800000) <= fault_addr && fault_addr < PHYS_BASE )){
         PANIC("pagefault() 8MB stack limit, 0xc0000000(PHYS_BASE: 3GB) - 0x800000 segfault addr \n");
         system_call_exit(-1);
      }
      // fault above stack ptr or next contiguous memory page(within 32 bytes of stack ptr) 
      if(!(fault_addr >= (esp - 32))){ 
         PANIC("pagefault() exceeds next contiguous memory \n");
         system_call_exit(-1);
      }

      // stack grow
      if(!vm_supt_install_zero_page (thread_current()->supt, fault_page)){
         PANIC("pagefault() vm_supt_install_zero_page() failed \n");
      }      
      if(!vm_load_kpage_using_supt(thread_current()->supt, thread_current()->pagedir, fault_page)){
         PANIC("pagefault() vm_load_kpage_using_supt() failed \n");
      }

      return; // succeeds

   }
	//    f->eip = (void *) f->eax; 
	//    f->eax = 0xffffffff;

   //    printf ("Seg fault at %p: %s error %s page in %s context.\n",
   //             fault_addr,
   //             not_present ? "not present" : "rights violation",
   //             write ? "writing" : "reading",
   //             user ? "user" : "kernel");
   //    kill (f);    

#endif

  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
}





/*****************************************************************
// helpers













******************************************************************/
/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}


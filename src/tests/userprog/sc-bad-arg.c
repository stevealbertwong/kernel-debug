/* Sticks a system call number (SYS_EXIT) at the very top of the
   stack, then invokes a system call with the stack pointer
   (%esp) set to its address.  The process must be terminated
   with -1 exit code because the argument to the system call
   would be above the top of the user address space. */
   
// this assembly code bypass your syscall implementation 
// resulting in page fault error that should system_call_exit(-1)
// directly in page_fault_handler() in exception.c 

#include <syscall-nr.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  asm volatile ("movl $0xbffffffc, %%esp; movl %0, (%%esp); int $0x30"
                : : "i" (SYS_EXIT));
  fail ("should have called exit(-1)");
}

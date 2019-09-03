/* Tries to execute a nonexistent process.
   The exec system call must return -1. */

// call process_execute() within process_execute()
// 1st load this elf, 2nd syscall exec()
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("exec(\"no-such-file\"): %d", exec ("no-such-file"));
}

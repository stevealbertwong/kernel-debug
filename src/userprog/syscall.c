#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "vm/frame.h"
#include "vm/page.h"


#define STDIN_FILENO 0
#define STDOUT_FILENO 1

static void syscall_handler (struct intr_frame *);
struct lock file_lock; // global file lock -> multi-threads access same file
struct file_desc *get_file_desc(int fd);
struct mmap_desc *get_mmap_desc(int mmapid);
void pin_and_grow_buffer(const void *buffer, unsigned size);
void unpin_buffer(const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

/***************************************************************/
// kernel side syscall starting point -> redirect to implementation 
// e.g. parse() args from intr_frame->esp, then syscall_exec(arg), then process_execute(arg)










/***************************************************************/

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	int *syscall_number = f->esp;
	int *argument = f->esp;
	int ret_val = 0;
	// printf("syscall no: %d \n", *syscall_number);
	// printf("syscall arg: %d \n", *(argument + 1));
	if (is_user_vaddr(syscall_number))
	{
		switch (*syscall_number)
		{
		
		// General
		case SYS_HALT:
			system_call_halt();
			break;

		
		// PS + VM
		case SYS_EXEC:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_exec((const char *) *(argument + 1));
			else // if kernel addr
				system_call_exit(-1);
			break;
		case SYS_WAIT:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_wait((pid_t) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_EXIT:
			if (is_user_vaddr(argument + 1))
				system_call_exit(*(argument + 1));
			else
				system_call_exit(-1);
			break;


		// FS
		case SYS_CREATE:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)){
				ret_val = system_call_create((const char *) *(argument + 1),
						(unsigned) *(argument + 2));				
			}else{
				// printf("syscall.c case SYS_CREATE: is_user_vaddr(argument) failed %d \n", ret_val);
				system_call_exit(-1);
			}

			break;
		case SYS_REMOVE:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_remove((const char *) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_OPEN:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_open((const char *) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_FILESIZE:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_filesize(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_READ:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)
					&& is_user_vaddr(argument + 3))
				ret_val = system_call_read(*(argument + 1),
						(void *) *(argument + 2), (unsigned) *(argument + 3));
			else
				system_call_exit(-1);
			break;
		case SYS_WRITE:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)
					&& is_user_vaddr(argument + 3))
				ret_val = system_call_write(*(argument + 1),
						(const void *) *(argument + 2),
						(unsigned) *(argument + 3));
			else
				system_call_exit(-1);
			break;
		case SYS_SEEK:
			// fd, pos
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2))
				system_call_seek(*(argument + 1), *(argument + 2));
			else
				system_call_exit(-1);
			break;
		case SYS_TELL:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_tell(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_CLOSE:
			if (is_user_vaddr(argument + 1))
				system_call_close(*(argument + 1));
			else
				system_call_exit(-1);
			break;
#ifdef VM
		case SYS_MMAP:
			if (is_user_vaddr(argument + 1))
				system_call_mmap((int)*(argument + 1), (void *) *(argument + 2));
			else
				system_call_exit(-1);
			break;

		case SYS_MUNMAP:
			if (is_user_vaddr(argument + 1))
				system_call_munmap((int)*(argument + 1));
			else
				system_call_exit(-1);
			break;
#endif

		default:
			system_call_exit(-1);
		}
	}
	else{ // intr_frame->esp not in user address
		// printf("syscall.c is_user_vaddr(syscall_number) %d \n", ret_val);
		system_call_exit(-1);
	}
	
	// syscall return thru intr_frame{}, then intr_exit(),
	// then back to user side syscall(), then user program
	f->eax = ret_val;
}



/***************************************************************/
// PS kernel side syscalls implementation

// TODO
// when lock_acquire() ??














/***************************************************************/

/**
 * Runs the executable whose name is given in cmd_line, 
 * passing any given arguments, and returns the new process's program id (pid). 
 */ 
pid_t system_call_exec(const char *cmd_line)
{
	tid_t tid;
	lock_acquire(&file_lock);
	tid = process_execute(cmd_line);
	lock_release(&file_lock);
	return tid;
}


int system_call_wait(pid_t pid)
{
	// why no lock ?? double dead lock when called with exec() ??
	return process_wait(pid);
}


/**
 * Terminates the current user program, returning status to the kernel. 
 * If the process's parent waits for it (see below), this is the status that will be returned
 * status of 0 == success, nonzero values == errors.
 * 
 * 1. palloc_free() global syscall lock, fd_list[]->file/dir
 * 		syscall_exit()
 * 2. synch + return elf exit status + palloc_free() parent/child pcb 
 * 		thread_exit() -> process_exit()
 * 3. palloc_free() vm data structure, elf code(eip), stack n cmdline(esp)
 * 		process_exit() -> pagedir_destroy()
 * 4. palloc_free() locks, thread_list, thread
 * 		thread_exit()
 * 
 * TODO: synch ?? return ELF status ??
 * syscall handler bug ?? 
 * open a pcb branch before making changes !!!!
 */ 
void system_call_exit(int status)
{
	struct thread *t;
	struct list_elem *e;

	t = thread_current();
	
	// 1. palloc_free() global syscall lock
	if (lock_held_by_current_thread(&file_lock))
		lock_release(&file_lock);

	// 1. palloc_free() fd_list[]->file/dir
	struct list *fd_list = &t->fd_list;
	
	while (!list_empty(fd_list))
	{
		// struct list_elem *e = list_begin (fd_list);
    	// struct file_desc *desc = list_entry(e, struct file_desc, fd_list_elem);
    	// if(desc->d != NULL){
		// 	dir_close(desc->d);
		// }else{
		// 	file_close(desc->f);
		// }
    	// list_remove(&desc->fd_list_elem);
		// free(desc);		
		e = list_begin(&t->fd_list);
		system_call_close(list_entry (e, struct file_desc, fd_list_elem)->id);
	}

	// how to return elf exit status ?? 
	t->elf_exit_status = status;	
	printf("%s: exit(%d)\n", t->name, t->elf_exit_status);
	
	// palloc_free() thread_list, thread, pcb(process_exit())
	thread_exit();
}


void system_call_halt(void)
{
	shutdown_power_off();
}





/***************************************************************/ 
// FS kernel side syscalls implementation
// mostly thread->fd_list[i]->file or thread->fd_list[i]->dir

// TODO -> change global FS syscall lock to read write lock

// why fd ?? quick access fd_list, instead of for loop filename search














/********************************************************************/
/**
 * returns fd or -1 if the file could not be opened.
 * fd 0 (STDIN_FILENO) is standard input, 
 * fd 1 (STDOUT_FILENO) is standard output. 
 * fd 2 is stanfard error
 * Each process has an independent set of file descriptors
 * 
 * 1. given file_name, find inode + malloc(), populate() file{}
 * 2. malloc() file_desc{}
 * 3. append() file{} to file_desc{}
 * 4. append() file_desc{} to fd_list[]
 * 
 * NOTE: user thread(not switched to kernel thread) running kernel code
 */
int system_call_open(const char *file_name)
{
	if (file_name != NULL){
		// 1. given file_name, find inode + malloc(), populate() file{}
		lock_acquire(&file_lock); // multi-threads open() same file
		struct file *file = filesys_open(file_name);
		lock_release(&file_lock);
		if (file == NULL){
			// printf("system_call_open() null file \n");
			return -1;			
		}
		// 2. malloc() file_desc{} -> fd free() by system_call_close
		struct file_desc *file_desc = (struct file_desc *) malloc(
				sizeof(struct file_desc));
		if (file_desc == NULL)
		{
			free(file_desc);
			file_close(file);
			return -1;
		}

		// 3. append() file{} to file_desc{}
		lock_acquire(&file_lock);		
		file_desc->f = file;
		thread_current()->total_fd +=1; 
		file_desc->id = thread_current()->total_fd;
		// printf("syscall.c fid: %d, tid: %d calling system_call_open() \n", file_desc->id, thread_current()->tid);
		
		// 4. append() file_desc{} to fd_list[]
		list_push_back(&thread_current()->fd_list, &file_desc->fd_list_elem);
		
		// DEBUG purpose
		// struct file_desc *file_desc_copy = get_file_desc(file_desc->id);
		// if (file_desc_copy != NULL && file_desc_copy->f != NULL){
		// 	printf("system_call_open() reopen succeeded ! \n");			
		// }else{
		// 	printf("system_call_open() reopen failed ! \n");
		// }
		
		lock_release(&file_lock);
		return file_desc->id;
	}else{ // filename == null
		system_call_exit(-1);
		return -1;
	}
}


/**
 * palloc_free() fd_list[i]->file/dir n file_desc
 * 
 * 1. for-loop thread->fd_list[] for file_desc{}
 * 2. remove() file_desc{} from thread->fd_list[]
 * 3. free() file/dir n file_desc
 */
void system_call_close(int fd)
{
	// 1. for-loop thread->fd_list[] for file_desc{}
	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);

	// if (file_desc == NULL){
	// 	// system_call_exit(-1);
	// 	// printf("syscall.c system_call_close() file_desc == NULL \n");
	// }else{

	if(file_desc && file_desc->f){
		// 2. remove() file_desc{} from thread->fd_list[]
		list_remove(&file_desc->fd_list_elem);
		// 3. free() file/dir
		file_close(file_desc->f);
		// if(file_desc->d != NULL) dir_close(file_desc->d);
		free(file_desc);
	}
	lock_release(&file_lock);
}


/**
 * Reads size bytes from the file open as fd into buffer. 
 * Returns the number of bytes actually read (0 at end of file), 
 * or -1 if the file could not be read (due to a condition other than end of file). 
 * 
 * 1. check fd, buffer 
 * 2. for-loop() file_desc 
 * 3. file_read()
 */ 
int system_call_read(int fd, void *buffer, unsigned size)
{
	unsigned int offset;
	// 1. check buffer
	if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size))
		system_call_exit(-1);

	// 1. check fd
	switch (fd){
	case STDIN_FILENO://Fd0/STDIN reads from keyboard w input_getc()		
		for (offset = 0; offset < size; ++offset)
			*(uint8_t *) (buffer + offset) = input_getc();
		return size;
	case STDOUT_FILENO: // standard out
		return -1;
	
	// 2. for-loop() file_desc
	default:
		lock_acquire(&file_lock);
		struct file_desc *file_desc = get_file_desc(fd);
		lock_release(&file_lock);
		if (file_desc == NULL){
			// printf("system_call_read() null fd %d\n", fd);
			return -1;
		}
		
		// 3. file_read()
#ifdef VM // support file bigger than 1 page(4KB)
		pin_and_grow_buffer(buffer, size); // buffer has existing kpages
#endif
		lock_acquire(&file_lock);
		int bytes_read = -1;
		bytes_read = file_read(file_desc->f, buffer, size);
		lock_release(&file_lock);
#ifdef VM	
		unpin_buffer(buffer,size);
#endif
		return bytes_read;


	}
}




/**
 * Writes size bytes from buffer to the open file fd. 
 * Returns the number of bytes actually written,
 * 
 * 
 */ 
// check fd, buffer + for-loop() file_desc + file_write()
// int system_call_write(int fd, const void *buffer, unsigned size)
// {
// 	// 1. check buffer
// 	if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size))
// 		system_call_exit(-1); // prevent user w() kernel memory to disk
	
// 	// 1. check fd
// 	switch (fd){
// 	case STDIN_FILENO: // 0
// 		return -1;
// 	case STDOUT_FILENO: // 1 -> printf()
// 		putbuf(buffer, size); // w() to stdout
// 		return size;
	
// 	default: // normal fd
// 		// 2. for-loop() file_desc
// 		lock_acquire(&file_lock);
// 		struct file_desc *file_desc = get_file_desc(fd);
// 		printf("syscall.c system_call_write() fd:%d, file_desc->id:%d, tid:%d \n", fd, file_desc->id, thread_current()->tid);
// 		if (file_desc == NULL){ // attempt to w() dir
// 			// printf("syscall.c system_call_write() file_desc is null %d \n", fd);
// 			lock_release(&file_lock);
// 			return -1;
// 		}
		
// 		// 3. file_write()
// 		int bytes_written = -1;
// 		// file_allow_write(file_desc->f);
// 		bytes_written = file_write(file_desc->f, buffer, size);
// 		printf("syscall.c system_call_write() bytes_written %d, size %d \n", bytes_written, size);
// 		lock_release(&file_lock);
// 		return bytes_written;
// 	}
// }


int system_call_write(int fd, const void *buffer, unsigned size)
{
	if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size))
		system_call_exit(-1); // prevent user w() kernel memory to disk	
	
#ifdef VM // support file bigger than 1 page(4KB)
	pin_and_grow_buffer(buffer, size); // buffer has existing kpages
#endif
	
	lock_acquire(&file_lock);
	int ret;

	if(fd == 1) { // write to stdout
		putbuf(buffer, size);
		ret = size;
	}
	else {
		// write into file
		struct file_desc* file_d = get_file_desc(fd);

		if(file_d && file_d->f) { // previous buggy line
			ret = file_write(file_d->f, buffer, size);
		}else{
			ret = -1;
		}				
	}
	lock_release(&file_lock);

#ifdef VM	
	unpin_buffer(buffer, size);
#endif

	return ret;
}




/**
 * grow new stack for buffer
 * called by syscall_read(), syscall_write()
 * 
 * NOTE: buffer_addr has already gone through pagefault() in hardware layer
 * i.e. if buffer_addr is within stack growth, already grown
 */ 
void pin_and_grow_buffer(const void *buffer, unsigned size){
	// printf("pin_and_grow_buffer() is called \n");
	struct hash *supt = thread_current()->supt;
  	uint32_t *pagedir = thread_current()->pagedir;
	void *upage; // buffer == upage
	for(upage = pg_round_down(buffer); upage < buffer + size; upage += PGSIZE)
	{
		// no need to grow stack !!!!!!
		// if(!vm_supt_search_supt(supt, upage)){
		// 	vm_supt_install_zero_page(supt, upage); 
		// }
		
		// reload kpage even done in pagefault as might be evicted
		vm_load_kpage_using_supt (supt, pagedir, upage);						
		vm_pin_upage(supt, upage);

	}
}

/**
 * unpin upage's underlying kpage
 * 
 */ 
void unpin_buffer(const void *buffer, unsigned size){
	struct hash *supt = thread_current()->supt;
	void *upage; // buffer == upage
	for(upage = pg_round_down(buffer); upage < buffer + size; upage += PGSIZE)
	{		
		vm_unpin_upage(supt, upage); 
	}
}


/**
 * creates an empty file or sub_dir without opening it 
 * no fd involved !!!!
 */ 
bool system_call_create(const char *file_name, unsigned initial_size)
{
	if (file_name != NULL)
	{
		lock_acquire(&file_lock);
		bool success = filesys_create(file_name, initial_size);
		lock_release(&file_lock);
		return success;
	}
	else{
		system_call_exit(-1);
	}
	return false;
}


/**
 * Deletes a file called "file" without opening it 
 * no fd involved !!!!
 */ 
bool system_call_remove(const char *file_name)
{
	if (file_name != NULL)
	{
		lock_acquire(&file_lock);
		bool success = filesys_remove(file_name);
		lock_release(&file_lock);
		return success;
	}
	else
		system_call_exit(-1);
	return false;
}


// when seek pointer is
unsigned system_call_tell(int fd)
{
	unsigned position = 0;

	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);
	lock_release(&file_lock);

	if (file_desc == NULL)
		return -1;

	lock_acquire(&file_lock);
	position = file_tell(file_desc->f);
	lock_release(&file_lock);

	return position;
}



void system_call_seek(int fd, unsigned position)
{
	
	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);
	lock_release(&file_lock);

	if (file_desc == NULL){
		printf("system_call_seek() null fd \n");
		system_call_exit(-1);
	}
		
	lock_acquire(&file_lock);
	file_seek(file_desc->f, position);
	lock_release(&file_lock);
}


int system_call_filesize(int fd)
{
	// 1. for-loop thread->fd_list[] for file_desc{}
	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);
	lock_release(&file_lock);
	if (file_desc == NULL){
		// printf("system_call_filesize() null fd :%d \n", fd);
		return -1;
	}
	// 2. file_length()
	lock_acquire(&file_lock);
	int size = file_length(file_desc->f);
	lock_release(&file_lock);

	return size;
}



// given fd, for-loop fd_list[] for file_desc{}
struct file_desc *
get_file_desc(int fd)
{
	struct file_desc *file_desc;
	struct list_elem *e;
	struct thread *t;

	t = thread_current();
	for (e = list_begin(&t->fd_list); e != list_end(&t->fd_list); e = list_next(e))
	{
		file_desc = list_entry(e, struct file_desc, fd_list_elem);
		if (file_desc->id == fd)
			return file_desc;
	}
	return NULL;
}




/***************************************************************/ 
// VM kernel side syscalls implementation

















/***************************************************************/
/**
 * directly map an "opened" file into RAM
 * 
 * 1. given fd, get file_desc->file{} 
 * 		- needed to store file{} in supt
 * 
 * 2. update supt to lazy load 
 * 		- NOT direct file_read()
 * 		- upage:file_content mapping
 * 		- use file size to calculate num of pages needed
 * 		- for loop file pages, add supt entry (filesystem)
 * 
 * 3. add() to thread->mmap_list[]
 * 		- mmap{} index() RAM mapped file: VA to delete supt entry
 * 
 * different from unix mmap() syscall
 * https://www.poftut.com/mmap-tutorial-with-examples-in-c-and-cpp-programming-languages/
 */ 
int system_call_mmap(int fd, void *upage){
	lock_acquire (&file_lock);

	struct thread *curr = thread_current();
	struct mmap_desc *mmap_desc = (struct mmap_desc*) malloc(sizeof(struct mmap_desc));


	// 1. given fd, duplicate file_desc->file{} 
	struct file_desc *file_desc = get_file_desc(fd);
	if(!file_desc){
		PANIC("system_call_mmap() fd has no file_desc{} \n");
	}		
	// avoid double free() same file{}
	mmap_desc->dup_file = file_reopen(file_desc->f); 
	

	// 2. update supt to lazy load 
	size_t file_size = file_length(mmap_desc->dup_file);
	size_t offset;
	for (offset = 0; offset < file_size; offset += PGSIZE) {
		void *elf_va = upage + offset; // user virtual addr

		size_t read_bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
		size_t zero_bytes = PGSIZE - read_bytes;

		bool success = vm_supt_install_filesystem(curr->supt, elf_va,
			mmap_desc->dup_file, offset, read_bytes, zero_bytes, /*writable*/true);
		
		if (!success){
			PANIC("system_call_mmap() vm_supt_install_filesystem() failed \n");
		}
	}


	// 3. add() to thread->mmap_list[]
	uint32_t mmap_id;	
	if(list_empty(&curr->mmap_list)){
		mmap_id = 1;
	}else{
		// last mmap_list[] + 1
		mmap_id = list_entry(list_back(&curr->mmap_list), struct mmap_desc, mmap_list_elem)->id + 1;
	}
	mmap_desc->id = mmap_id;
	mmap_desc->upage = upage;
	mmap_desc->file_size = file_size;
	list_push_back (&curr->mmap_list, &mmap_desc->mmap_list_elem);
	
	lock_release (&file_lock);

	return mmap_id;
}


/**
 * 
 * 1. update supt NOT to lazy load 
 * 		- delete supt entry (filesystem)
 * 2. delete from thread->mmap_list[] 
 */
void
system_call_munmap(int mmapid){
	
	lock_acquire (&file_lock);
	
	struct thread *curr = thread_current();
	struct mmap_desc *mmap_desc = get_mmap_desc(mmapid);	
    uint32_t offset;
	uint32_t file_size = mmap_desc->file_size;

    for(offset = 0; offset < file_size; offset += PGSIZE) {
		void *starting_addr = mmap_desc->upage + offset; // upage
		size_t bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
		
		bool success = vm_supt_unload_kpage(curr->supt, curr->pagedir,
			starting_addr, mmap_desc->dup_file, offset, bytes);
		
		if(!success){
			PANIC("system_call_munmap() vm_supt_unload_kpage() failed");
		}
	}

	list_remove(&mmap_desc->mmap_list_elem);
    file_close(mmap_desc->dup_file);
    free(mmap_desc);

	lock_release (&file_lock);
}


// given mmapid, for-loop mmap_list[] for mmap_desc{}
struct mmap_desc *
get_mmap_desc(int mmapid)
{
	struct mmap_desc *mmap_desc;
	struct list_elem *e;
	struct thread *t;

	t = thread_current();
	for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e))
	{
		mmap_desc = list_entry(e, struct mmap_desc, mmap_list_elem);
		if (mmap_desc->id == mmapid)
			return mmap_desc;
	}
	return NULL;
}
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
struct lock file_lock; // multi-threads access same file

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}



/***************************************************************/
// kernel side redirect to syscall implementation
// e.g. parse() intr_frame->esp, then syscall_exec(), then process_execute()












/***************************************************************/

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // TODO 

	int *syscall_number = f->esp;
	int *argument = f->esp;
	int ret_val = 0;

	if (is_user_vaddr(syscall_number))
	{
		switch (*syscall_number)
		{
		
		// General
		case SYS_HALT:
			system_call_halt();
			break;
		case SYS_EXIT:
			if (is_user_vaddr(argument + 1))
				system_call_exit(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		
		// PS
		case SYS_EXEC:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_exec((const char *) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_WAIT:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_wait((pid_t) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		
		// FS
		case SYS_CREATE:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2))
				ret_val = system_call_create((const char *) *(argument + 1),
						(unsigned) *(argument + 2));
			else
				system_call_exit(-1);
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
		case SYS_WRITE:	//Write System call
			//printf("System call to write");
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)
					&& is_user_vaddr(argument + 3))
				ret_val = system_call_write(*(argument + 1),
						(const void *) *(argument + 2),
						(unsigned) *(argument + 3));
			else
				system_call_exit(-1);
			break;
		case SYS_SEEK:
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
}




// TODO
// when lock_acquire() ??

/***************************************************************/
// PS kernel side syscalls implementation













/***************************************************************/

void system_call_halt(void)
{
	shutdown_power_off();
}




void system_call_exit(int status)
{
	struct thread *t;
	struct list_elem *e;

	t = thread_current();
	if (lock_held_by_current_thread(&file_lock))
		lock_release(&file_lock);

	while (!list_empty(&t->fd_list))
	{
		e = list_begin(&t->fd_list);
		system_call_close(
		list_entry (e, struct file_desc, thread_file_elem)->fid);
	}

	t->return_status = status;

	//print this when the process exits
	printf("%s: exit(%d)\n", t->name, t->return_status);

	thread_exit();
}

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
	return process_wait(pid);
}


/***************************************************************/ 
// FS kernel side syscalls implementation
// mostly on thread->fd_list[]











/***************************************************************/


// creates an empty file or sub_dir without opening it
bool system_call_create(const char *file, unsigned initial_size)
{
	if (file != NULL)
	{
		lock_acquire(&file_lock);
		bool success = filesys_create(file, initial_size);
		lock_release(&file_lock);
		return success;
	}
	else
		system_call_exit(-1);
	return false;
}


bool system_call_remove(const char *file)
{
	if (file != NULL)
	{
		lock_acquire(&file_lock);
		bool success = filesys_remove(file);
		lock_release(&file_lock);
		return success;
	}
	else
		system_call_exit(-1);
	return false;
}



// append() "opened file" in thread->fd_list[]
int system_call_open(char *file_name)
{
	if (file_name != NULL)
	{
		// 1. malloc() file{}
		lock_acquire(&file_lock); //multi-threads open() same file
		struct file *file = filesys_open(file_name);
		lock_release(&file_lock);
		if (file == NULL)
			return -1;

		// 2. malloc() file_desc{}
		struct file_desc *file_desc = (struct file_desc *) malloc(
				sizeof(struct file_desc));
		if (file_desc == NULL)
		{
			file_close(file);
			return -1;
		}

		// 3. append() file{} to file_desc{}
		lock_acquire(&file_lock);		
		file_desc->f = file;
		thread_current()->total_fd +=1; 
		file_desc->id = thread_current()->total_fd;
		
		// 4. append() file_desc{} to fd_list[]
		list_push_back(&thread_current()->fd_list,
				&file_desc->fd_list_elem);
		lock_release(&file_lock);
		return file_desc->id;
	}
	else
	{
		system_call_exit(-1);
	}
	return -1;
}




// check fd, buffer + for-loop() file_desc + file_read()
int system_call_read(int fd, void *buffer, unsigned size)
{
	// 1. check buffer
	if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size))
		system_call_exit(-1);

	// 1. check fd
	switch (fd){
	case STDIN_FILENO:
		unsigned int offset = 0;
		for (offset = 0; offset < size; ++offset)
			*(uint8_t *) (buffer + offset) = input_getc();
		return size;
	case STDOUT_FILENO:
		return -1;
	
	default:
		// 2. for-loop() file_desc
		lock_acquire(&file_lock);
		struct file_desc *file_desc = get_file_desc(fd);
		lock_release(&file_lock);
		if (file_desc == NULL || file_desc->d != NULL)
			return -1;
		
		// 3. file_write()
		lock_acquire(&file_lock);
		int bytes_read = -1;
		bytes_read = file_read(file_desc->f, buffer, size);
		lock_release(&file_lock);
		return bytes_read;
	}
}




// check fd, buffer + for-loop() file_desc + file_write()
int system_call_write(int fd, const void *buffer, unsigned size)
{
	// 1. check buffer
	if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size))
		system_call_exit(-1); // prevent user w() kernel memory to disk

	// 1. check fd
	switch (fd){
	case STDIN_FILENO: // 0
		return -1;
	case STDOUT_FILENO: // 1
		putbuf(buffer, size); // w() to stdout
		return size;
	
	default: // normal fd
		// 2. for-loop() file_desc
		lock_acquire(&file_lock); // ?? read-only necessary
		struct file_desc *file_desc = get_file_desc(fd);
		lock_release(&file_lock);
		if (file_desc == NULL || file_desc->d != NULL){
			return -1;
		}
		
		// 3. file_write()
		lock_acquire(&file_lock);
		int bytes_written = -1;
		bytes_written = file_write(file_desc->f, buffer, size);
		lock_release(&file_lock);
		return bytes_written;
	}
}



void system_call_seek(int fd, unsigned position)
{
	
	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);
	lock_release(&file_lock);

	if (file_desc == NULL || file_desc->d != NULL)
		system_call_exit(-1);
	lock_acquire(&file_lock);
	file_seek(file_desc->f, position);
	lock_release(&file_lock);
}



// when seek pointer is
unsigned system_call_tell(int fd)
{
	unsigned position = 0;

	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);
	lock_release(&file_lock);

	if (file_desc == NULL || file_desc->d != NULL)
		return -1;

	lock_acquire(&file_lock);
	position = file_tell(file_desc->f);
	lock_release(&file_lock);

	return position;
}




void system_call_close(int fd)
{
	// 1. for-loop thread->fd_list[] for file_desc{}
	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);
	lock_release(&file_lock);
	if (file_desc == NULL)
		system_call_exit(-1);

	// 2. remove() file_desc{} from thread->fd_list[]
	lock_acquire(&file_lock);
	list_remove(&file_desc->fd_list_elem);
	
	// 3. free() file/dir
	if(file_desc->d != NULL){
		dir_close(file_desc->d);
	}else{
		file_close(file_desc->f);
	}
	free(file_desc);
	lock_release(&file_lock);
}




int system_call_filesize(int fd)
{
	// 1. for-loop thread->fd_list[] for file_desc{}
	lock_acquire(&file_lock);
	struct file_desc *file_desc = get_file_desc(fd);
	lock_release(&file_lock);
	if (file_desc == NULL || file_desc->d != NULL)
		return -1;

	// 2. file_length()
	lock_acquire(&file_lock);
	int size = file_length(file_desc->f);
	lock_release(&file_lock);

	return size;
}




/***************************************************************/
// helper












/***************************************************************/





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

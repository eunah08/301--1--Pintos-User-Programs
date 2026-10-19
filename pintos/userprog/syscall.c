#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// 추가 헤더
#include "lib/user/syscall.h"
#include "threads/init.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void sys_exit(int status);
int sys_write(int fd, const void *buffer, unsigned size);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// SW: 레지스터 값을 읽어서 가져오고
	uint64_t rax = f->R.rax;
	uint64_t rdi = f->R.rdi;
	uint64_t rsi = f->R.rsi;
	uint64_t rdx = f->R.rdx;
	uint64_t r10 = f->R.r10;
	uint64_t r8 = f->R.r8;
	uint64_t r9 = f->R.r9;

	// SW: rax 레지스터가 시스템 콜 번호이므로 여기서 분기
	switch(f->R.rax) {
		case SYS_WRITE:
		int w_size;
		w_size = sys_write((int)rdi, (const void*)rsi, rdx);
		f->R.rax = (uint64_t)w_size;
		break;

		case SYS_HALT:
		power_off();
		break;

		case SYS_EXIT:
		sys_exit((int)rdi);
		break;

		default:
		break;
	}
}

// SW: (정상)종료 시스템 콜 wrapper 함수
void sys_exit(int status) {
	struct thread *cur = thread_current();
	cur->tf.R.rax = status;

	thread_exit();
}

// SW: 쓰기 시스템 콜 wrapper 함수
int sys_write(int fd, const void *buffer, unsigned size) {
	if (fd == STDOUT_FILENO) {
		putbuf((const char*)buffer, size);
		return size;
	}
}

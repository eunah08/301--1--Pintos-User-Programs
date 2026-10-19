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
#include "threads/vaddr.h" //EN
#include "threads/mmu.h" //EN

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void sys_exit(int status);
int sys_write(int fd, const void *buffer, unsigned size);
void check_address(const void *addr); //EN

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
		check_address((const void *)rsi); //EN
		int w_size;
		w_size = sys_write((int)rdi, (const void*)rsi, rdx);
		f->R.rax = (uint64_t)w_size;
		break;

		case SYS_HALT:
		power_off();
		break;

		case SYS_EXIT:
		sys_exit(rdi);
		break;

		case SYS_CREATE: // 파일 생성 시스템 콜
			check_address((const void *)rdi);
			break;

		case SYS_OPEN: // 파일 열기 시스템 콜
			check_address((const void *)rdi);
			break;

		case SYS_EXEC: // 새 유저 프로그램 실행 시스템 콜
			check_address((const void *)rdi);
			break;

		case SYS_READ: // 파일 또는 입력 장치에서 데이터 읽기
			check_address((const void *)rsi);
			break;

		default:
		break;
	}
}

// SW: (정상)종료 시스템 콜 wrapper 함수
// TODO: 종료 메시지를 출력해야 하는 상황
void sys_exit(int status) { 									// 프로세스를 종료하는 시스템 콜 함수이며 종료 상태를 전달받음
    printf("%s: exit(%d)\n", thread_current()->name, status); 	// 현재 스레드의 이름과 종료 상태를 출력
    thread_exit(); 												// 현재 스레드를 종료하고 스케줄러가 다른 스레드를 실행할 수 있도록 함
}

// EN: 유저가 넘긴 주소가 NULL, 커널 영역, 할당 안 된 곳이면 프로세스 종료 //EN
void check_address(const void *addr) { 							// 전달받은 주소가 유효한 유저 메모리 주소인지 검사하는 함수
    if (addr == NULL                  							// 주소가 NULL인지 확인
        || !is_user_vaddr(addr)       							// 유저 영역의 가상 주소가 아닌지 확인
        || pml4_get_page(thread_current()->pml4, addr) == NULL) // 현재 스레드의 페이지 테이블에 해당 주소의 매핑이 존재하는지 확인
        sys_exit(-1);                 							// 조건 중 하나라도 참이면 프로세스를 종료하고 -1을 반환
}

// SW: 쓰기 시스템 콜 wrapper 함수
int sys_write(int fd, const void *buffer, unsigned size) {
	if (fd == STDOUT_FILENO) {
		putbuf((const char*)buffer, size);
		return size;
	}
}

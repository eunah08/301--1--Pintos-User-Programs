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
int sys_read(int fd, const void *buffer, unsigned size);
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

	// 4번 동작 시작: syscall_handler 내부의 시스템 콜 번호 분기 처리
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
			f->R.rax = (uint64_t)sys_read((int)rdi, (void *)rsi, rdx); // rdi(fd), rsi(buffer), rdx(size) 전달 후 결과 rax에 저장
			break;

		default:
		break;
	}
}
// 4번 동작 끝

// SW: (정상)종료 시스템 콜 wrapper 함수
// TODO: 종료 메시지를 출력해야 하는 상황
void sys_exit(int status) { 									// 프로세스를 종료하는 시스템 콜 함수이며 종료 상태를 전달받음
    printf("%s: exit(%d)\n", thread_current()->name, status); 	// 현재 스레드의 이름과 종료 상태를 출력
    thread_exit(); 												// 현재 스레드를 종료하고 스케줄러가 다른 스레드를 실행할 수 있도록 함
}

// 1번 동작 시작: 유저 가상 주소 유효성 사전 검증 함수 (대리인 문제 및 커널 패닉 방지)
void check_address(const void *addr) {
    if ((addr == NULL)                                          // 1. 포인터가 0번지(NULL)인지 확인
        || (is_user_vaddr(addr) == false)                       // 2. 커널 가상 메모리 영역(KERN_BASE 이상)을 침범했는지 확인
        || (pml4_get_page(thread_current()->pml4, addr) == NULL)) // 3. 유저 영역이라도 실제 물리 메모리(RAM)와 연결된 페이지인지 확인
    {
        sys_exit(-1); // 위 세 가지 중 하나라도 해당되면 즉시 유저 프로세스를 -1로 강제 종료 (커널 보호)
    }
}
// 1번 동작 끝


// 2번 동작 시작: sys_write 시스템 콜 구현 (버퍼 유효성 검사 및 콘솔 출력)
int sys_write(int fd, const void *buffer, unsigned size) {
    check_address(buffer); // 커널이 버퍼를 역참조(putbuf)하기 전에 유저가 넘긴 포인터의 유효성 검사

    if (fd == STDOUT_FILENO) {                 // 표준 출력(콘솔 화면, fd = 1) 요청인 경우
        putbuf((const char *)buffer, size);    // 커널 내부 출력 함수를 호출해 화면에 버퍼 내용 출력
        return size;                           // 정상 출력한 바이트 수 반환
    }
    return -1; // 그 외 파일 쓰기는 아직 미구현 상태이므로 임시 -1 반환
}
// 2번 동작 끝



// 3번 동작 시작: sys_read 시스템 콜 스켈레톤 구현 (버퍼 유효성 검사)
int sys_read(int fd, void *buffer, unsigned size) {
    check_address(buffer); // 커널이 데이터를 채워넣기 전 유저 버퍼 포인터의 유효성 검사 (bad-read2 차단)

    /* 추후 키보드 입력(fd 0) 및 파일 읽기 로직 추가 예정 */
    return -1; // 현재는 미구현 상태이므로 임시 -1 반환
}
// 3번 동작 끝
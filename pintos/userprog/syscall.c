#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// 추가 헤더 (유저용 헤더인 lib/user/syscall.h는 제거)
#include "threads/init.h"
#include "threads/vaddr.h" //EN
#include "threads/mmu.h" //EN
#include "filesys/filesys.h" // BD
#include "filesys/file.h" // BD
#include "threads/malloc.h"

// 스레드 구조체 안에 들어갈 fd 테이블 요소
struct fd_entry {
	int fd;
	struct file *f;
	struct list_elem elem;
};

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// 함수 프로토타입 (하단 구현부와 타입 완전 일치)
void check_address(const void *addr);
void sys_exit(int status);
int sys_write(int fd, const void *buffer, unsigned size);
int sys_read(int fd, void *buffer, unsigned size);
bool sys_create(const char *file, unsigned initial_size);
int sys_open(const char *file);
int sys_filesize(int fd);
unsigned sys_tell(int fd);
void sys_close(int fd);

struct list_elem *find_fd_with_num(int fd);

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
	switch (f->R.rax) {
		case SYS_HALT:
			power_off();
			break;

		case SYS_EXIT:
			sys_exit((int)rdi);
			break;

		case SYS_WRITE:
			f->R.rax = (uint64_t)sys_write((int)rdi, (const void *)rsi, (unsigned)rdx);
			break;

		case SYS_READ:
			f->R.rax = (uint64_t)sys_read((int)rdi, (void *)rsi, (unsigned)rdx);
			break;

		// 아직 구현되지 않은 시스템 콜은 주소 검사만 먼저 수행
		case SYS_CREATE:
			check_address((const void *)rdi);
			f->R.rax = (uint64_t)sys_create((const char *)rdi, (unsigned)rsi);
			break;

		case SYS_OPEN:
			check_address((const void *)rdi);
			f->R.rax = (uint64_t)sys_open((const char *)rdi);
			break;

		case SYS_FILESIZE:
			f->R.rax = (uint64_t)sys_filesize((int)rdi);
			break;

		case SYS_EXEC:
			check_address((const void *)rdi);
			break;

		case SYS_TELL:
			f->R.rax = (uint64_t)sys_tell((int)rdi);
			break;
		
		case SYS_CLOSE:
			sys_close(rdi);
			break;

		default:
			break;
	}
}
// 4번 동작 끝

// SW: (정상)종료 시스템 콜 wrapper 함수
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

	// 진짜 파일이 열렸을 경우
	else if (fd >= 2) {
		// 현재 스레드에서 해당하는 fd에 대한 엔트리 리스트 요소를 찾음
		struct list_elem *res = find_fd_with_num(fd);
		// 못 찾으면 -1
		if (res == NULL) {
			return -1;
		}

		// 리스트 요소로부터 엔트리를 가져오고, 파일 구조체를 찾음
		struct fd_entry *entry = list_entry(res, struct fd_entry, elem);
		struct file *entry_file = entry->f;

		// 파일의 처음부터 쓰므로 0번부터 쓰도록 함
		unsigned int write_bytes = file_write_at(entry_file, buffer, size, 0);

		return write_bytes;
	}
}
// 2번 동작 끝



// 3번 동작 시작: sys_read 시스템 콜 스켈레톤 구현 (버퍼 유효성 검사)
int sys_read(int fd, void *buffer, unsigned size) {
	check_address(buffer); // 커널이 데이터를 채워넣기 전 유저 버퍼 포인터의 유효성 검사 (bad-read2 차단)
	
    /* 추후 키보드 입력(fd 0) 및 파일 읽기 로직 추가 예정 */
	if (fd == STDIN_FILENO) {
		return -1; // 현재는 미구현 상태이므로 임시 -1 반환
	}
	
	// 진짜 파일이 열렸을 경우
    else if (fd >= 2) {
		// 현재 스레드에서 해당하는 fd에 대한 엔트리 리스트 요소를 찾음
		struct list_elem *res = find_fd_with_num(fd);
		// 못 찾으면 -1
		if (res == NULL) {
			return -1;
		}
		
		// 리스트 요소로부터 엔트리를 가져오고, 파일 구조체를 찾음
		struct fd_entry *entry = list_entry(res, struct fd_entry, elem);
		struct file *entry_file = entry->f;
		
		// 파일의 처음부터 읽으므로 0번부터 읽도록 함
		unsigned int read_bytes = file_read_at(entry_file, buffer, size, 0);
		
		// sample.txt 파일의 크기는 373이므로, 여기서 373이 반환되면 성공
		return read_bytes;
	}
}
// 3번 동작 끝

bool sys_create(const char *file, unsigned initial_size) {
	bool res = filesys_create(file, initial_size);

	return res;
}

// sys_open 시스템 콜 (파일 열기)
int sys_open(const char *file) {
	struct file *newfile = filesys_open(file); // inode 생성과 struct file 초기화까지 모두 해주는 함수
	// 파일 열기 실패 시 단순 -1 반환
	if (newfile == NULL) { // 파일 열기 실패 시
		return -1;
	}
	
	// thread 구조체 안의 fd_table에다가 열린 파일에 대한 fd 엔트리를 삽입
	struct thread *cur_thread = thread_current();
	int fd_cnt = cur_thread->next_fd_num++;
	
	// 엔트리를 위한 공간 하나 초기화, 실패하면 프로세스 종료
	struct fd_entry *entry = malloc(sizeof(*entry));
	if (entry == NULL) {
		file_close(newfile);
		sys_exit(-1);
	}
	
	// 엔트리의 fd 번호 지정, 함수 반환 시 증가 카운터가 1 상승
	entry->fd = fd_cnt;
	// 엔트리의 f를 지정
	entry->f = newfile;
	list_push_back(&cur_thread->fd_table, &entry->elem);
	
	return fd_cnt;
}

// sys_filesize 시스템 콜 (파일의 크기)
int sys_filesize(int fd) {
	struct list_elem *res = find_fd_with_num(fd);
	
	// 조회하려는 엔트리가 없으면 -1 반환
	if (res == NULL) {
		return -1;
	}
	
	struct fd_entry *cur_entry = list_entry(res, struct fd_entry, elem);
	struct file *cur_file = cur_entry->f;
	return file_length(cur_file);
}

// sys_tell 시스템 콜 (파일 디스크립터 내의 커서 위치)
unsigned sys_tell(int fd) {
	struct list_elem *res = find_fd_with_num(fd);
	
	// 조회하려는 엔트리가 없으면 -1 반환
	if (res == NULL) {
		return -1;
	}
	
	struct fd_entry *cur_entry = list_entry(res, struct fd_entry, elem);
	struct file *cur_file = cur_entry->f;
	return file_tell(cur_file);
}

// sys_close 시스템 콜 (파일 닫기)
void sys_close(int fd) {
	struct list_elem *res = find_fd_with_num(fd);

	// 닫으려는 파일 엔트리가 리스트에 없으면 함수는 조용히 종료
	if (res != NULL) {
		struct fd_entry *cur_entry = list_entry(res, struct fd_entry, elem);

		// 다음을 순차적으로 실행: 찾은 엔트리를 리스트에서 제거 -> 파일 구조체 반환 -> 엔트리 반환
		list_remove(res);
		free(cur_entry->f);
		free(cur_entry);
	}
}

// fd 번호로 fd 멤버를 찾을 헬퍼 함수, 없으면 NULL 반환
struct list_elem *find_fd_with_num(int fd) {
	struct thread *cur_thread = thread_current();
	struct list *fd_table = &(cur_thread->fd_table);
	struct list_elem *fd_elem = list_front(fd_table);

	while (fd_elem->next != NULL) {
		struct fd_entry *cur_entry = list_entry(fd_elem, struct fd_entry, elem);
		if (fd == cur_entry->fd) {
			return fd_elem;
		}
	}

	return NULL;
}

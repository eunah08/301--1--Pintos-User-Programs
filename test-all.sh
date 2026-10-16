#!/bin/bash
# userprog(프로젝트 2) 채점 대상 테스트 전체 실행 스크립트
#
# 사용법 (저장소 최상위 디렉터리에서):
#     chmod +x pintos/utils/* test-all.sh
#   ./test-all.sh            # 전체 테스트 (병렬 실행)
#   JOBS=1 ./test-all.sh     # 순차 실행
#   ./test-all.sh fork       # 경로에 'fork'가 포함된 테스트만
#   ./test-all.sh filesys    # filesys/base 테스트만
#   EXTRA=1 ./test-all.sh    # dup2(선택 과제) 테스트도 포함
#                            # (pintos/userprog/Make.vars의 EXTRA2 주석 3줄을 먼저 풀어야 함)
#
# 출력 형식:
#   1. args-none : PASS
#   2. args-single : FAIL

ROOT_DIR="$( cd -P "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PINTOS_DIR="$ROOT_DIR/pintos"
USERPROG_DIR="$PINTOS_DIR/userprog"
BUILD_DIR="$USERPROG_DIR/build"
FILTER="$1"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# pintos 유틸리티를 PATH에 추가
source "$PINTOS_DIR/activate"

# tests/ 아래 경로 기준 (Grading.no-extra에 포함된 테스트)
TESTS=(
  # Argument Passing
  userprog/args-none userprog/args-single userprog/args-multiple
  userprog/args-many userprog/args-dbl-space

  # User Memory
  userprog/bad-read userprog/bad-write userprog/bad-jump
  userprog/bad-read2 userprog/bad-write2 userprog/bad-jump2
  userprog/create-bad-ptr userprog/open-bad-ptr userprog/read-bad-ptr
  userprog/write-bad-ptr userprog/exec-bad-ptr
  userprog/create-null userprog/open-null
  userprog/create-bound userprog/open-boundary userprog/read-boundary
  userprog/write-boundary userprog/fork-boundary userprog/exec-boundary

  # System Calls: 프로세스
  userprog/halt userprog/exit
  userprog/fork-once userprog/fork-multiple userprog/fork-recursive
  userprog/fork-read userprog/fork-close
  userprog/exec-once userprog/exec-arg userprog/exec-missing userprog/exec-read
  userprog/wait-simple userprog/wait-twice userprog/wait-killed userprog/wait-bad-pid
  userprog/multi-recurse userprog/multi-child-fd
  userprog/no-vm/multi-oom

  # System Calls: 파일
  userprog/create-normal userprog/create-empty userprog/create-long userprog/create-exists
  userprog/open-normal userprog/open-missing userprog/open-empty userprog/open-twice
  userprog/close-normal userprog/close-twice userprog/close-bad-fd
  userprog/read-normal userprog/read-zero userprog/read-stdout userprog/read-bad-fd
  userprog/write-normal userprog/write-zero userprog/write-stdin userprog/write-bad-fd

  # System Calls: 파일 시스템
  filesys/base/sm-create filesys/base/sm-full filesys/base/sm-random
  filesys/base/sm-seq-block filesys/base/sm-seq-random
  filesys/base/lg-create filesys/base/lg-full filesys/base/lg-random
  filesys/base/lg-seq-block filesys/base/lg-seq-random
  filesys/base/syn-read filesys/base/syn-write filesys/base/syn-remove

  # Denying Writes to Executables
  userprog/rox-simple userprog/rox-child userprog/rox-multichild

  # 1주차 회귀 확인 (alarm, priority)
  threads/alarm-single threads/alarm-multiple threads/alarm-simultaneous
  threads/alarm-priority threads/alarm-zero threads/alarm-negative
  threads/priority-change threads/priority-preempt threads/priority-fifo
  threads/priority-sema threads/priority-condvar
  threads/priority-donate-one threads/priority-donate-multiple
  threads/priority-donate-multiple2 threads/priority-donate-nest
  threads/priority-donate-chain threads/priority-donate-sema
  threads/priority-donate-lower
)

# Extend File Descriptor (선택 과제)
if [ -n "$EXTRA" ]; then
  TESTS+=(userprog/dup2/dup2-simple userprog/dup2/dup2-complex)
fi

if [ -n "$FILTER" ]; then
  FILTERED=()
  for t in "${TESTS[@]}"; do
    [[ "$t" == *"$FILTER"* ]] && FILTERED+=("$t")
  done
  TESTS=("${FILTERED[@]}")
fi

if [ ${#TESTS[@]} -eq 0 ]; then
  echo "실행할 테스트가 없습니다: '$FILTER'"
  exit 1
fi

# 1. 커널 빌드
echo "빌드 중..."
if ! make -C "$USERPROG_DIR" > "$ROOT_DIR/build.log" 2>&1; then
  echo "빌드 실패! build.log 확인:"
  tail -20 "$ROOT_DIR/build.log"
  exit 1
fi

cd "$BUILD_DIR" || exit 1

# 2. 이전 결과 삭제 후 테스트 실행
TARGETS=()
for t in "${TESTS[@]}"; do
  rm -f "tests/$t.output" "tests/$t.errors" "tests/$t.result"
  TARGETS+=("tests/$t.result")
done

echo "테스트 실행 중... (${#TESTS[@]}개, 병렬 $JOBS)"
make -j"$JOBS" -k "${TARGETS[@]}" > "$ROOT_DIR/test.log" 2>&1

# 3. 결과 출력
echo
PASS=0
FAIL=0
FAILED=()
i=1
for t in "${TESTS[@]}"; do
  name="$(basename "$t")"
  result_file="tests/$t.result"
  if [ -f "$result_file" ] && [ "$(head -n 1 "$result_file")" = "PASS" ]; then
    echo "$i. $name : PASS"
    PASS=$((PASS + 1))
  else
    echo "$i. $name : FAIL"
    FAIL=$((FAIL + 1))
    FAILED+=("$t")
  fi
  i=$((i + 1))
done

echo
echo "총 ${#TESTS[@]}개 중 PASS $PASS / FAIL $FAIL"
if [ $FAIL -gt 0 ]; then
  echo
  echo "실패한 테스트의 결과 파일:"
  for t in "${FAILED[@]}"; do
    echo "  pintos/userprog/build/tests/$t.result"
  done
  exit 1
fi

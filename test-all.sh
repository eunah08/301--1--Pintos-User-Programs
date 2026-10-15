#!/bin/bash
# threads 프로젝트 전체 테스트 실행 스크립트
#
# 사용법 (저장소 최상위 디렉터리에서):
#   ./test-all.sh            # 전체 테스트 (병렬 실행)
#   JOBS=1 ./test-all.sh     # 순차 실행
#   ./test-all.sh alarm      # 이름에 'alarm'이 포함된 테스트만
#
# 출력 형식:
#   1. alarm-single : PASS
#   2. alarm-multiple : FAIL

ROOT_DIR="$( cd -P "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PINTOS_DIR="$ROOT_DIR/pintos"
THREADS_DIR="$PINTOS_DIR/threads"
BUILD_DIR="$THREADS_DIR/build"
FILTER="$1"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# pintos 유틸리티를 PATH에 추가
source "$PINTOS_DIR/activate"

TESTS=(
  alarm-single alarm-multiple alarm-simultaneous alarm-priority
  alarm-zero alarm-negative
  priority-change priority-donate-one priority-donate-multiple
  priority-donate-multiple2 priority-donate-nest priority-donate-sema
  priority-donate-lower priority-fifo priority-preempt priority-sema
  priority-condvar priority-donate-chain
  mlfqs/mlfqs-load-1 mlfqs/mlfqs-load-60 mlfqs/mlfqs-load-avg
  mlfqs/mlfqs-recent-1 mlfqs/mlfqs-fair-2 mlfqs/mlfqs-fair-20
  mlfqs/mlfqs-nice-2 mlfqs/mlfqs-nice-10 mlfqs/mlfqs-block
)

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
if ! make -C "$THREADS_DIR" > "$ROOT_DIR/build.log" 2>&1; then
  echo "빌드 실패! build.log 확인:"
  tail -20 "$ROOT_DIR/build.log"
  exit 1
fi

cd "$BUILD_DIR" || exit 1

# 2. 이전 결과 삭제 후 테스트 실행
TARGETS=()
for t in "${TESTS[@]}"; do
  rm -f "tests/threads/$t.output" "tests/threads/$t.errors" "tests/threads/$t.result"
  TARGETS+=("tests/threads/$t.result")
done

echo "테스트 실행 중... (${#TESTS[@]}개, 병렬 $JOBS)"
make -j"$JOBS" -k "${TARGETS[@]}" > "$ROOT_DIR/test.log" 2>&1

# 3. 결과 출력
echo
PASS=0
FAIL=0
i=1
for t in "${TESTS[@]}"; do
  name="$(basename "$t")"
  result_file="tests/threads/$t.result"
  if [ -f "$result_file" ] && [ "$(head -n 1 "$result_file")" = "PASS" ]; then
    echo "$i. $name : PASS"
    PASS=$((PASS + 1))
  else
    echo "$i. $name : FAIL"
    FAIL=$((FAIL + 1))
  fi
  i=$((i + 1))
done

echo
echo "총 ${#TESTS[@]}개 중 PASS $PASS / FAIL $FAIL"
if [ $FAIL -gt 0 ]; then
  echo "실패 원인은 pintos/threads/build/tests/threads/<테스트명>.result 파일을 확인하세요."
  exit 1
fi

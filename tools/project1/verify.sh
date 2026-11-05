#!/usr/bin/env bash
# 用法：bash verify.sh 源码目录 构建目录 证据目录 debug|memory|thread|package|quality
set -euo pipefail
source_dir=$(realpath "$1")
build_dir=$(realpath -m "$2")
result_dir=$(realpath -m "$3")
mode="$4"
mkdir -p "$result_dir"
printf 'step\texit_code\n' > "$result_dir/exit-codes.tsv"
run_step() {
  local step="$1"
  shift
  local result=0
  "$@" > "$result_dir/$step.log" 2>&1 || result=$?
  printf '%s\t%s\n' "$step" "$result" >> "$result_dir/exit-codes.tsv"
  if (( result != 0 )); then
    tail -n 60 "$result_dir/$step.log"
    exit "$result"
  fi
}
cd "$source_dir"
p1_files=(
  src/include/container/hash/extendible_hash_table.h
  src/container/hash/extendible_hash_table.cpp
  src/include/buffer/lru_k_replacer.h
  src/buffer/lru_k_replacer.cpp
  src/include/buffer/buffer_pool_manager_instance.h
  src/buffer/buffer_pool_manager_instance.cpp
)
regression_files=(
  test/container/hash/extendible_hash_table_regression_test.cpp
  test/buffer/lru_k_replacer_regression_test.cpp
  test/buffer/buffer_pool_manager_regression_test.cpp
)
if [[ "$mode" == quality ]]; then
  run_step format "$P1_CLANG_FORMAT" --dry-run --Werror "${p1_files[@]}" "${regression_files[@]}"
  run_step lint python3 build_support/cpplint.py --verbose=2 --quiet --linelength=120 \
    --filter=-legal/copyright,-build/header_guard,-runtime/references "${p1_files[@]}" "${regression_files[@]}"
  run_step tidy python3 build_support/run_clang_tidy.py -clang-tidy-binary "$P1_CLANG_TIDY" \
    -p "$build_dir" -j 4 -extra-arg=-Wno-unknown-warning-option "${p1_files[@]}"
  exit 0
fi
case "$mode" in
  debug) sanitizer=address ;;
  memory|package) sanitizer=address,undefined ;;
  thread) sanitizer=thread ;;
  *) printf '未知验收模式：%s\n' "$mode" >&2; exit 2 ;;
esac
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export TSAN_OPTIONS=halt_on_error=1
run_step configure cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ "-DBUSTUB_SANITIZER=$sanitizer" \
  '-DCMAKE_CXX_FLAGS=-Wno-error=dangling-reference -Wno-error=format-truncation' \
  "-DCLANG_FORMAT_BIN=$P1_CLANG_FORMAT" "-DCLANG_TIDY_BIN=$P1_CLANG_TIDY"
targets=(extendible_hash_table_test lru_k_replacer_test buffer_pool_manager_instance_test)
if [[ "$mode" != package ]]; then
  targets+=(extendible_hash_table_regression_test lru_k_replacer_regression_test buffer_pool_manager_regression_test)
fi
run_step build cmake --build "$build_dir" --parallel 4 --target "${targets[@]}"
# 各测试进程使用自己的工作目录，隔离测试数据库和日志文件。
for target in "${targets[@]}"; do
  mkdir -p "$result_dir/work-$target"
  cd "$result_dir/work-$target"
  run_step "$target" timeout 300 "$build_dir/test/$target" \
    --gtest_color=no "--gtest_output=xml:$result_dir/$target.xml"
done
run_step results python3 "$P1_VERIFY_ROOT/check_results.py" "$result_dir" "$mode"

#!/usr/bin/env bash
#SBATCH --job-name=bustub-p1-check
#SBATCH --partition=CPU
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --time=00:30:00
# 参数：项目独立目录、待测提交标识、阶段列表（默认为 debug memory thread quality package）。
set -euo pipefail
module load cmake/3.28.1 gcc/13.1.0
p1_root=$(realpath "$1")
revision="$2"
shift 2
source_dir="$p1_root/source-$revision"
export P1_VERIFY_ROOT="$source_dir/tools/project1"
export P1_CLANG_FORMAT="$p1_root/tools-linux/clang-format/clang_format/data/bin/clang-format"
export P1_CLANG_TIDY="$p1_root/tools-linux/clang-tidy/clang_tidy/data/bin/clang-tidy"
run_dir="$p1_root/evidence/accept-$SLURM_JOB_ID"
mkdir -p "$run_dir"
printf '%s\n' "$revision" > "$run_dir/revision.txt"
hostname > "$run_dir/host.txt"
g++ --version > "$run_dir/compiler.txt"
cmake --version > "$run_dir/cmake.txt"
(cd "$source_dir" && sha256sum src/include/container/hash/extendible_hash_table.h \
  src/container/hash/extendible_hash_table.cpp src/include/buffer/lru_k_replacer.h \
  src/buffer/lru_k_replacer.cpp src/include/buffer/buffer_pool_manager_instance.h \
  src/buffer/buffer_pool_manager_instance.cpp) > "$run_dir/source-sha256.txt"
if (( $# == 0 )); then
  set -- debug quality memory thread package
fi
for phase in "$@"; do
  build_dir="$p1_root/build-$revision-$phase"
  phase_source="$source_dir"
  if [[ "$phase" == quality ]]; then
    build_dir="$p1_root/build-$revision-debug"
  elif [[ "$phase" == package ]]; then
    cmake --build "$p1_root/build-$revision-memory" --target submit-p1 > "$run_dir/package-create.log" 2>&1
    phase_source="$p1_root/package-$revision-$SLURM_JOB_ID"
    mkdir "$phase_source"
    tar -xf "$p1_root/uploads/baseline.tar" -C "$phase_source"
    python3 "$P1_VERIFY_ROOT/check_package.py" "$source_dir" "$phase_source" "$run_dir"
  fi
  bash "$P1_VERIFY_ROOT/verify.sh" "$phase_source" "$build_dir" "$run_dir/$phase" "$phase"
done

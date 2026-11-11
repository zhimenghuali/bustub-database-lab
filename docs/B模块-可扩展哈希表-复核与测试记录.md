# B 模块交付记录：可扩展哈希表（Bucket / Find / Remove）

本文档对应六人分工表中的 B 角色：负责 `Bucket` 类、`Find`、`Remove`，并承担本模块的并发/边界测试、代码审查与文档记录。基线为仓库 `main` 分支 `a51552e`。

## 1. 交付物

| 分工表要求 | 交付物 | 位置 |
| --- | --- | --- |
| `Bucket` 类 | 桶的构造、容量判定、局部深度、桶内插入/查找/删除 | `src/container/hash/extendible_hash_table.cpp` |
| `Find` | 表级 `Find` + 桶内 `Find`，未命中有明确契约 | 同上 |
| `Remove` | 表级 `Remove` + 桶内 `Remove`，不收缩不合并 | 同上 |
| 并发/边界测试 | 11 个用例，含结构不变式与并发竞争 | `test/container/hash/extendible_hash_table_find_remove_test.cpp` |
| 代码审查 | 逐函数检查表与发现项 | 本文第 3 节 |
| 文档/测试记录 | 本文 | `docs/` |

验收脚本同步接入：`tools/project1/verify.sh` 增加该测试目标，`tools/project1/check_results.py` 增加 11 个用例的计数校验。

## 2. 实现契约

- **`Bucket::Find`**：桶内是无序链表，线性扫描；命中才写回 `value`，未命中不修改输出参数。
- **`Bucket::Remove`**：命中首个匹配项后立即 `erase` 并返回，删除后不再使用该迭代器；未命中返回 `false`。
- **`Bucket::Insert`**：先扫描已有键做更新，再判断 `IsFull()`。顺序不能颠倒：满桶中更新已有键不能触发分裂，否则表级 `Insert` 会为一次纯更新做出不必要的分裂。
- **表级 `Find` / `Remove`**：持 `latch_` 后调用 `IndexOf(key)` 定位目录项，再委托桶操作。索引由 `(1 << global_depth_) - 1` 掩码得到，恒小于目录长度。
- **不收缩**：`Remove` 只摘除键值对，不合并空桶、不回缩目录。这是任务说明明确允许的行为，也保证目录索引在删除后仍然稳定。
- **线程安全**：所有公开操作共用一把表级 `std::mutex`，桶不再单独加锁，因此不存在嵌套加锁与锁序问题。

## 3. 代码审查

### 3.1 检查项

| # | 检查项 | 结论 | 依据 |
| --- | --- | --- | --- |
| 1 | 空表上 `Find` / `Remove` 不越界 | 通过 | 目录初始长度为 1，`global_depth_ = 0` 时掩码为 0；`EmptyTable` 用例 |
| 2 | 未命中不修改输出参数 | 通过 | `MissingKeyKeepsOutputUnchanged`、`RemoveEveryKeyInRandomOrder` |
| 3 | 重复删除返回 `false` 且无副作用 | 通过 | `RemoveIsIdempotent`、`ConcurrentRemoveSameKeysSucceedsOnce` |
| 4 | 满桶更新不触发分裂 | 通过 | `Bucket::Insert` 先更新后判满；既有 `FullBucketUpdateDoesNotSplit` 回归用例 |
| 5 | 删除后目录与桶数量不变 | 通过 | `RemoveDoesNotShrinkTable` |
| 6 | 删除不影响同桶其他键 | 通过 | `RemoveEveryKeyInRandomOrder`、`SingleSlotBucketChurn` |
| 7 | 目录索引与桶映射一致 | 通过 | `CheckDirectoryInvariant` 在 3 个用例中校验前缀分组与桶数量 |
| 8 | 锁覆盖 `IndexOf` 与桶访问的全部路径 | 通过 | `Find` / `Remove` 均在函数入口取 `scoped_lock` |
| 9 | 无嵌套加锁 | 通过 | 桶方法不取锁，只在表锁内被调用 |
| 10 | 迭代器安全 | 通过 | `Remove` 命中即返回，不复用已失效迭代器 |

### 3.2 审查发现

1. **[建议，非阻塞] `GetLocalDepth(int dir_index)` 缺少下标校验**（属 A 负责的函数）。
   当前实现直接 `dir_[dir_index]`，传入负数会被转换为极大的 `size_t`，越界即未定义行为。调试期建议加一层保护：

   ```cpp
   if (dir_index < 0 || static_cast<size_t>(dir_index) >= dir_.size()) {
     throw std::out_of_range("目录下标越界");
   }
   ```

   现有调用方（缓冲池页表、测试）只传有效索引，因此不影响本次验收；是否加入由 A 与本组统一决定。

2. **[说明] 读路径与写路径共用一把锁**。`Find` 会被正在分裂的 `Insert` 阻塞，吞吐受限但正确性最强。课程只要求线程安全，未要求并发性能；若后续要提升读并发，需要引入桶级锁并固定“目录锁 → 桶锁”的加锁顺序，风险与工作量都明显上升，本期不做。

3. **[说明] 删除后空桶保留**。`Remove` 不减少 `num_buckets_`，也不降低局部深度。这符合“不需要 shrink/combination”的任务描述；若后续有人实现收缩，需要同步修改 `GetNumBuckets` 相关测试。

4. **[整理] 头文件中仍有 `TODO(P1): Add implementation` 标记**，但对应函数均已实现。本次只清理 B 负责函数旁的注释，其余标记建议由 A 在合并前统一删除，避免评审误判为未完成。

5. **[确认] 构造函数对 `bucket_size == 0` 抛 `std::invalid_argument`**，属于超出任务要求的额外约定。缓冲池传入的池大小恒大于 0，官方测试也不构造 0 容量，因此保留；若评分脚本构造 0 容量需另行评估。

### 3.3 结论

B 负责的函数逻辑正确、锁覆盖完整，未发现阻塞性缺陷，可以提交。发现 1 为健壮性建议，转交 A 处理。

## 4. 测试设计

### 4.1 思路

三类用例互补：

1. **边界**：空表、未命中、重复删除、单槽桶、深目录、全量删除后重插。
2. **并发**：并发查找与分裂、同一批键竞争删除（成功次数必须恰好等于键数）、删除与重插交替时的值一致性。
3. **结构不变式**：只通过公开接口推导“目录前缀分组 ↔ 桶”的映射关系，校验分裂后 `Find` 依赖的索引仍然自洽。

结构不变式的三条断言：局部深度不超过全局深度；同一 `local_depth` 位前缀的目录项局部深度相同；每个桶恰好被 `2^(global_depth - local_depth)` 个目录项共享，且分组数等于 `GetNumBuckets()`。

### 4.2 用例清单

| 用例 | 类型 | 关键断言 |
| --- | --- | --- |
| `EmptyTable` | 边界 | 空表查找/删除均失败且不改输出；随后插入可用 |
| `MissingKeyKeepsOutputUnchanged` | 边界 | 多轮分裂后未命中键不修改 `value` |
| `RemoveIsIdempotent` | 边界 | 重复删除返回 `false`；删除后可重新插入 |
| `RemoveEveryKeyInRandomOrder` | 边界 | 固定种子乱序删除 200 键，每次删除后立即查找为空 |
| `RemoveDoesNotShrinkTable` | 边界 | 全删后全局深度、桶数量不变，结构不变式成立 |
| `SingleSlotBucketChurn` | 边界 | 容量 1 的桶插入 64 键、删一半、写回，值全部正确 |
| `FindReturnsLatestValue` | 边界 | 连续更新同一键，`Find` 始终返回最新值且不新增条目 |
| `DeepDirectoryWithManyKeys` | 边界 | 5000 键、容量 3，全量查找 + 隔一删一 + 结构不变式 |
| `ConcurrentFindDuringInsert` | 并发 | 4 写 2 读并发；读到的值必须与键一致，结束后全量可查 |
| `ConcurrentRemoveSameKeysSucceedsOnce` | 并发 | 8 线程 × 5 轮竞争删除同一批 256 键，每轮成功次数恰为 256 |
| `ConcurrentRemoveAndReinsert` | 并发 | 4 线程边删边插 10 轮 + 2 读者校验值，结束后全量可查 |

### 4.3 断言强度验证（变异测试）

为确认用例不是“空跑”，对实现注入两个典型缺陷后重跑：

| 变异体 | 注入的缺陷 | 结果 |
| --- | --- | --- |
| M1 | `Bucket::Find` 直接返回 `false` | 11 个用例中 8 个失败 |
| M2 | `Bucket::Remove` 找到键但不 `erase` | 11 个用例中 6 个失败 |

两个变异体均被稳定捕获，说明用例覆盖了查找与删除的核心路径。

## 5. 运行记录（2026-11-08）

环境：Windows 11 + MinGW-w64 GCC 15.2.0，C++17，`-O2 -Wall -Wextra -Werror`。本机无法使用仓库 CMake（缺少 CMake 与 Linux 工具链），因此直接用 `g++` 编译实现、gtest 与测试文件；正式验收仍需在 Linux/超算按第 6 节复跑。

| 检查 | 结果 |
| --- | --- |
| 官方 `extendible_hash_table_test` | 12/12 通过 |
| 新增 `extendible_hash_table_find_remove_test` | 11/11 通过 |
| 新增用例连续重复 30 次 | 30/30 次全部通过，0 次失败 |
| 编译告警 | `-Wall -Wextra -Werror` 下零告警 |
| clang-format 14.0.6 `--dry-run --Werror` | 通过 |
| cpplint（linelength=120，仓库同款过滤项） | 通过 |

合计：哈希表模块 23 个用例全部通过（官方 12 + 新增 11）。

## 6. 在 Linux / 超算复跑

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j4 extendible_hash_table_test extendible_hash_table_find_remove_test
./test/extendible_hash_table_test
./test/extendible_hash_table_find_remove_test
```

Debug 构建默认启用 AddressSanitizer；数据竞争检查需单独构建：

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DBUSTUB_SANITIZER=thread ..
make -j4 extendible_hash_table_find_remove_test
./test/extendible_hash_table_find_remove_test
```

接入既有验收流程后，`tools/project1/verify.sh` 会在 `debug`、`memory`、`thread` 三种模式构建并运行该用例，`check_results.py` 按 11 个用例核验 XML。

## 7. 对既有验收材料的影响

- 测试总数由 42 变为 53（原有 29 + 既有补充 13 + 本次 11）。
- `docs/project1_验收记录.md` 记录的是快照 `24557f2` 的历史结果，保持原样；如需把新用例计入验收证据，需在 Linux 上重新提交作业。
- 提交包不受影响：`make submit-p1` 仍只打包六个源码文件，测试与文档不进入 `project1-submission.zip`。

## 8. 遗留事项

1. 推送分支并开 PR（本机无 GitHub 凭据，需由组员执行）。
2. A 决定是否采纳 `GetLocalDepth` 下标校验建议。
3. 在 Linux 上补跑 ASan/TSan 与格式、lint、Tidy 完整流程。

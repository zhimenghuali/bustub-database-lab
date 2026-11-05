# P1 哈希表模块 · B 角色交付说明

> 分支：`hash-table`　｜　模块：Extendible Hash Table　｜　责任人：B（KOBE）
> 主责：`Bucket` 类、`Find`、`Remove`、并发/边界测试、代码审查　｜　兼任：文档、测试记录

---

## 1. 交付物清单

| 文件 | 类型 | 说明 |
| --- | --- | --- |
| `src/container/hash/extendible_hash_table.cpp` | 代码 | `Bucket::Find/Remove/Insert`、`ExtendibleHashTable::Find/Remove`、构造函数初始化目录（公共前置） |
| `test/container/hash/extendible_hash_table_boundary_test.cpp` | 测试 | 19 个边界/并发用例（Bucket 6 + 表级 9 + 并发 4） |
| `docs/p1/p1-hash-table-B.md` | 文档 | 本文：设计说明 + 边界语义 + 测试记录 |
| `docs/p1/p1-hash-table-review.md` | 文档 | 代码审查记录、问题清单与 review checklist |

> 未改动 `Insert` / `RedistributeBucket` / 目录翻倍相关逻辑，属于 A 的分工。

---

## 2. 实现说明

### 2.1 `Bucket`

存储沿用骨架给定的 `std::list<std::pair<K, V>> list_`：`size_` 是容量上限，`depth_` 是本地深度。

| 方法 | 语义 | 复杂度 |
| --- | --- | --- |
| `Find(key, value)` | 命中则写回 `value` 并返回 `true`；未命中**不修改** `value` 并返回 `false` | O(n) |
| `Remove(key)` | 命中则从链表摘除并返回 `true`；未命中返回 `false` | O(n) |
| `Insert(key, value)` | ① 已存在 → 原地更新 `value`，返回 `true`；② 已满 → 返回 `false`；③ 否则追加，返回 `true` | O(n) |

**判定顺序是关键**：必须"先查重、再判满"。若先判满，满桶上覆盖已有 key 会被误判为插入失败，上层 `Insert` 会因此多触发一次分裂（官方 `InsertAndReplace` 用例即针对该场景）。本模块用 `DuplicateKeyUpdatesInPlace`、`CapacityOne` 两个用例把这条语义钉死。

### 2.2 表级 `Find` / `Remove`

```cpp
std::scoped_lock<std::mutex> lock(latch_);
return dir_[IndexOf(key)]->Find(key, value);
```

- **必须在锁内完成"算索引 + 取桶 + 访问桶"三步**。`Insert` 分裂时会对 `dir_` 做翻倍并替换目录项，先算索引再解锁会让索引指向旧目录或已被替换的桶。
- `Remove` 只删除 kv，**不减少 `num_buckets_`、不缩小目录**：题目明确 "Shrink & Combination is not required"，若擅自 shrink，`GetNumBuckets()` 会与目录长度失配，官方 `GetNumBuckets` 用例会挂。
- 两处都复用 `IndexOf(key)`，与 `Insert` 走同一条哈希路径，保证 `Find/Remove` 与插入落点一致。

### 2.3 并发策略

- 整表一把 `std::mutex latch_`，`Insert` / `Find` / `Remove` 全部串行化。
- 正确性优先：目录翻倍、桶分裂、`num_buckets_` 计数都是跨桶的状态变更，分段锁需要额外的版本号/重试机制，收益不抵风险。
- 代价是读操作无并发度；若后续要做 fine-grained locking，需要把 `Bucket` 加锁并让 `Find` 在目录变更时重读索引（不在本次范围内，已记入审查文档的"可选优化"）。

### 2.4 构造函数（公共前置，单独一个 commit）

```cpp
dir_.push_back(std::make_shared<Bucket>(bucket_size, 0));
```

骨架的构造函数只赋了 `bucket_size_`，空表没有任何桶，`Find` / `Remove` / `IndexOf` 第一次访问 `dir_[0]` 就越界，官方 `InitiallyEmpty` 用例也无法通过。这行是 A、B 两个子任务的公共前置，**若 A 的实现里也包含同样的初始化，合并时保留任一即可**。

---

## 3. 边界语义约定

| 场景 | 约定行为 | 覆盖用例 |
| --- | --- | --- |
| 空桶 `Find` / `Remove` | 返回 `false`，不写 out-param | `EmptyBucket` |
| 空表 `Find` / `Remove` | 返回 `false`，全局深度 0、桶数 1 | `EmptyTableLookupAndRemove` |
| 未命中时的 out-param | 保持调用方原值 | `MissKeepsOutParam`、`EmptyBucket` |
| 重复插入同一 key | 更新值，不占新槽位 | `DuplicateKeyUpdatesInPlace` |
| 满桶覆盖已有 key | 返回 `true`（不触发分裂） | `DuplicateKeyUpdatesInPlace`、`CapacityOne` |
| 重复 `Remove` 同一 key | 第一次 `true`，之后 `false` | `DoubleRemove` |
| 容量 0 的桶 | 永远 `IsFull()`，`Insert` 返回 `false` | `ZeroCapacityBucketNeverAcceptsEntries` |
| 容量 1 的桶 | 每次插入都必然触发分裂 | `CapacityOne`、`BucketSizeOneDeepSplitChain` |
| 负 key / `INT_MIN` / `INT_MAX` | 与普通 key 行为一致 | `NegativeAndExtremeKeys` |
| 删除全部元素 | 目录与全局深度**不变**，表仍可继续插入 | `RemovingAllKeysKeepsMetadataStable`、`RemoveThenReinsert` |
| `Page *` 作为 value | 指针原样取回，不拷贝页面 | `PagePointerValues` |
| 同一 key 并发 `Remove` | 恰好一个线程返回 `true` | `ConcurrentRemoveSameKey` |

---

## 4. 测试记录

### 4.1 环境

| 项 | 值 |
| --- | --- |
| 本地验证环境 | Windows 10 + MinGW g++ 15.2.0，`-std=c++17 -O2 -pthread` |
| 测试框架 | 仓库自带 `third_party/googletest`（直接编译 `gtest-all.cc` + `gtest_main.cc`） |
| 说明 | 本机无 cmake / WSL / Docker，无法跑完整 `p1_test.sh`，因此用"官方测试文件 + 实现文件 + gtest"的最小可执行目标验证 |

编译与运行命令（在仓库根目录）：

```bash
GT=third_party/googletest/googletest
g++ -std=c++17 -O2 -pthread -I$GT -I$GT/include -c $GT/src/gtest-all.cc -o /tmp/gtest-all.o
g++ -std=c++17 -O2 -pthread -I$GT -I$GT/include -c $GT/src/gtest_main.cc  -o /tmp/gtest_main.o

# 官方用例
g++ -std=c++17 -O2 -pthread -Isrc/include -I$GT/include \
    test/container/hash/extendible_hash_table_test.cpp \
    src/container/hash/extendible_hash_table.cpp /tmp/gtest-all.o /tmp/gtest_main.o -o /tmp/official_test

# 本次新增用例
g++ -std=c++17 -O2 -pthread -Isrc/include -I$GT/include \
    test/container/hash/extendible_hash_table_boundary_test.cpp \
    src/container/hash/extendible_hash_table.cpp /tmp/gtest-all.o /tmp/gtest_main.o -o /tmp/boundary_test
```

> 本地执行时 `Insert` 用的是**临时参考实现**（仅存在于本地验证目录，未提交），用于在 A 的分支合并前先把 `Find/Remove` 侧结论跑实。合并 A 的 `Insert` 后，按 `p1_test.sh` 在 Linux/Mac 上复跑即可。

### 4.2 结果

| 用例集 | 数量 | 结果 | 耗时 |
| --- | --- | --- | --- |
| 官方 `extendible_hash_table_test` | 12 | 全部 PASSED | 43 ms |
| 新增 `extendible_hash_table_boundary_test` | 19 | 全部 PASSED | 103 ms |
| 其中 `ExtendibleHashTableBucketTest` | 6 | PASSED | 0 ms |
| 其中 `ExtendibleHashTableBoundaryTest` | 9 | PASSED | 0 ms |
| 其中 `ExtendibleHashTableConcurrencyTest` | 4 | PASSED | 103 ms |

并发用例耗时明细：`ParallelReaders` 8 ms、`DisjointInsertRemove` 12 ms、`ConcurrentRemoveSameKey` 73 ms（100 轮 × 8 线程）、`ReaderWriterMixed` 25 ms（4 写 × 500 key + 4 读 × 50000 次查找）。

### 4.3 测试有效性（变异测试）

为确认用例不是"只会绿灯"，对实现注入了两个典型缺陷，观察用例是否能捕获：

| 变异体 | 注入的错误 | 捕获用例 | 结论 |
| --- | --- | --- | --- |
| M1 | `Bucket::Insert` 先判满再查重（满桶更新被误判失败） | `DuplicateKeyUpdatesInPlace`、`CapacityOne` | 已杀死 |
| M2 | `Bucket::Remove` 只返回是否存在，不真正摘除节点 | `DoubleRemove`、`RemoveAcrossSplitBuckets`、`RemoveThenReinsert`、`BucketSizeOneDeepSplitChain`、`NegativeAndExtremeKeys`、`PagePointerValues` | 已杀死 |

### 4.4 还没做的验证

| 项 | 原因 | 建议 |
| --- | --- | --- |
| ThreadSanitizer | MinGW 无 `libtsan`，TSan 在 Windows 上不可用 | 在 Linux/Mac 上执行 `cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" ..` 后运行 `boundary_test` |
| `make check-lint` / `check-clang-tidy-p1` | 本机无 clang 工具链 | 在组内 CI 或有 clang 的机器上补跑；代码按 `.clang-format`（Google, 120 列）手写 |
| ASAN / 内存泄漏 | Windows 上 g++ 不支持 `-fsanitize=address` 的完整运行时 | 同上，在 Linux 上补跑 |

---

## 5. 与 A 的接口约定（合并注意事项）

1. **构造函数**：`dir_` 的初始桶只能有一份实现，合并时以任意一方为准。
2. **`Insert` 未合并前**，本文件的表级用例（9 个）和并发用例（4 个）会是红的，这是预期状态；`BucketTest` 的 6 个用例不依赖 `Insert`，应始终为绿。
3. `num_buckets_`、`global_depth_`、`dir_` 的结构变更只允许出现在 `Insert` 的分裂路径里；`Remove` 侧不碰这三个状态。
4. `RedistributeBucket` 直接操作 `bucket->GetItems()`，**不要调用 `Bucket::Remove`**：`Remove` 语义是"对外删除一个 key"，用它做迁移会引入额外的线性查找，且容易在迭代中失效。
5. `Bucket::Insert` 的"先查重后判满"顺序不要为了"提前返回"而调换。

---

## 6. 提交记录（`hash-table` 分支）

| commit | 内容 |
| --- | --- |
| `fix(hash-table): construct the directory with an initial bucket` | 构造函数放入首个桶（公共前置） |
| `feat(hash-table): implement Bucket and table-level Find/Remove` | B 的主责实现 |
| `test(hash-table): add boundary and concurrency tests` | 19 个边界/并发用例 |
| `docs(hash-table): add B delivery report and review notes` | 本文与审查记录 |

# 哈希表模块 · 代码审查记录

> 审查人：B（兼"代码审查"职责）　｜　范围：`src/container/hash/extendible_hash_table.*`、`test/container/hash/*`
> 审查方式：逐行读骨架代码 + 对照官方用例反推语义 + 变异测试验证用例有效性

---

## 1. 审查结论

| 模块 | 责任人 | 状态 | 结论 |
| --- | --- | --- | --- |
| `Bucket` / `Find` / `Remove` | B | 已实现 | 通过，19 个新增用例 + 12 个官方用例全绿 |
| `Insert` / 分裂 / 目录翻倍 / `Get*` | A | 未提交 | **待审**，审查要点见第 4 节 |
| 构造函数目录初始化 | A、B 交叉 | 已补（B） | 合并时二选一 |

---

## 2. 问题清单

级别定义：**Blocker** 必须改；**Major** 建议改，影响正确性或可维护性；**Minor** 可改可不改；**Nit** 风格建议。

| # | 级别 | 位置 | 问题 | 建议 |
| --- | --- | --- | --- | --- |
| 1 | **Blocker** | `ExtendibleHashTable` 构造函数 | 只赋值 `bucket_size_`，未向 `dir_` 放入初始桶。空表时 `dir_[0]` 越界，`InitiallyEmpty` 用例直接崩 | 已修：构造函数 `dir_.push_back(std::make_shared<Bucket>(bucket_size, 0))` |
| 2 | **Major** | `Insert` 重试路径（A 的实现） | 若传入的 key 集合在哈希低位上完全一致（或 `std::hash<K>` 退化为常量），`while (!bucket->Insert(...))` 会不断分裂直到 `global_depth_` 逼近 31，进而在 `IndexOf` 的 `1 << global_depth_` 处触发有符号左移溢出 | 分裂循环加深度上限保护（如 `global_depth_ >= 31` 时 `UNREACHABLE`/抛异常），并在文档中写明前提："key 的哈希需在足够低位上分散" |
| 3 | **Minor** | `IndexOf` | `int mask = (1 << global_depth_) - 1;`：`global_depth_` 到 31 时 `1 << 31` 是 UB；返回值是 `size_t` 却由 `int` 参与与运算 | 改为 `const size_t mask = (size_t{1} << global_depth_) - 1;` |
| 4 | **Minor** | `GetLocalDepth(int dir_index)` | 没有边界检查，传入负值或越界索引是 UB，测试代码很容易踩到 | 加 `BUSTUB_ASSERT(dir_index >= 0 && static_cast<size_t>(dir_index) < dir_.size(), "dir_index out of range")` |
| 5 | **Minor** | `Bucket::IsFull()` | `list_.size() == size_` 用等值比较。若容量配置为 0 或某条路径让元素数超过 `size_`，"满"会判错 | 改为 `list_.size() >= size_`，语义更稳 |
| 6 | **Minor** | 头文件 TODO 注释 | `TODO(P1): Add implementation` 在实现完成后仍保留，提交给助教时容易被当作未完成 | 终稿合并时由集成负责人（F）统一清理 TODO 标记 |
| 7 | **Nit** | `RedistributeBucket(std::shared_ptr<Bucket> bucket)` | 按值传 `shared_ptr`，每次调用都做原子引用计数增减 | 改成 `const std::shared_ptr<Bucket> &` |
| 8 | **Nit** | 官方测试文件 | 存在 `ASSERT_EQ(false, table->Find(...))`、`ASSERT_EQ(true, table->Remove(...))` 这类写法 | 用 `ASSERT_FALSE` / `ASSERT_TRUE`，失败信息更可读（非必须，避免与官方文件冲突） |
| 9 | **可选** | 并发策略 | 整表一把 `std::mutex`，读操作完全串行 | 本项目正确性优先，维持现状；若要加分可做读写锁 + 桶级 latch，代价是目录变更时 `Find` 需重试 |

> 第 4、5 条会改动 A 也在维护的文件，**不在 B 的分支里改**，交由 A 或终稿合并时统一处理，避免冲突。

---

## 3. 自审：B 的实现

| 检查项 | 结论 |
| --- | --- |
| `Find` 未命中是否污染 out-param | 不会，命中才赋值（`MissKeepsOutParam` 覆盖） |
| 满桶覆盖已有 key 是否被误判为失败 | 不会，先查重再判满（变异体 M1 验证） |
| `Remove` 是否真正释放桶内槽位 | 是，`list_.erase`（变异体 M2 验证） |
| `Remove` 是否误改 `num_buckets_` / `global_depth_` | 不会，只删 kv |
| 锁的范围是否覆盖 `IndexOf` + 取桶 + 桶内操作 | 是，`scoped_lock` 覆盖整个函数体 |
| 是否存在锁内再次加锁（死锁） | 没有，`Find`/`Remove` 只拿 `latch_`，`Bucket` 内部无锁 |
| 异常安全 | 只用 `list::erase` / `emplace_back`，`emplace_back` 失败会抛 `bad_alloc`，锁由 RAII 释放 |

---

## 4. 待审：A 的 `Insert` / 分裂（合并时按此表逐条核对）

| # | 检查点 | 期望 |
| --- | --- | --- |
| 1 | 目录翻倍条件 | 仅当 `bucket->GetDepth() == global_depth_` 时翻倍；`local_depth < global_depth` 时只分裂 |
| 2 | 翻倍方式 | `dir_.resize(dir_.size() * 2)` 或把前半段逐项 `push_back`，语义等价（第 i 项与第 i + old_size 项指向同一个桶） |
| 3 | 分裂后深度 | 原桶与新桶的 `depth` 都等于 `原 depth + 1` |
| 4 | 目录重定向依据 | 用**新增加的那一位**（`hash & (1 << (new_depth - 1))`）判断，不能按目录下标模运算 |
| 5 | `num_buckets_` | 只在创建新桶时 +1，翻倍目录**不**改变桶数 |
| 6 | 重试循环 | `while (!dir_[IndexOf(key)]->Insert(key, value)) { ... }`，且每次循环重新 `IndexOf` |
| 7 | 迁移时的迭代器安全 | `iter = items.erase(iter)`，不能 `erase` 后再 `++iter` |
| 8 | 迁移是否用 `Bucket::Remove` | 不应使用；直接搬 `GetItems()` 中的 pair |
| 9 | 死循环保护 | 见问题 #2 |
| 10 | `GetNumBuckets` / `GetGlobalDepth` / `GetLocalDepth` | 三个 `Internal` 版本不加锁、公开版本加锁，不能互相调用造成重复加锁 |

---

## 5. 可复用的 review checklist（给 C/D/E/F 的 PR 用）

**通用**

- [ ] 没有修改 `test/` 下的官方用例来"迁就"实现
- [ ] 没有提交 `build/`、`.o`、IDE 配置、`cmake-build-*`
- [ ] commit message 有前缀（`feat/fix/test/docs`）且一个 commit 只做一件事
- [ ] 与 `main` 已同步，`git merge-tree` 无冲突
- [ ] `p1_test.sh` 里对应的测试目标能编过、能跑过
- [ ] `make check-lint`、`make check-clang-tidy-p1` 无新增告警
- [ ] 头文件里自己负责的 `TODO(P1)` 已清理

**LRU-K（C / D）**

- [ ] `Evict` 只在 `curr_size_ > 0` 时返回帧；被驱逐帧的 `is_evictable_` 置 false、从两个表中移除
- [ ] `SetEvictable(frame, false)` 对不可驱逐帧幂等；`Size()` 只统计 evictable 帧
- [ ] `RecordAccess` 满 K 次后只更新时间戳队列，不再增长历史
- [ ] K-距离计算用当前时间戳差，`inf` 的处理正确
- [ ] `Remove` 对不存在的 frame 返回 false，且不影响 `curr_size_`
- [ ] 并发：所有公开方法同一把 latch，内部方法不加锁

**Buffer Pool（E / F）**

- [ ] `NewPgImp` 优先复用空闲帧，`pin_count_` 置 1，返回前页面已 `ResetMemory`
- [ ] `FetchPgImp` 命中/未命中两条路径的 `pin_count_` 与 replacer 状态一致
- [ ] 脏页换出后 `is_dirty_ = false`；`FlushPgImp` 不清 pin
- [ ] `UnpinPgImp` 在 `pin_count_ == 0` 时不得下溢；`is_dirty` 只有传 `true` 才置位
- [ ] `DeletePgImp` 对已钉住页返回 false
- [ ] 页表 `page_table_` 与帧数组在换出时必须同步删除/更新
- [ ] 无空闲帧且全部被钉住时返回 `nullptr`，不能死循环

---

## 6. 审查记录

| 日期 | 对象 | 结果 |
| --- | --- | --- |
| 2026-11-05 | 骨架 + B 的实现 | 提出 9 条问题（1 Blocker 已修、1 Major 待 A 处理、其余 Minor/Nit） |
| 2026-11-05 | B 的测试用例 | 变异测试 2/2 被捕获，用例有效 |
| 待定 | A 的 `Insert` | 按第 4 节 10 条核对 |
| 待定 | C/D、E/F 分支 | 按第 5 节 checklist |

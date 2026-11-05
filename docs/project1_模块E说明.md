# Project 1 模块 E：Buffer Pool Manager 实现说明

## 一、范围

| 项 | 内容 |
| --- | --- |
| 负责函数 | `NewPgImp()`、`FetchPgImp()`、帧获取与驱逐、脏页写回 |
| 涉及文件 | `src/buffer/buffer_pool_manager_instance.cpp`<br>`src/include/buffer/buffer_pool_manager_instance.h` |
| 不在范围内 | `UnpinPgImp()`、`FlushPgImp()`、`FlushAllPgsImp()`、`DeletePgImp()`（模块 F） |
| 提交 | `4091448`，分支 `p1-buffer-pool-core`，PR #8 |

构造函数末尾那句 `throw NotImplementedException(...)` 一并删除。它会让 `BufferPoolManagerInstance` 无法构造，任何调用都会在第一行抛出，删除后才具备跑测试的前提。

## 二、实现

### 2.1 帧获取：`AcquireFrame(frame_id_t *frame_id)`

新增的私有辅助函数，由 `NewPgImp()` 与 `FetchPgImp()` 共用。调用者必须已持有 `latch_`，函数内部不再加锁。

```text
1. free_list_ 非空        → 取队首帧，pop，直接返回 true（该帧不承载任何页，无需驱逐）
2. 否则 replacer_->Evict() → 取回退 k-距离最大的可驱逐帧
3. 两者都失败              → 所有帧都被固定，返回 false
4. 受害者 is_dirty_        → disk_manager_->WritePage(page_id, data) 写回
5. page_table_->Remove(victim_id)
6. ResetMemory()，page_id_ = INVALID_PAGE_ID，pin_count_ = 0，is_dirty_ = false
```

两个细节：

- **脏页写回必须在覆写帧内存之前**。帧是复用的，`ResetMemory()` 一执行，未写回的修改就永久丢失。
- **evict 之后不要再调 `replacer_->Remove()`**。`Evict()` 成功时已经删掉了该帧的访问历史，重复删除会触发 `LRUKReplacer` 的异常分支。

### 2.2 `NewPgImp(page_id_t *page_id)`

关键在顺序：**先拿到帧，再调用 `AllocatePage()`**。

反过来写（先分配页码再取帧）会在取帧失败时白白消耗一个页码：`next_page_id_` 前移了，却没有任何页与之对应。测试里对 `page_id` 连续性的断言（`SampleTest` 期望第一个新建页的 id 为 0）会因此失败。

拿到帧后依次执行：`ResetMemory()` → `page_id_ = new_id` → `pin_count_ = 1` → `is_dirty_ = false` → `page_table_->Insert()` → `RecordAccess()` → `SetEvictable(false)`。

`SetEvictable(false)` 必须在 `RecordAccess()` 之后：先让 replacer 知道这个帧刚被访问，再禁止它被选中，否则在两步之间的窗口里该帧仍处于可驱逐状态。

### 2.3 `FetchPgImp(page_id_t page_id)`

- `page_id == INVALID_PAGE_ID` 直接返回 `nullptr`，避免把非法页码塞进哈希表。
- **命中**（`page_table_->Find()` 为真）：`pin_count_++`、`RecordAccess()`、`SetEvictable(false)`，不碰页内容。
- **未命中**：`AcquireFrame()` → `ResetMemory()` → `disk_manager_->ReadPage()` → 设置元数据 → `page_table_->Insert()` → `RecordAccess()` → `SetEvictable(false)`。
- 取不到帧时返回 `nullptr`，此时页表与 replacer 均未被改动。

## 三、验证

### 3.1 环境

Windows 10 + MinGW g++ 8.1，手工编译 buffer 层四个源文件与 `third_party/googletest`，未走 CMake 全量构建。

### 3.2 结果

依赖使用 #4（可扩展哈希表）与 #5（LRU-K）的**真实实现**，未使用任何替身：

| 测试 | 结果 |
| --- | --- |
| `extendible_hash_table_test` | 12 / 12 通过 |
| `lru_k_replacer_test` | 4 / 4 通过 |
| `buffer_pool_manager_instance_test` | 13 / 13 通过 |

`buffer_pool_manager_instance_test` 的 13 项包括 `SampleTest`、`BinaryDataTest`、`NewPage`、`UnpinPage`、`FetchPage`、`DeletePage`、`IsDirty`、`ConcurrencyTest`、`IntegratedTest`、`HardTest_1` ~ `HardTest_4`。

### 3.3 值得关注的三个用例

**`FetchPage` 中的 Clock 边界。** page5、page6、page7 被写入新内容后以 `is_dirty=false` 解固定，随后 page5 被驱逐。再次 Fetch page5 必须读到磁盘上的旧值 `"5"`，而不是内存里的 `"updatedpage5"`。这条通过，说明驱逐时的写回判据是 **dirty 位**，而不是「内容与磁盘不一致」这种无法廉价判定的条件。

**`HardTest_1`。** 1000 轮 × 每轮 10 页，每轮前 5 页以 `is_dirty=false` 解固定、后 5 页以 `is_dirty=true` 解固定，随后顺序 Fetch 10000 页：未标脏的页应读到旧内容，标脏的页应读到写入内容。这条覆盖了「不脏就不写回」的正确性。

**`ConcurrencyTest`。** 5 线程 × 50 轮并发 Fetch / Unpin，验证 `latch_` 对页表、空闲帧链表、replacer 三者的一致性保护。

## 四、Windows 平台下的三个障碍

以下问题与 BPM 逻辑无关，但会让人误以为实现有 bug，记录在此。

**1. `test/buffer/mock_buffer_pool_manager.h` 导致段错误。**
该文件中的限定名调用写成了 `BufferPoolManager::FetchPgImp(page_id)`，而 `BufferPoolManager` 里这些是**纯虚函数**。本机运行时直接崩溃。改为 `BufferPoolManagerInstance::` 后正常。从代码看这是平台无关的语义问题，但**未在 Linux 上验证**。

**2. MinGW 下 `stat()` 报不出已打开文件的大小。**
`std::fstream` 即使调用了 `flush()`，`stat()` 仍返回 0，直到 `close()` 才更新。`DiskManager::ReadPage` 用 `offset > GetFileSize()` 判断越界，于是除第 0 页外所有读取都被静默跳过，页内容保持全零——表现为「写进去的数据读不出来」。本地复现时改为直接 `seekg + read`、短读补零，语义等价（从未写过的页本来就该读出全零）。

**3. Windows 下 `remove("test.db")` 在文件仍被打开时失败。**
各测试用例结尾的 `remove()` 因文件句柄未释放而无效，数据库文件残留，下一个用例会读到上一个用例的数据。表现为「单个用例跑能过、连起来跑就挂」。需要逐个用例运行，且每次运行前手动删库。

## 五、与 PR #6 的关系

#6（2026-11-02）已实现 BPM 全部六个函数，与本实现功能重叠。两版独立编写，在同一套用例上结果一致（均为 13 / 13）。

组内二选一即可，不需要两个都合：

- 采用 #6：本分支直接保留或删除，均不影响交付；
- 采用本分支：需把 #6 中模块 F 的四个函数迁过来，并注意 #6 评论中提到的 `FlushFrame` 自守问题。

两版都新增了同名的 `AcquireFrame(frame_id_t *)` 声明，**同时合入必然冲突**。

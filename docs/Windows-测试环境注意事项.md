# Windows 环境下的测试障碍记录

2026-11 在 Windows 10 + MinGW g++ 8.1 上跑 Project 1 的 buffer 层测试时，遇到三个与实现逻辑无关、但会让人误判为「缓冲池写错了」的问题。记录在此，供同样在 Windows 上复现的同学参考。

团队在 nscc Linux（GCC 13.1）上的验收结果为 42/42 通过（见 `project1_验收记录.md`），以下问题不影响 Linux 上的构建与测试。

## 一、MinGW 下 `std::fstream` 已 flush 仍报文件大小为 0

`std::fstream` 调用 `flush()` 之后，`stat()` 仍返回 0，直到 `close()` 才更新。

`DiskManager::ReadPage` 用 `offset > GetFileSize(file_name_)` 判断越界，于是**除第 0 页外所有读取都被跳过**，目标缓冲区保持调用前的状态（`ResetMemory()` 之后即全零）。

表现：把数据写进页面、驱逐、再 Fetch 回来是空的，看起来像脏页写回没生效。实际是读根本没有发生。

本地绕过方式（仅验证用，不提交）：改为直接 `seekg + read`，短读补零。语义与「从未写过的页读出全零」一致。

## 二、Windows 下 `remove("test.db")` 在文件仍被打开时失败

各测试用例结尾都有 `remove("test.db")`，但此时 `DiskManager` 仍持有文件句柄。Windows 不允许删除已打开的文件，`remove()` 静默失败。下一个用例打开同名文件时，会读到上一个用例残留的数据。

表现：单个用例单独跑能过，整套连着跑就挂。例如 `HardTest_1` 期望「未标脏的页读回旧内容」，却读到了上一个用例写进去的值。

处理：逐个用例运行，每次运行前手动删除 `test.db`。

## 三、`mock_buffer_pool_manager.h` 的纯虚限定名调用

该文件中的调用写成 `BufferPoolManager::FetchPgImp(page_id)` 等形式，而 `BufferPoolManager` 中这些是纯虚函数（`= 0`，无定义）。通过限定名调用纯虚函数属于未定义行为。

本机 MinGW g++ 8.1：编译链接通过，运行即段错误（SIGSEGV）。把这些限定名改为 `BufferPoolManagerInstance::` 后，13 个用例全部通过。

需要说明的是：同一份文件在 nscc Linux（GCC 13.1）上跑完了 42 个测试，未出现该问题。两个平台表现不同，原因未查明。因此这一条只作为 Windows 环境下的现象记录，**不代表仓库代码在 Linux 上存在缺陷**。

## 附：不依赖 CMake 的本地验证方式

只编译 buffer 层四个源文件，不构建整个项目：

```bash
g++ -std=c++17 -Isrc/include -Ithird_party/fmt/include -c src/buffer/buffer_pool_manager_instance.cpp
g++ -std=c++17 -Isrc/include -Ithird_party/fmt/include -c src/buffer/lru_k_replacer.cpp
g++ -std=c++17 -Isrc/include -Ithird_party/fmt/include -c src/container/hash/extendible_hash_table.cpp
g++ -std=c++17 -Isrc/include -Ithird_party/fmt/include -c src/storage/disk/disk_manager.cpp
```

测试文件需要额外加 `-Itest -Itest/buffer`。另外 MinGW 没有 `rand_r`，需要提供一个 32 位等价实现（如 `seed = seed * 1103515245 + 12345; return (seed >> 16) % 32768;`），否则 `buffer_pool_manager_instance_test.cpp` 编译不过。

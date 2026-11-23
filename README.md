# BusTub 数据库实验

本仓库用于数据库系统课程实验，基于 CMU BusTub 教学框架。当前重点是 **Project 1：缓冲池管理器**，使用 C++17 实现内存页表、LRU-K 页面替换和页面生命周期管理。

## 实验内容

| 模块 | 作用 | 主要文件 |
| --- | --- | --- |
| Extendible Hash Table | 维护 `page_id → frame_id` 映射，实现桶分裂和目录扩展 | `src/container/hash/extendible_hash_table.cpp` |
| LRU-K Replacer | 记录最近 K 次访问，在可淘汰帧中选择受害者 | `src/buffer/lru_k_replacer.cpp` |
| Buffer Pool Manager | 新建、读取、解除固定、刷新和删除页面；复用帧前写回脏页 | `src/buffer/buffer_pool_manager_instance.cpp` |

三个模块的头文件分别位于 `src/include/container/hash/` 和 `src/include/buffer/`。

## 当前进度

- P1 三个核心模块已在 `main` 完成。
- 主干现有 **53 个测试**：29 个官方测试、24 个回归测试。F 分支另有 9 个测试，本次作为额外验证，总计 62 项。
- 2026-11-23，PR #13 的合并曾遗漏 `NewPgImp`、`FetchPgImp` 和 `FlushFrame`；PR #14 已回退该合并并恢复主干实现。本次验收以更新后的 `f5dee14` 为准，见 [最新验收记录](docs/project1_验收记录_2026-11-23.md)。
- P2 B+Tree、P3 执行器仍有未实现部分，P1 测试通过不代表整套课程实验已完成。

## 开发环境

建议使用 Ubuntu 或 macOS。Windows 用户可使用 WSL Ubuntu；仓库中的 Windows 测试说明见 [环境注意事项](docs/Windows-测试环境注意事项.md)。

本机已验证的配置：

| 工具 | 版本 / 要求 |
| --- | --- |
| macOS | Apple Silicon / arm64 |
| C++ | C++17；Apple Clang 17 |
| CMake | 3.31.6 |
| clang-format | 14.0.6 |
| clang-tidy | 18.1.8；原配置存在兼容问题，见下文 |
| Python、Git | Python 3、Git |

`third_party/` 已包含项目依赖和 GoogleTest。此代码版本的第三方 CMake 配置较旧，建议沿用已验证的 CMake 3.31.6。

VS Code 或 CLion 请直接打开仓库根目录。VS Code 可使用 C/C++、CMake Tools 和 CodeLLDB 扩展；代码提示可读取构建生成的 `build/compile_commands.json`。

## 构建

```bash
git clone https://github.com/zhimenghuali/bustub-database-lab.git
cd bustub-database-lab

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

Debug 构建默认启用 AddressSanitizer。若要同时检查未定义行为：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUSTUB_SANITIZER=address,undefined
cmake --build build --parallel 4
```

仅修改现有源码后可以直接增量编译；新增测试文件后需要重新执行 CMake 配置。

## P1 测试

先编译主干的全部 P1 测试目标：

```bash
cmake --build build --parallel 4 --target \
  extendible_hash_table_test \
  lru_k_replacer_test \
  buffer_pool_manager_instance_test \
  extendible_hash_table_regression_test \
  extendible_hash_table_find_remove_test \
  lru_k_replacer_regression_test \
  buffer_pool_manager_regression_test
```

运行主干全部 53 个 P1 测试：

```bash
ctest --test-dir build \
  -R 'ExtendibleHashTable|LRUKReplacer|BufferPoolManager' \
  --output-on-failure
```

F 的 `buffer_pool_manager_instance_f_test.cpp` 保留在 `p1-bpm-f` 分支，当前主干不包含该测试文件。本次在独立验证目录中用它补测主干实现，未将回退的 F 源码重新覆盖主干。

不要用尚未实现的 P2/P3 测试来判断 P1 是否完成。`tools/project1/verify.sh` 是 Linux 远程验收脚本，现有计数检查覆盖主干的 53 个测试。

## 格式和检查

```bash
cmake --build build --target check-format check-lint
```

需要自动整理格式时：

```bash
cmake --build build --target format
```

当前 `.clang-tidy` 包含 `AnalyzeTemporaryDtors`，本机 clang-tidy 18 无法解析该旧选项。原始 `check-clang-tidy-p1` 即使返回 0，也必须检查输出是否存在配置错误；本仓库不将这种情况记为检查通过。

现有 GitHub Actions 工作流仅监听 `master`，仓库默认分支为 `main`。本次验收以实际构建、测试退出码和 XML 结果为准，未宣称 GitHub CI 已通过。

## 六人分工约定

每个模块由两人协作，按函数划分，合并前互相评审。

| 成员角色 | 负责内容 |
| --- | --- |
| A | 哈希表 `Insert`、桶分裂、目录翻倍、深度与桶数统计 |
| B | `Bucket`、`Find`、`Remove`、边界/并发测试及模块文档 |
| C | LRU-K 的访问历史、`RecordAccess`、`Evict` |
| D | `SetEvictable`、`Remove`、`Size` 及并发测试 |
| E | BPM 的 `NewPgImp`、`FetchPgImp`、帧获取、驱逐和脏页写回 |
| F | `UnpinPgImp`、`FlushPgImp`、`FlushAllPgsImp`、`DeletePgImp` 及补充测试 |

角色表是小组分工约定；具体成员和贡献以课程提交信息及 Git 历史为准。

## 打包和提交

```bash
cmake --build build --target submit-p1
```

提交包在仓库根目录 `project1-submission.zip`，只应包含以下六个文件：

```text
src/include/container/hash/extendible_hash_table.h
src/container/hash/extendible_hash_table.cpp
src/include/buffer/lru_k_replacer.h
src/buffer/lru_k_replacer.cpp
src/include/buffer/buffer_pool_manager_instance.h
src/buffer/buffer_pool_manager_instance.cpp
```

腾讯文档提交按课程要求填写姓名、学号、小组信息，粘贴代码并插入实际运行截图。代码附件使用上述六文件包，截图应对应同一份源码。上传文档、提交仓库链接和课程平台提交由小组按要求完成。

## 目录与资料

```text
src/                  实验实现和接口
test/                 官方测试与补充回归测试
third_party/          随仓库提供的依赖
build_support/        格式、lint、构建辅助工具
tools/project1/       远程验收和提交包检查工具
docs/                 实现说明、环境说明和验收记录
build/                本地构建产物，Git 忽略
```

- [P1 实现说明](docs/project1_实现说明.md)
- [B 模块复核与测试记录](docs/B模块-可扩展哈希表-复核与测试记录.md)
- [历史验收记录](docs/project1_验收记录.md)
- [2026-11-23 重新验收记录](docs/project1_验收记录_2026-11-23.md)
- [LICENSE](LICENSE)

BusTub 原始代码版权和许可证声明保留在源码及 LICENSE 中。

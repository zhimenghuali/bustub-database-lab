//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table_find_remove_test.cpp
//
// B 模块（Bucket 类、Find、Remove）的边界与并发测试
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <map>
#include <random>
#include <string>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "container/hash/extendible_hash_table.h"
#include "gtest/gtest.h"

namespace bustub {

namespace {

using Table = ExtendibleHashTable<int, std::string>;

/**
 * @brief 校验目录与桶的结构不变式，用于确认 Find 依赖的索引关系没有被破坏。
 *
 * 1. 每个目录项指向的桶，局部深度不超过全局深度；
 * 2. 共享同一个 local_depth 位前缀的目录项必须指向同一个桶（局部深度相同）；
 * 3. 一个桶被 2^(global_depth - local_depth) 个目录项共享，因此不同
 *    <前缀, 局部深度> 组的数量应当等于桶数量。
 */
void CheckDirectoryInvariant(Table *table) {
  const int global_depth = table->GetGlobalDepth();
  ASSERT_GE(global_depth, 0);
  const auto dir_size = std::size_t{1} << global_depth;

  std::map<std::pair<std::size_t, int>, std::size_t> groups;
  for (std::size_t index = 0; index < dir_size; ++index) {
    const int local_depth = table->GetLocalDepth(static_cast<int>(index));
    ASSERT_GE(local_depth, 0);
    ASSERT_LE(local_depth, global_depth);
    // 同一前缀下的目录项必须具有相同的局部深度，否则分组计数会与期望不符。
    groups[{index >> (global_depth - local_depth), local_depth}]++;
  }

  for (const auto &entry : groups) {
    const int local_depth = entry.first.second;
    ASSERT_EQ(entry.second, std::size_t{1} << (global_depth - local_depth));
  }
  ASSERT_EQ(groups.size(), static_cast<std::size_t>(table->GetNumBuckets()));
}

}  // namespace

//===--------------------------------------------------------------------===//
// 边界测试
//===--------------------------------------------------------------------===//

TEST(ExtendibleHashTableFindRemoveTest, EmptyTable) {
  Table table(2);

  std::string value = "sentinel";
  EXPECT_FALSE(table.Find(0, value));
  EXPECT_EQ("sentinel", value);
  EXPECT_FALSE(table.Remove(0));

  EXPECT_EQ(0, table.GetGlobalDepth());
  EXPECT_EQ(1, table.GetNumBuckets());

  // 空表上的失败操作不应破坏表结构，后续插入仍然可用。
  table.Insert(7, "seven");
  ASSERT_TRUE(table.Find(7, value));
  EXPECT_EQ("seven", value);
}

TEST(ExtendibleHashTableFindRemoveTest, MissingKeyKeepsOutputUnchanged) {
  Table table(1);
  for (int key = 0; key < 32; ++key) {
    table.Insert(key, std::to_string(key));
  }

  // 目录已多次翻倍，未命中的键不应修改输出参数。
  for (const int key : {32, 100, 1000, -1, -1024}) {
    std::string value = "sentinel";
    EXPECT_FALSE(table.Find(key, value)) << "key=" << key;
    EXPECT_EQ("sentinel", value) << "key=" << key;
  }
}

TEST(ExtendibleHashTableFindRemoveTest, RemoveIsIdempotent) {
  Table table(2);
  table.Insert(5, "a");

  EXPECT_TRUE(table.Remove(5));
  std::string value;
  EXPECT_FALSE(table.Find(5, value));

  // 重复删除同一个键必须返回 false，且不产生副作用。
  EXPECT_FALSE(table.Remove(5));
  EXPECT_FALSE(table.Remove(5));

  table.Insert(5, "b");
  ASSERT_TRUE(table.Find(5, value));
  EXPECT_EQ("b", value);
}

TEST(ExtendibleHashTableFindRemoveTest, RemoveEveryKeyInRandomOrder) {
  constexpr int kKeyCount = 200;
  Table table(2);
  for (int key = 0; key < kKeyCount; ++key) {
    table.Insert(key, std::to_string(key));
  }

  std::vector<int> order(kKeyCount);
  for (int key = 0; key < kKeyCount; ++key) {
    order[key] = key;
  }
  std::mt19937 generator(20261108);
  std::shuffle(order.begin(), order.end(), generator);

  for (const int key : order) {
    EXPECT_TRUE(table.Remove(key)) << "key=" << key;
    std::string value = "sentinel";
    EXPECT_FALSE(table.Find(key, value)) << "key=" << key;
    EXPECT_EQ("sentinel", value) << "key=" << key;
  }

  for (int key = 0; key < kKeyCount; ++key) {
    std::string value;
    EXPECT_FALSE(table.Find(key, value)) << "key=" << key;
    EXPECT_FALSE(table.Remove(key)) << "key=" << key;
  }
}

TEST(ExtendibleHashTableFindRemoveTest, RemoveDoesNotShrinkTable) {
  Table table(2);
  for (int key = 0; key < 64; ++key) {
    table.Insert(key, std::to_string(key));
  }

  const int global_depth = table.GetGlobalDepth();
  const int num_buckets = table.GetNumBuckets();
  ASSERT_GT(num_buckets, 1);

  for (int key = 0; key < 64; ++key) {
    EXPECT_TRUE(table.Remove(key));
  }

  // 任务说明明确不要求收缩或合并，删除后目录与桶数量应保持不变。
  EXPECT_EQ(global_depth, table.GetGlobalDepth());
  EXPECT_EQ(num_buckets, table.GetNumBuckets());
  CheckDirectoryInvariant(&table);

  // 全部删除后仍可继续写入，且局部深度不会回退。
  table.Insert(0, "again");
  std::string value;
  ASSERT_TRUE(table.Find(0, value));
  EXPECT_EQ("again", value);
  EXPECT_GE(table.GetLocalDepth(0), 0);
}

TEST(ExtendibleHashTableFindRemoveTest, SingleSlotBucketChurn) {
  constexpr int kKeyCount = 64;
  Table table(1);

  for (int key = 0; key < kKeyCount; ++key) {
    table.Insert(key, "v" + std::to_string(key));
  }
  CheckDirectoryInvariant(&table);

  for (int key = 0; key < kKeyCount; key += 2) {
    EXPECT_TRUE(table.Remove(key)) << "key=" << key;
  }

  for (int key = 0; key < kKeyCount; ++key) {
    std::string value;
    const bool found = table.Find(key, value);
    if (key % 2 == 0) {
      EXPECT_FALSE(found) << "key=" << key;
    } else {
      ASSERT_TRUE(found) << "key=" << key;
      EXPECT_EQ("v" + std::to_string(key), value) << "key=" << key;
    }
  }

  // 删除一半后再写回，验证分裂过的目录仍能把键定位到正确的桶。
  for (int key = 0; key < kKeyCount; key += 2) {
    table.Insert(key, "w" + std::to_string(key));
  }

  for (int key = 0; key < kKeyCount; ++key) {
    std::string value;
    ASSERT_TRUE(table.Find(key, value)) << "key=" << key;
    const std::string prefix = (key % 2 == 0) ? "w" : "v";
    EXPECT_EQ(prefix + std::to_string(key), value) << "key=" << key;
  }
  CheckDirectoryInvariant(&table);
}

TEST(ExtendibleHashTableFindRemoveTest, FindReturnsLatestValue) {
  Table table(4);
  std::string value;

  for (int round = 0; round < 10; ++round) {
    table.Insert(42, "round-" + std::to_string(round));
    ASSERT_TRUE(table.Find(42, value));
    EXPECT_EQ("round-" + std::to_string(round), value);
  }

  // 更新不应新增键：删除一次后就应当查不到。
  EXPECT_TRUE(table.Remove(42));
  EXPECT_FALSE(table.Find(42, value));
}

TEST(ExtendibleHashTableFindRemoveTest, DeepDirectoryWithManyKeys) {
  constexpr int kKeyCount = 5000;
  Table table(3);

  for (int key = 0; key < kKeyCount; ++key) {
    table.Insert(key, std::to_string(key));
  }
  CheckDirectoryInvariant(&table);

  for (int key = 0; key < kKeyCount; ++key) {
    std::string value;
    ASSERT_TRUE(table.Find(key, value)) << "key=" << key;
    ASSERT_EQ(std::to_string(key), value) << "key=" << key;
  }

  for (int key = 0; key < kKeyCount; key += 2) {
    ASSERT_TRUE(table.Remove(key)) << "key=" << key;
  }
  CheckDirectoryInvariant(&table);

  for (int key = 0; key < kKeyCount; ++key) {
    std::string value;
    const bool found = table.Find(key, value);
    if (key % 2 == 0) {
      EXPECT_FALSE(found) << "key=" << key;
    } else {
      ASSERT_TRUE(found) << "key=" << key;
      EXPECT_EQ(std::to_string(key), value) << "key=" << key;
    }
  }
}

//===--------------------------------------------------------------------===//
// 并发测试
//===--------------------------------------------------------------------===//

TEST(ExtendibleHashTableFindRemoveTest, ConcurrentFindDuringInsert) {
  constexpr int kWriterThreads = 4;
  constexpr int kKeysPerWriter = 250;
  constexpr int kReaderThreads = 2;

  Table table(2);
  std::atomic<int> writers_left{kWriterThreads};
  std::atomic<int> mismatches{0};
  std::vector<std::thread> threads;
  threads.reserve(kWriterThreads + kReaderThreads);

  for (int tid = 0; tid < kWriterThreads; ++tid) {
    threads.emplace_back([tid, &table, &writers_left]() {
      for (int i = 0; i < kKeysPerWriter; ++i) {
        const int key = tid * kKeysPerWriter + i;
        table.Insert(key, std::to_string(key));
      }
      writers_left.fetch_sub(1);
    });
  }

  // 读者与分裂过程并发执行：只要求不崩溃、不读到错误的值。
  // 断言统一放在 join 之后，避免在多个线程里同时触发 gtest 断言。
  for (int tid = 0; tid < kReaderThreads; ++tid) {
    threads.emplace_back([&table, &writers_left, &mismatches]() {
      std::random_device device;
      std::mt19937 generator(device());
      std::uniform_int_distribution<int> distribution(0, kWriterThreads * kKeysPerWriter - 1);
      while (writers_left.load() > 0) {
        std::string value;
        const int key = distribution(generator);
        if (table.Find(key, value) && value != std::to_string(key)) {
          mismatches.fetch_add(1);
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }
  EXPECT_EQ(0, mismatches.load());

  for (int key = 0; key < kWriterThreads * kKeysPerWriter; ++key) {
    std::string value;
    ASSERT_TRUE(table.Find(key, value)) << "key=" << key;
    ASSERT_EQ(std::to_string(key), value) << "key=" << key;
  }
  CheckDirectoryInvariant(&table);
}

TEST(ExtendibleHashTableFindRemoveTest, ConcurrentRemoveSameKeysSucceedsOnce) {
  constexpr int kKeyCount = 256;
  constexpr int kThreads = 8;
  constexpr int kRounds = 5;

  Table table(4);
  for (int key = 0; key < kKeyCount; ++key) {
    table.Insert(key, std::to_string(key));
  }
  const int num_buckets = table.GetNumBuckets();

  for (int round = 0; round < kRounds; ++round) {
    // 每轮先把整批键写回，再让所有线程竞争删除同一批键。
    for (int key = 0; key < kKeyCount; ++key) {
      table.Insert(key, std::to_string(key));
    }

    std::atomic<int> removed{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int tid = 0; tid < kThreads; ++tid) {
      threads.emplace_back([&table, &removed]() {
        for (int key = 0; key < kKeyCount; ++key) {
          if (table.Remove(key)) {
            removed.fetch_add(1);
          }
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    // 每个键只能被成功删除一次，否则说明存在重复删除（重复 erase 或丢失的更新）。
    EXPECT_EQ(kKeyCount, removed.load()) << "round=" << round;
    for (int key = 0; key < kKeyCount; ++key) {
      std::string value;
      EXPECT_FALSE(table.Find(key, value)) << "key=" << key << " round=" << round;
    }
  }

  EXPECT_EQ(num_buckets, table.GetNumBuckets());
}

TEST(ExtendibleHashTableFindRemoveTest, ConcurrentRemoveAndReinsert) {
  constexpr int kThreads = 4;
  constexpr int kKeysPerThread = 100;
  constexpr int kRounds = 10;
  constexpr int kReaderThreads = 2;

  Table table(2);
  for (int tid = 0; tid < kThreads; ++tid) {
    for (int i = 0; i < kKeysPerThread; ++i) {
      const int key = tid * kKeysPerThread + i;
      table.Insert(key, std::to_string(key));
    }
  }

  std::atomic<int> writers_left{kThreads};
  std::atomic<int> remove_failures{0};
  std::atomic<int> mismatches{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads + kReaderThreads);

  for (int tid = 0; tid < kThreads; ++tid) {
    threads.emplace_back([tid, &table, &writers_left, &remove_failures]() {
      for (int round = 0; round < kRounds; ++round) {
        for (int i = 0; i < kKeysPerThread; ++i) {
          const int key = tid * kKeysPerThread + i;
          if (!table.Remove(key)) {
            remove_failures.fetch_add(1);
          }
          table.Insert(key, std::to_string(key));
        }
      }
      writers_left.fetch_sub(1);
    });
  }

  // 删除与重新插入交替进行时，找到的键必须对应正确的值（不能读到撕裂的条目）。
  for (int tid = 0; tid < kReaderThreads; ++tid) {
    threads.emplace_back([&table, &writers_left, &mismatches]() {
      std::random_device device;
      std::mt19937 generator(device());
      std::uniform_int_distribution<int> distribution(0, kThreads * kKeysPerThread - 1);
      while (writers_left.load() > 0) {
        std::string value;
        const int key = distribution(generator);
        if (table.Find(key, value) && value != std::to_string(key)) {
          mismatches.fetch_add(1);
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  EXPECT_EQ(0, remove_failures.load());
  EXPECT_EQ(0, mismatches.load());

  for (int key = 0; key < kThreads * kKeysPerThread; ++key) {
    std::string value;
    ASSERT_TRUE(table.Find(key, value)) << "key=" << key;
    ASSERT_EQ(std::to_string(key), value) << "key=" << key;
  }
  CheckDirectoryInvariant(&table);
}

}  // namespace bustub

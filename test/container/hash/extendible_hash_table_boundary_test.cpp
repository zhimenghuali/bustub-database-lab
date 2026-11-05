/**
 * extendible_hash_table_boundary_test.cpp
 *
 * Boundary / edge-case and concurrency tests for the Extendible Hash Table (P1).
 * Owner: member B (Bucket class, Find, Remove, boundary & concurrency testing).
 *
 * Covered here:
 *   - Bucket  : empty bucket, full bucket, duplicate-key update, capacity == 1, capacity == 0
 *   - Table   : lookup / remove on an empty table, out-param untouched on a miss, double remove,
 *               negative and extreme keys, remove-then-reinsert, bucket_size == 1 (deep split chain),
 *               metadata keeps stable after removing everything (no merge/shrink in this project)
 *   - Threads : parallel readers, disjoint insert+remove, reader-writer mixed workload,
 *               concurrent Remove() of the same key (exactly one winner)
 *
 * NOTE: every table-level test uses Insert() to build the state, so the whole file is green only
 * after member A's Insert() / split implementation is merged into the module branch.
 */

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "container/hash/extendible_hash_table.h"
#include "gtest/gtest.h"
#include "storage/page/page.h"

namespace bustub {

using IntTable = ExtendibleHashTable<int, std::string>;
using IntBucket = ExtendibleHashTable<int, std::string>::Bucket;

//===--------------------------------------------------------------------===//
// Bucket
//===--------------------------------------------------------------------===//

TEST(ExtendibleHashTableBucketTest, EmptyBucket) {
  IntBucket bucket(2);

  std::string value = "sentinel";
  ASSERT_FALSE(bucket.Find(1, value));
  ASSERT_EQ("sentinel", value) << "Find() must not touch the out-param on a miss";
  ASSERT_FALSE(bucket.Remove(1));
  ASSERT_FALSE(bucket.IsFull());
  ASSERT_TRUE(bucket.GetItems().empty());
}

TEST(ExtendibleHashTableBucketTest, DuplicateKeyUpdatesInPlace) {
  IntBucket bucket(2);

  ASSERT_TRUE(bucket.Insert(1, "a"));
  ASSERT_TRUE(bucket.Insert(1, "b")) << "updating an existing key must always succeed";
  ASSERT_EQ(1U, bucket.GetItems().size()) << "a duplicate key must not consume a new slot";

  std::string value;
  ASSERT_TRUE(bucket.Find(1, value));
  ASSERT_EQ("b", value);

  // One slot left, the next distinct key fits, then the bucket is full.
  ASSERT_TRUE(bucket.Insert(2, "c"));
  ASSERT_TRUE(bucket.IsFull());
  ASSERT_FALSE(bucket.Insert(3, "d"));

  // Updating an existing key must still succeed when the bucket is full.
  ASSERT_TRUE(bucket.Insert(1, "e"));
  ASSERT_TRUE(bucket.Find(1, value));
  ASSERT_EQ("e", value);
  ASSERT_FALSE(bucket.Find(3, value));
}

TEST(ExtendibleHashTableBucketTest, RemoveFreesSlotAndKeepsOtherItems) {
  IntBucket bucket(3);
  ASSERT_TRUE(bucket.Insert(1, "a"));
  ASSERT_TRUE(bucket.Insert(2, "b"));
  ASSERT_TRUE(bucket.Insert(3, "c"));
  ASSERT_TRUE(bucket.IsFull());

  ASSERT_TRUE(bucket.Remove(2));
  ASSERT_FALSE(bucket.Remove(2)) << "removing the same key twice must report false";
  ASSERT_EQ(2U, bucket.GetItems().size());
  ASSERT_FALSE(bucket.IsFull());

  std::string value;
  ASSERT_TRUE(bucket.Find(1, value));
  ASSERT_EQ("a", value);
  ASSERT_FALSE(bucket.Find(2, value));
  ASSERT_TRUE(bucket.Find(3, value));
  ASSERT_EQ("c", value);

  // The freed slot is reusable.
  ASSERT_TRUE(bucket.Insert(4, "d"));
  ASSERT_TRUE(bucket.IsFull());
}

TEST(ExtendibleHashTableBucketTest, CapacityOne) {
  IntBucket bucket(1);

  ASSERT_TRUE(bucket.Insert(1, "a"));
  ASSERT_TRUE(bucket.IsFull());
  ASSERT_FALSE(bucket.Insert(2, "b"));
  ASSERT_TRUE(bucket.Insert(1, "b")) << "update must bypass the fullness check";

  std::string value;
  ASSERT_TRUE(bucket.Find(1, value));
  ASSERT_EQ("b", value);

  ASSERT_TRUE(bucket.Remove(1));
  ASSERT_FALSE(bucket.IsFull());
  ASSERT_TRUE(bucket.Insert(2, "c"));
}

TEST(ExtendibleHashTableBucketTest, ZeroCapacityBucketNeverAcceptsEntries) {
  IntBucket bucket(0);

  ASSERT_TRUE(bucket.IsFull());
  ASSERT_FALSE(bucket.Insert(1, "a"));
  ASSERT_TRUE(bucket.GetItems().empty());

  std::string value = "sentinel";
  ASSERT_FALSE(bucket.Find(1, value));
  ASSERT_EQ("sentinel", value);
}

TEST(ExtendibleHashTableBucketTest, LocalDepthBookkeeping) {
  IntBucket bucket(2);
  ASSERT_EQ(0, bucket.GetDepth());
  bucket.IncrementDepth();
  bucket.IncrementDepth();
  ASSERT_EQ(2, bucket.GetDepth());

  // Depth is per bucket, not shared.
  IntBucket other(2);
  ASSERT_EQ(0, other.GetDepth());
}

//===--------------------------------------------------------------------===//
// Table: Find / Remove boundary
//===--------------------------------------------------------------------===//

TEST(ExtendibleHashTableBoundaryTest, EmptyTableLookupAndRemove) {
  auto table = std::make_unique<IntTable>(2);

  std::string value = "sentinel";
  for (int key : {0, 1, -1, 7, 1024}) {
    ASSERT_FALSE(table->Find(key, value));
    ASSERT_EQ("sentinel", value);
    ASSERT_FALSE(table->Remove(key));
  }
  ASSERT_EQ(0, table->GetGlobalDepth());
  ASSERT_EQ(1, table->GetNumBuckets());
}

TEST(ExtendibleHashTableBoundaryTest, MissKeepsOutParam) {
  auto table = std::make_unique<IntTable>(2);
  for (int key = 0; key < 100; key++) {
    table->Insert(key, std::to_string(key));
  }

  std::string value = "sentinel";
  ASSERT_FALSE(table->Find(1000, value));
  ASSERT_EQ("sentinel", value);
  ASSERT_FALSE(table->Find(-1000, value));
  ASSERT_EQ("sentinel", value);
}

TEST(ExtendibleHashTableBoundaryTest, DoubleRemove) {
  auto table = std::make_unique<IntTable>(2);
  table->Insert(5, "five");

  ASSERT_TRUE(table->Remove(5));
  ASSERT_FALSE(table->Remove(5));

  std::string value;
  ASSERT_FALSE(table->Find(5, value));
}

TEST(ExtendibleHashTableBoundaryTest, NegativeAndExtremeKeys) {
  auto table = std::make_unique<IntTable>(2);

  const std::vector<int> keys = {std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
                                 -1,                                  0,
                                 1,                                   42};
  for (int key : keys) {
    table->Insert(key, std::to_string(key));
  }

  std::string value;
  for (int key : keys) {
    ASSERT_TRUE(table->Find(key, value)) << "key=" << key;
    ASSERT_EQ(std::to_string(key), value);
  }
  ASSERT_FALSE(table->Find(2, value));

  for (int key : keys) {
    ASSERT_TRUE(table->Remove(key)) << "key=" << key;
    ASSERT_FALSE(table->Find(key, value));
  }
}

TEST(ExtendibleHashTableBoundaryTest, RemoveAcrossSplitBuckets) {
  auto table = std::make_unique<IntTable>(2);
  constexpr int kKeys = 300;
  for (int key = 0; key < kKeys; key++) {
    table->Insert(key, std::to_string(key));
  }
  ASSERT_GT(table->GetGlobalDepth(), 0) << "the workload must have triggered at least one split";

  // Remove every third key, the surviving ones must still be reachable through the directory.
  for (int key = 0; key < kKeys; key += 3) {
    ASSERT_TRUE(table->Remove(key)) << "key=" << key;
  }

  std::string value;
  for (int key = 0; key < kKeys; key++) {
    if (key % 3 == 0) {
      ASSERT_FALSE(table->Find(key, value)) << "key=" << key;
    } else {
      ASSERT_TRUE(table->Find(key, value)) << "key=" << key;
      ASSERT_EQ(std::to_string(key), value);
    }
  }
}

TEST(ExtendibleHashTableBoundaryTest, RemoveThenReinsert) {
  auto table = std::make_unique<IntTable>(2);
  table->Insert(9, "old");
  ASSERT_TRUE(table->Remove(9));

  table->Insert(9, "new");
  std::string value;
  ASSERT_TRUE(table->Find(9, value));
  ASSERT_EQ("new", value);

  // Removing everything must leave the table usable.
  for (int key = 0; key < 200; key++) {
    table->Insert(key, std::to_string(key));
  }
  for (int key = 0; key < 200; key++) {
    ASSERT_TRUE(table->Remove(key));
  }
  for (int key = 0; key < 200; key++) {
    ASSERT_FALSE(table->Find(key, value));
    table->Insert(key, std::to_string(key + 1));
  }
  for (int key = 0; key < 200; key++) {
    ASSERT_TRUE(table->Find(key, value));
    ASSERT_EQ(std::to_string(key + 1), value);
  }
}

TEST(ExtendibleHashTableBoundaryTest, RemovingAllKeysKeepsMetadataStable) {
  auto table = std::make_unique<IntTable>(2);
  for (int key = 0; key < 200; key++) {
    table->Insert(key, std::to_string(key));
  }
  const int depth = table->GetGlobalDepth();
  const int buckets = table->GetNumBuckets();

  for (int key = 0; key < 200; key++) {
    ASSERT_TRUE(table->Remove(key));
  }

  // "Shrink & Combination is not required for this project": no directory entry may disappear.
  ASSERT_EQ(depth, table->GetGlobalDepth());
  ASSERT_EQ(buckets, table->GetNumBuckets());
}

TEST(ExtendibleHashTableBoundaryTest, BucketSizeOneDeepSplitChain) {
  auto table = std::make_unique<IntTable>(1);
  constexpr int kKeys = 64;
  for (int key = 0; key < kKeys; key++) {
    table->Insert(key, std::to_string(key));
  }

  std::string value;
  for (int key = 0; key < kKeys; key++) {
    ASSERT_TRUE(table->Find(key, value)) << "key=" << key;
    ASSERT_EQ(std::to_string(key), value);
  }

  for (int key = 0; key < kKeys; key++) {
    ASSERT_TRUE(table->Remove(key)) << "key=" << key;
  }
  for (int key = 0; key < kKeys; key++) {
    ASSERT_FALSE(table->Find(key, value)) << "key=" << key;
  }
}

TEST(ExtendibleHashTableBoundaryTest, PagePointerValues) {
  // The buffer pool manager stores page pointers through ExtendibleHashTable<page_id_t, Page *>.
  auto table = std::make_unique<ExtendibleHashTable<page_id_t, Page *>>(4);
  std::vector<std::unique_ptr<Page>> pages;
  for (int i = 0; i < 20; i++) {
    pages.push_back(std::make_unique<Page>());
    table->Insert(i, pages.back().get());
  }

  Page *out = nullptr;
  ASSERT_TRUE(table->Find(7, out));
  ASSERT_EQ(pages[7].get(), out);
  ASSERT_TRUE(table->Remove(7));
  ASSERT_FALSE(table->Find(7, out));
}

//===--------------------------------------------------------------------===//
// Concurrency
//===--------------------------------------------------------------------===//

TEST(ExtendibleHashTableConcurrencyTest, ParallelReaders) {
  auto table = std::make_unique<IntTable>(4);
  constexpr int kKeys = 500;
  for (int key = 0; key < kKeys; key++) {
    table->Insert(key, std::to_string(key));
  }

  constexpr int kThreads = 8;
  std::atomic<int> mismatches{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int tid = 0; tid < kThreads; tid++) {
    threads.emplace_back([&table, &mismatches]() {
      for (int repeat = 0; repeat < 20; repeat++) {
        for (int key = 0; key < kKeys; key++) {
          std::string value;
          if (!table->Find(key, value) || value != std::to_string(key)) {
            mismatches.fetch_add(1);
          }
        }
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  ASSERT_EQ(0, mismatches.load());
}

TEST(ExtendibleHashTableConcurrencyTest, DisjointInsertRemove) {
  constexpr int kThreads = 6;
  constexpr int kKeysPerThread = 200;

  for (int round = 0; round < 10; round++) {
    auto table = std::make_unique<IntTable>(2);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int tid = 0; tid < kThreads; tid++) {
      threads.emplace_back([&table, tid]() {
        const int base = tid * kKeysPerThread;
        for (int i = 0; i < kKeysPerThread; i++) {
          table->Insert(base + i, std::to_string(base + i));
        }
        for (int i = 0; i < kKeysPerThread; i += 2) {
          table->Remove(base + i);
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }

    std::string value;
    for (int key = 0; key < kThreads * kKeysPerThread; key++) {
      if (key % 2 == 0) {
        ASSERT_FALSE(table->Find(key, value)) << "key=" << key;
      } else {
        ASSERT_TRUE(table->Find(key, value)) << "key=" << key;
        ASSERT_EQ(std::to_string(key), value);
      }
    }
  }
}

TEST(ExtendibleHashTableConcurrencyTest, ConcurrentRemoveSameKey) {
  constexpr int kThreads = 8;

  for (int round = 0; round < 100; round++) {
    auto table = std::make_unique<IntTable>(2);
    table->Insert(42, "answer");

    std::atomic<int> winners{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int tid = 0; tid < kThreads; tid++) {
      threads.emplace_back([&table, &winners]() {
        if (table->Remove(42)) {
          winners.fetch_add(1);
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }

    ASSERT_EQ(1, winners.load()) << "Remove() of the same key must succeed exactly once";
    std::string value;
    ASSERT_FALSE(table->Find(42, value));
  }
}

TEST(ExtendibleHashTableConcurrencyTest, ReaderWriterMixed) {
  constexpr int kWriterThreads = 4;
  constexpr int kReaderThreads = 4;
  constexpr int kKeysPerWriter = 500;
  constexpr int kReaderIterations = 50000;
  constexpr int kTotalKeys = kWriterThreads * kKeysPerWriter;

  auto table = std::make_unique<IntTable>(4);
  std::atomic<int> bad_reads{0};
  std::vector<std::thread> threads;
  threads.reserve(kWriterThreads + kReaderThreads);

  for (int tid = 0; tid < kWriterThreads; tid++) {
    threads.emplace_back([&table, tid]() {
      const int base = tid * kKeysPerWriter;
      for (int i = 0; i < kKeysPerWriter; i++) {
        table->Insert(base + i, std::to_string(base + i));
      }
    });
  }
  for (int rid = 0; rid < kReaderThreads; rid++) {
    threads.emplace_back([&table, &bad_reads, rid]() {
      // Deterministic per-thread LCG so the test stays reproducible without a shared RNG.
      uint32_t seed = 0x9E3779B9U * static_cast<uint32_t>(rid + 1);
      for (int iteration = 0; iteration < kReaderIterations; iteration++) {
        seed = seed * 1664525U + 1013904223U;
        const int key = static_cast<int>(seed % kTotalKeys);
        std::string value;
        // Either the key is not inserted yet, or it must carry its own value.
        if (table->Find(key, value) && value != std::to_string(key)) {
          bad_reads.fetch_add(1);
        }
        if (iteration % 1000 == 0) {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  ASSERT_EQ(0, bad_reads.load());
  std::string value;
  for (int key = 0; key < kTotalKeys; key++) {
    ASSERT_TRUE(table->Find(key, value)) << "key=" << key;
    ASSERT_EQ(std::to_string(key), value);
  }
}

}  // namespace bustub

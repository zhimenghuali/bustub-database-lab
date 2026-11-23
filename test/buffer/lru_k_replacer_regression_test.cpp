#include <atomic>
#include <stdexcept>
#include <thread>  // NOLINT
#include <vector>

#include "buffer/lru_k_replacer.h"
#include "gtest/gtest.h"

namespace bustub {

TEST(LRUKReplacerRegressionTest, KOneUsesMostRecentAccess) {
  LRUKReplacer replacer(2, 1);
  replacer.RecordAccess(0);
  replacer.RecordAccess(1);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  replacer.SetEvictable(1, true);
  frame_id_t victim = -1;
  ASSERT_TRUE(replacer.Evict(&victim));
  EXPECT_EQ(victim, 1);
}

TEST(LRUKReplacerRegressionTest, IncompleteHistoryUsesEarliestAccess) {
  LRUKReplacer replacer(2, 3);
  replacer.RecordAccess(0);
  replacer.RecordAccess(1);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  replacer.SetEvictable(1, true);
  frame_id_t victim = -1;
  ASSERT_TRUE(replacer.Evict(&victim));
  EXPECT_EQ(victim, 0);
}

TEST(LRUKReplacerRegressionTest, InvalidFramesDoNotChangeState) {
  LRUKReplacer replacer(2, 2);
  EXPECT_THROW(replacer.RecordAccess(-1), std::out_of_range);
  EXPECT_THROW(replacer.RecordAccess(2), std::out_of_range);
  EXPECT_THROW(replacer.SetEvictable(2, true), std::out_of_range);
  EXPECT_THROW(replacer.Remove(-1), std::out_of_range);
  EXPECT_EQ(replacer.Size(), 0);
  EXPECT_THROW(LRUKReplacer(2, 0), std::invalid_argument);
}

TEST(LRUKReplacerRegressionTest, ConcurrentEvictableChangesKeepSizeConsistent) {
  LRUKReplacer replacer(1, 2);
  replacer.SetEvictable(0, true);
  EXPECT_EQ(replacer.Size(), 0);
  replacer.RecordAccess(0);

  constexpr auto kThreadCount = 8;
  constexpr auto kIterations = 1000;
  std::atomic<bool> stop_size_reader{false};
  std::atomic<bool> invalid_size{false};
  std::atomic<size_t> size_reads{0};
  std::thread size_reader([&] {
    while (!stop_size_reader.load(std::memory_order_acquire)) {
      if (replacer.Size() > 1) {
        invalid_size.store(true, std::memory_order_relaxed);
      }
      size_reads.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::yield();
    }
  });

  const auto run_concurrently = [&](const auto &operation) {
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (int i = 0; i < kThreadCount; ++i) {
      workers.emplace_back([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        operation();
      });
    }
    while (ready.load(std::memory_order_acquire) != kThreadCount) {
      std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto &worker : workers) {
      worker.join();
    }
  };

  run_concurrently([&] {
    for (int i = 0; i < kIterations; ++i) {
      replacer.SetEvictable(0, true);
      replacer.SetEvictable(0, false);
    }
  });
  EXPECT_EQ(replacer.Size(), 0);

  replacer.SetEvictable(0, true);
  EXPECT_EQ(replacer.Size(), 1);
  run_concurrently([&] {
    for (int i = 0; i < kIterations; ++i) {
      replacer.Remove(0);
    }
  });
  EXPECT_EQ(replacer.Size(), 0);

  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  EXPECT_EQ(replacer.Size(), 1);
  replacer.Remove(0);
  EXPECT_EQ(replacer.Size(), 0);

  stop_size_reader.store(true, std::memory_order_release);
  size_reader.join();
  EXPECT_GT(size_reads.load(std::memory_order_relaxed), 0);
  EXPECT_FALSE(invalid_size.load(std::memory_order_relaxed));
}

TEST(LRUKReplacerRegressionTest, RejectRemovalOfPinnedFrame) {
  LRUKReplacer replacer(1, 2);
  replacer.RecordAccess(0);
  EXPECT_THROW(replacer.Remove(0), std::logic_error);
  replacer.SetEvictable(0, true);
  frame_id_t victim = -1;
  ASSERT_TRUE(replacer.Evict(&victim));
  EXPECT_EQ(victim, 0);
}

TEST(LRUKReplacerRegressionTest, ReusedFrameStartsWithEmptyHistory) {
  LRUKReplacer replacer(2, 2);
  for (frame_id_t frame = 0; frame < 2; ++frame) {
    replacer.RecordAccess(frame);
    replacer.RecordAccess(frame);
    replacer.SetEvictable(frame, true);
  }
  frame_id_t victim = -1;
  ASSERT_TRUE(replacer.Evict(&victim));
  ASSERT_EQ(victim, 0);
  replacer.RecordAccess(0);
  replacer.SetEvictable(0, true);
  ASSERT_TRUE(replacer.Evict(&victim));
  EXPECT_EQ(victim, 0);
}

}  // namespace bustub

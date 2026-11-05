//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance_f_test.cpp
//
// Identification: test/buffer/buffer_pool_manager_instance_f_test.cpp
//
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>  // NOLINT
#include <vector>

#include "buffer/buffer_pool_manager_instance.h"
#include "gtest/gtest.h"
#include "storage/disk/disk_manager_memory.h"

namespace bustub {
namespace {

class RecordingDiskManager : public DiskManagerUnlimitedMemory {
 public:
  void WritePage(page_id_t page_id, const char *page_data) override {
    if (fail_writes_) {
      throw std::runtime_error("simulated write failure");
    }
    DiskManagerUnlimitedMemory::WritePage(page_id, page_data);
    writes_++;
  }

  auto WriteCount() const -> size_t { return writes_.load(); }
  void SetWriteFailure(bool fail) { fail_writes_ = fail; }

 private:
  std::atomic<size_t> writes_{0};
  std::atomic<bool> fail_writes_{false};
};

TEST(BufferPoolManagerFTest, UnpinRejectsMissingAndZeroPins) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(1, &disk);
  EXPECT_FALSE(bpm.UnpinPage(INVALID_PAGE_ID, true));
  EXPECT_FALSE(bpm.UnpinPage(100, true));

  page_id_t page_id = INVALID_PAGE_ID;
  auto *page = bpm.NewPage(&page_id);
  ASSERT_NE(page, nullptr);
  ASSERT_TRUE(bpm.UnpinPage(page_id, false));
  EXPECT_EQ(page->GetPinCount(), 0);
  EXPECT_FALSE(page->IsDirty());
  EXPECT_FALSE(bpm.UnpinPage(page_id, true));
  EXPECT_EQ(page->GetPinCount(), 0);
  EXPECT_FALSE(page->IsDirty());
}

TEST(BufferPoolManagerFTest, UnpinPreservesDirtyAndWaitsForFinalPin) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(1, &disk);
  page_id_t page_id = INVALID_PAGE_ID;
  auto *page = bpm.NewPage(&page_id);
  ASSERT_NE(page, nullptr);
  ASSERT_EQ(bpm.FetchPage(page_id), page);
  ASSERT_EQ(page->GetPinCount(), 2);

  ASSERT_TRUE(bpm.UnpinPage(page_id, true));
  EXPECT_EQ(page->GetPinCount(), 1);
  EXPECT_TRUE(page->IsDirty());
  page_id_t replacement_id = INVALID_PAGE_ID;
  EXPECT_EQ(bpm.NewPage(&replacement_id), nullptr);

  ASSERT_TRUE(bpm.UnpinPage(page_id, false));
  EXPECT_EQ(page->GetPinCount(), 0);
  EXPECT_TRUE(page->IsDirty());
  EXPECT_NE(bpm.NewPage(&replacement_id), nullptr);
}

TEST(BufferPoolManagerFTest, FlushAlwaysWritesAndClearsDirty) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(1, &disk);
  page_id_t page_id = INVALID_PAGE_ID;
  auto *page = bpm.NewPage(&page_id);
  ASSERT_NE(page, nullptr);

  std::array<char, BUSTUB_PAGE_SIZE> expected{};
  expected.fill('x');
  expected[BUSTUB_PAGE_SIZE / 2] = '\0';
  expected[BUSTUB_PAGE_SIZE - 1] = 'z';
  std::memcpy(page->GetData(), expected.data(), expected.size());
  ASSERT_FALSE(page->IsDirty());
  ASSERT_TRUE(bpm.FlushPage(page_id));
  EXPECT_EQ(disk.WriteCount(), 1U);
  EXPECT_EQ(page->GetPinCount(), 1);

  std::array<char, BUSTUB_PAGE_SIZE> actual{};
  disk.ReadPage(page_id, actual.data());
  EXPECT_EQ(actual, expected);

  ASSERT_TRUE(bpm.UnpinPage(page_id, true));
  ASSERT_TRUE(page->IsDirty());
  ASSERT_TRUE(bpm.FlushPage(page_id));
  EXPECT_EQ(disk.WriteCount(), 2U);
  EXPECT_FALSE(page->IsDirty());
  EXPECT_EQ(page->GetPinCount(), 0);
  page_id_t replacement_id = INVALID_PAGE_ID;
  EXPECT_NE(bpm.NewPage(&replacement_id), nullptr);
}

TEST(BufferPoolManagerFTest, FlushRejectsMissingAndInvalidPages) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(2, &disk);
  EXPECT_FALSE(bpm.FlushPage(INVALID_PAGE_ID));
  EXPECT_FALSE(bpm.FlushPage(100));
  EXPECT_EQ(disk.WriteCount(), 0U);
}

TEST(BufferPoolManagerFTest, FlushAllSkipsFreeAndDeletedFrames) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(4, &disk);
  bpm.FlushAllPages();
  EXPECT_EQ(disk.WriteCount(), 0U);

  page_id_t first_id = INVALID_PAGE_ID;
  auto *first = bpm.NewPage(&first_id);
  ASSERT_NE(first, nullptr);
  page_id_t deleted_id = INVALID_PAGE_ID;
  ASSERT_NE(bpm.NewPage(&deleted_id), nullptr);
  page_id_t third_id = INVALID_PAGE_ID;
  auto *third = bpm.NewPage(&third_id);
  ASSERT_NE(third, nullptr);
  first->GetData()[0] = 'a';
  third->GetData()[0] = 'c';
  ASSERT_TRUE(bpm.UnpinPage(first_id, true));
  ASSERT_TRUE(bpm.UnpinPage(deleted_id, true));
  ASSERT_TRUE(bpm.DeletePage(deleted_id));

  bpm.FlushAllPages();
  EXPECT_EQ(disk.WriteCount(), 2U);
  EXPECT_FALSE(first->IsDirty());
  EXPECT_FALSE(third->IsDirty());
  EXPECT_EQ(first->GetPinCount(), 0);
  EXPECT_EQ(third->GetPinCount(), 1);
  std::array<char, BUSTUB_PAGE_SIZE> actual{};
  disk.ReadPage(first_id, actual.data());
  EXPECT_EQ(actual[0], 'a');
  disk.ReadPage(third_id, actual.data());
  EXPECT_EQ(actual[0], 'c');

  bpm.FlushAllPages();
  EXPECT_EQ(disk.WriteCount(), 4U);
}

TEST(BufferPoolManagerFTest, DeleteRejectsPinnedAndReturnsFrameOnce) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(2, &disk);
  page_id_t page_id = INVALID_PAGE_ID;
  auto *page = bpm.NewPage(&page_id);
  ASSERT_NE(page, nullptr);
  std::memset(page->GetData(), 'd', BUSTUB_PAGE_SIZE);

  EXPECT_FALSE(bpm.DeletePage(page_id));
  EXPECT_EQ(page->GetPageId(), page_id);
  EXPECT_EQ(page->GetPinCount(), 1);
  EXPECT_EQ(page->GetData()[0], 'd');
  ASSERT_TRUE(bpm.UnpinPage(page_id, true));
  ASSERT_TRUE(bpm.DeletePage(page_id));
  EXPECT_EQ(page->GetPageId(), INVALID_PAGE_ID);
  EXPECT_EQ(page->GetPinCount(), 0);
  EXPECT_FALSE(page->IsDirty());
  std::array<char, BUSTUB_PAGE_SIZE> empty{};
  EXPECT_EQ(std::memcmp(page->GetData(), empty.data(), empty.size()), 0);
  EXPECT_EQ(disk.WriteCount(), 0U);
  EXPECT_TRUE(bpm.DeletePage(page_id));
  EXPECT_FALSE(bpm.FlushPage(page_id));
  EXPECT_FALSE(bpm.UnpinPage(page_id, false));

  page_id_t replacement_id = INVALID_PAGE_ID;
  ASSERT_NE(bpm.NewPage(&replacement_id), nullptr);
  ASSERT_NE(bpm.NewPage(&replacement_id), nullptr);
  EXPECT_EQ(bpm.NewPage(&replacement_id), nullptr);
}

TEST(BufferPoolManagerFTest, DeleteMissingPageDoesNotConsumeFramesOrIds) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(1, &disk);
  EXPECT_TRUE(bpm.DeletePage(100));
  EXPECT_TRUE(bpm.DeletePage(INVALID_PAGE_ID));
  page_id_t page_id = INVALID_PAGE_ID;
  ASSERT_NE(bpm.NewPage(&page_id), nullptr);
  EXPECT_EQ(page_id, 0);
  EXPECT_EQ(disk.WriteCount(), 0U);
}

TEST(BufferPoolManagerFTest, FailedFlushPreservesDirtyFlag) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(1, &disk);
  page_id_t page_id = INVALID_PAGE_ID;
  auto *page = bpm.NewPage(&page_id);
  ASSERT_NE(page, nullptr);
  ASSERT_TRUE(bpm.UnpinPage(page_id, true));
  disk.SetWriteFailure(true);
  EXPECT_THROW(bpm.FlushPage(page_id), std::runtime_error);
  EXPECT_TRUE(page->IsDirty());
  EXPECT_EQ(page->GetPinCount(), 0);
  EXPECT_EQ(disk.WriteCount(), 0U);

  disk.SetWriteFailure(false);
  ASSERT_TRUE(bpm.FlushPage(page_id));
  EXPECT_FALSE(page->IsDirty());
  EXPECT_EQ(disk.WriteCount(), 1U);
}

TEST(BufferPoolManagerFTest, ConcurrentUnpinDoesNotUnderflow) {
  RecordingDiskManager disk;
  BufferPoolManagerInstance bpm(1, &disk);
  page_id_t page_id = INVALID_PAGE_ID;
  auto *page = bpm.NewPage(&page_id);
  ASSERT_NE(page, nullptr);
  constexpr int num_threads = 8;
  for (int i = 1; i < num_threads; i++) {
    ASSERT_EQ(bpm.FetchPage(page_id), page);
  }

  std::atomic<int> successes{0};
  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&bpm, &successes, page_id, i]() {
      if (bpm.UnpinPage(page_id, i == 0)) {
        successes++;
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  EXPECT_EQ(successes.load(), num_threads);
  EXPECT_EQ(page->GetPinCount(), 0);
  EXPECT_TRUE(page->IsDirty());
  EXPECT_FALSE(bpm.UnpinPage(page_id, false));
}

}  // namespace
}  // namespace bustub

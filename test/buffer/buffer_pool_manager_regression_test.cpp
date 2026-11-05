#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

#include "buffer/buffer_pool_manager_instance.h"
#include "gtest/gtest.h"

namespace bustub {

class BufferPoolManagerRegressionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    disk_ = std::make_unique<DiskManager>("p1-regression.db");
    bpm_ = std::make_unique<BufferPoolManagerInstance>(1, disk_.get(), 2);
  }

  void TearDown() override {
    bpm_.reset();
    disk_->ShutDown();
    disk_.reset();
    std::remove("p1-regression.db");
    std::remove("p1-regression.log");
  }

  std::unique_ptr<DiskManager> disk_;
  std::unique_ptr<BufferPoolManagerInstance> bpm_;
};

TEST_F(BufferPoolManagerRegressionTest, FlushPinnedPageRegardlessOfDirtyFlag) {
  page_id_t page_id;
  auto *page = bpm_->NewPage(&page_id);
  ASSERT_NE(page, nullptr);
  std::memcpy(page->GetData(), "pinned", 7);
  ASSERT_FALSE(page->IsDirty());
  ASSERT_TRUE(bpm_->FlushPage(page_id));
  std::array<char, BUSTUB_PAGE_SIZE> data{};
  disk_->ReadPage(page_id, data.data());
  EXPECT_STREQ(data.data(), "pinned");
  EXPECT_EQ(page->GetPinCount(), 1);
  ASSERT_TRUE(bpm_->UnpinPage(page_id, true));
  ASSERT_EQ(bpm_->FetchPage(page_id), page);
  std::memcpy(page->GetData(), "batch", 6);
  bpm_->FlushAllPages();
  EXPECT_FALSE(page->IsDirty());
  EXPECT_EQ(page->GetPinCount(), 1);
  disk_->ReadPage(page_id, data.data());
  EXPECT_STREQ(data.data(), "batch");
}

TEST_F(BufferPoolManagerRegressionTest, DirtyFlagSurvivesCleanUnpinAndEviction) {
  page_id_t original;
  auto *page = bpm_->NewPage(&original);
  ASSERT_NE(page, nullptr);
  std::memcpy(page->GetData(), "persist", 8);
  ASSERT_EQ(bpm_->FetchPage(original), page);
  ASSERT_TRUE(bpm_->UnpinPage(original, true));
  ASSERT_TRUE(bpm_->UnpinPage(original, false));
  EXPECT_TRUE(page->IsDirty());
  page_id_t replacement;
  auto *reused = bpm_->NewPage(&replacement);
  ASSERT_NE(reused, nullptr);
  EXPECT_EQ(reused->GetData()[0], '\0');
  EXPECT_FALSE(reused->IsDirty());
  ASSERT_TRUE(bpm_->UnpinPage(replacement, false));
  auto *reloaded = bpm_->FetchPage(original);
  ASSERT_NE(reloaded, nullptr);
  EXPECT_STREQ(reloaded->GetData(), "persist");
}

TEST_F(BufferPoolManagerRegressionTest, DeleteReleasesFrameAndResetsMetadata) {
  page_id_t original;
  auto *page = bpm_->NewPage(&original);
  ASSERT_NE(page, nullptr);
  page->GetData()[0] = 'x';
  EXPECT_FALSE(bpm_->DeletePage(original));
  ASSERT_TRUE(bpm_->UnpinPage(original, true));
  ASSERT_TRUE(bpm_->DeletePage(original));
  EXPECT_TRUE(bpm_->DeletePage(original));
  page_id_t next;
  auto *reused = bpm_->NewPage(&next);
  ASSERT_NE(reused, nullptr);
  EXPECT_EQ(reused, page);
  EXPECT_NE(next, original);
  EXPECT_EQ(reused->GetPageId(), next);
  EXPECT_EQ(reused->GetPinCount(), 1);
  EXPECT_FALSE(reused->IsDirty());
  EXPECT_EQ(reused->GetData()[0], '\0');
}

TEST_F(BufferPoolManagerRegressionTest, FailedRequestsPreservePinnedPage) {
  page_id_t original;
  auto *page = bpm_->NewPage(&original);
  ASSERT_NE(page, nullptr);
  page->GetData()[0] = 'x';
  page_id_t failed_id = 123;
  EXPECT_EQ(bpm_->NewPage(&failed_id), nullptr);
  EXPECT_EQ(failed_id, 123);
  EXPECT_EQ(bpm_->FetchPage(INVALID_PAGE_ID), nullptr);
  EXPECT_FALSE(bpm_->FlushPage(INVALID_PAGE_ID));
  EXPECT_FALSE(bpm_->UnpinPage(123, true));
  EXPECT_EQ(page->GetPageId(), original);
  EXPECT_EQ(page->GetPinCount(), 1);
  EXPECT_EQ(page->GetData()[0], 'x');
}

}  // namespace bustub

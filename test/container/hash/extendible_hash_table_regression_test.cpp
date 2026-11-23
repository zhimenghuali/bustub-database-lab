#include <stdexcept>

#include "container/hash/extendible_hash_table.h"
#include "gtest/gtest.h"

namespace bustub {

TEST(ExtendibleHashTableRegressionTest, FullBucketUpdateDoesNotSplit) {
  ExtendibleHashTable<int, int> table(1);
  table.Insert(1, 10);
  table.Insert(1, 20);
  int value = -1;
  ASSERT_TRUE(table.Find(1, value));
  EXPECT_EQ(value, 20);
  EXPECT_EQ(table.GetGlobalDepth(), 0);
  EXPECT_EQ(table.GetNumBuckets(), 1);
}

TEST(ExtendibleHashTableRegressionTest, RepeatedSplitsPreserveOtherBuckets) {
  ExtendibleHashTable<int, int> table(1);
  table.Insert(0, 10);
  table.Insert(1, 20);
  table.Insert(16, 30);
  EXPECT_EQ(table.GetGlobalDepth(), 5);
  int value = -1;
  ASSERT_TRUE(table.Find(0, value));
  EXPECT_EQ(value, 10);
  ASSERT_TRUE(table.Find(1, value));
  EXPECT_EQ(value, 20);
  ASSERT_TRUE(table.Find(16, value));
  EXPECT_EQ(value, 30);
  EXPECT_TRUE(table.Remove(16));
  EXPECT_FALSE(table.Remove(16));
  EXPECT_EQ(table.GetGlobalDepth(), 5);
}

TEST(ExtendibleHashTableRegressionTest, RejectZeroCapacity) {
  EXPECT_THROW((ExtendibleHashTable<int, int>(0)), std::invalid_argument);
}

}  // namespace bustub

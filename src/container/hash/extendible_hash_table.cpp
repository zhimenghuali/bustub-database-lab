//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <functional>
#include <list>
#include <stdexcept>
#include <utility>

#include "container/hash/extendible_hash_table.h"
#include "storage/page/page.h"

namespace bustub {

template <typename K, typename V>
ExtendibleHashTable<K, V>::ExtendibleHashTable(size_t bucket_size) : bucket_size_(bucket_size) {
  if (bucket_size == 0) {
    throw std::invalid_argument("桶容量必须大于零");
  }
  dir_.push_back(std::make_shared<Bucket>(bucket_size_));
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::IndexOf(const K &key) -> size_t {
  size_t mask = (size_t{1} << global_depth_) - 1;
  return std::hash<K>()(key) & mask;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepth() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetGlobalDepthInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepthInternal() const -> int {
  return global_depth_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepth(int dir_index) const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetLocalDepthInternal(dir_index);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepthInternal(int dir_index) const -> int {
  return dir_[dir_index]->GetDepth();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBuckets() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetNumBucketsInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBucketsInternal() const -> int {
  return num_buckets_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Find(const K &key, V &value) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  // 未命中时不写 value，调用方可以复用同一个输出参数做连续查找。
  return dir_[IndexOf(key)]->Find(key, value);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Remove(const K &key) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  // 只摘除键值对，不合并空桶、不回缩目录：任务说明明确不要求 shrink/combination。
  return dir_[IndexOf(key)]->Remove(key);
}

template <typename K, typename V>
void ExtendibleHashTable<K, V>::Insert(const K &key, const V &value) {
  std::scoped_lock<std::mutex> lock(latch_);
  auto bucket = dir_[IndexOf(key)];
  while (!bucket->Insert(key, value)) {
    RedistributeBucket(bucket);
    bucket = dir_[IndexOf(key)];
  }
}

template <typename K, typename V>
void ExtendibleHashTable<K, V>::RedistributeBucket(std::shared_ptr<Bucket> bucket) {
  const auto split_bit = size_t{1} << bucket->GetDepth();
  if (bucket->GetDepth() == global_depth_) {
    const auto old_size = dir_.size();
    if (old_size > dir_.max_size() / 2) {
      throw std::length_error("哈希目录已达到容量上限");
    }
    dir_.resize(old_size * 2);
    for (size_t i = 0; i < old_size; ++i) {
      dir_[old_size + i] = dir_[i];
    }
    ++global_depth_;
  }

  auto new_bucket = std::make_shared<Bucket>(bucket_size_, bucket->GetDepth() + 1);
  bucket->IncrementDepth();
  ++num_buckets_;
  for (size_t i = 0; i < dir_.size(); ++i) {
    if (dir_[i] == bucket && (i & split_bit) != 0) {
      dir_[i] = new_bucket;
    }
  }

  // 只移动本次分裂桶的节点，保持其他桶和键值对象不变。
  auto &items = bucket->GetItems();
  auto &new_items = new_bucket->GetItems();
  for (auto item = items.begin(); item != items.end();) {
    auto current = item++;
    if ((std::hash<K>()(current->first) & split_bit) != 0) {
      new_items.splice(new_items.end(), items, current);
    }
  }
}

//===--------------------------------------------------------------------===//
// Bucket
//===--------------------------------------------------------------------===//
template <typename K, typename V>
ExtendibleHashTable<K, V>::Bucket::Bucket(size_t array_size, int depth) : size_(array_size), depth_(depth) {}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Find(const K &key, V &value) -> bool {
  // 桶内是无序链表，只能线性扫描；只有命中时才写回 value。
  for (const auto &item : list_) {
    if (item.first == key) {
      value = item.second;
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Remove(const K &key) -> bool {
  // 桶内不允许重复键，因此最多命中一次，删除后立即返回。
  for (auto item = list_.begin(); item != list_.end(); ++item) {
    if (item->first == key) {
      list_.erase(item);
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Insert(const K &key, const V &value) -> bool {
  // 满桶中的已有键仍可更新，无需触发分裂。
  for (auto &item : list_) {
    if (item.first == key) {
      item.second = value;
      return true;
    }
  }
  if (IsFull()) {
    return false;
  }
  list_.emplace_back(key, value);
  return true;
}

template class ExtendibleHashTable<page_id_t, Page *>;
template class ExtendibleHashTable<Page *, std::list<Page *>::iterator>;
template class ExtendibleHashTable<int, int>;
// test purpose
template class ExtendibleHashTable<int, std::string>;
template class ExtendibleHashTable<int, std::list<int>::iterator>;

}  // namespace bustub

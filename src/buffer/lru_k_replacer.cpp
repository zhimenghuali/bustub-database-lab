//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"

#include <stdexcept>

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k), frames_(num_frames) {
  if (k == 0) {
    throw std::invalid_argument("LRU-K 的 K 必须大于零");
  }
}

void LRUKReplacer::ValidateFrame(frame_id_t frame_id) const {
  if (frame_id < 0 || static_cast<size_t>(frame_id) >= replacer_size_) {
    throw std::out_of_range("帧编号超出替换器范围");
  }
}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  size_t victim = frames_.size();
  bool victim_incomplete = false;
  size_t oldest_access = 0;
  for (size_t i = 0; i < frames_.size(); ++i) {
    const auto &frame = frames_[i];
    if (!frame.evictable_ || frame.accesses_.empty()) {
      continue;
    }
    const bool incomplete = frame.accesses_.size() < k_;
    const auto access = frame.accesses_.front();
    if (victim == frames_.size() || (incomplete && !victim_incomplete) ||
        (incomplete == victim_incomplete && access < oldest_access)) {
      victim = i;
      victim_incomplete = incomplete;
      oldest_access = access;
    }
  }
  if (victim == frames_.size()) {
    return false;
  }
  frames_[victim].accesses_.clear();
  frames_[victim].evictable_ = false;
  --curr_size_;
  *frame_id = static_cast<frame_id_t>(victim);
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  ValidateFrame(frame_id);
  auto &history = frames_[frame_id].accesses_;
  history.push_back(current_timestamp_++);
  if (history.size() > k_) {
    history.pop_front();
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);
  ValidateFrame(frame_id);
  auto &frame = frames_[frame_id];
  if (frame.accesses_.empty() || frame.evictable_ == set_evictable) {
    return;
  }
  frame.evictable_ = set_evictable;
  if (set_evictable) {
    ++curr_size_;
  } else {
    --curr_size_;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  ValidateFrame(frame_id);
  auto &frame = frames_[frame_id];
  if (frame.accesses_.empty()) {
    return;
  }
  if (!frame.evictable_) {
    throw std::logic_error("不能移除被固定的帧");
  }
  frame.accesses_.clear();
  frame.evictable_ = false;
  --curr_size_;
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub

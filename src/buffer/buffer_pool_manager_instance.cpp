//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::AcquireFrame(frame_id_t *frame_id) -> bool {
  // A frame is always taken from the free list first: it holds no page, so nothing has to be evicted.
  if (!free_list_.empty()) {
    *frame_id = free_list_.front();
    free_list_.pop_front();
    return true;
  }

  // No free frame left, ask the replacer for the frame with the largest backward k-distance.
  if (!replacer_->Evict(frame_id)) {
    // Every frame is pinned, the caller cannot be served.
    return false;
  }

  Page *victim = &pages_[*frame_id];
  page_id_t victim_id = victim->GetPageId();

  // A dirty page holds the only up-to-date copy of its content, it must reach the disk before the frame is reused.
  if (victim->IsDirty()) {
    disk_manager_->WritePage(victim_id, victim->GetData());
  }

  // Drop every trace of the victim so that the frame looks like a brand new one.
  page_table_->Remove(victim_id);
  victim->ResetMemory();
  victim->page_id_ = INVALID_PAGE_ID;
  victim->pin_count_ = 0;
  victim->is_dirty_ = false;

  // Evict() already removed the victim's access history from the replacer.
  return true;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);

  frame_id_t frame_id;
  if (!AcquireFrame(&frame_id)) {
    // All frames are pinned, no page id was consumed and the caller sees nullptr.
    return nullptr;
  }

  // The page id is allocated only once a frame is guaranteed, so a failure never burns an id.
  page_id_t new_page_id = AllocatePage();
  Page *page = &pages_[frame_id];
  page->ResetMemory();
  page->page_id_ = new_page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;

  page_table_->Insert(new_page_id, frame_id);
  replacer_->RecordAccess(frame_id);
  // The frame must not be evicted while the caller is still using it.
  replacer_->SetEvictable(frame_id, false);

  *page_id = new_page_id;
  return page;
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  if (page_id == INVALID_PAGE_ID) {
    return nullptr;
  }

  std::scoped_lock<std::mutex> lock(latch_);

  frame_id_t frame_id;
  if (page_table_->Find(page_id, frame_id)) {
    // Hit: the page is already resident, only its pin count and access history change.
    Page *page = &pages_[frame_id];
    page->pin_count_++;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return page;
  }

  // Miss: the page has to be brought in from the disk.
  if (!AcquireFrame(&frame_id)) {
    return nullptr;
  }

  Page *page = &pages_[frame_id];
  page->ResetMemory();
  disk_manager_->ReadPage(page_id, page->GetData());
  page->page_id_ = page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;

  page_table_->Insert(page_id, frame_id);
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return page;
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool { return false; }

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool { return false; }

void BufferPoolManagerInstance::FlushAllPgsImp() {}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool { return false; }

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

}  // namespace bustub

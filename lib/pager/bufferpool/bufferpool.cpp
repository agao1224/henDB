#include "pager/bufferpool/bufferpool.h"
#include "pager/bufferpool/evictor.h"
#include <cassert>

pager::BufferPoolManager::BufferPoolManager(
    uint32_t num_frames, storage::StorageEngine *storage_engine)
    : num_frames_(num_frames) {
  storage_engine_ = std::shared_ptr<storage::StorageEngine>(storage_engine);
  evictor_ = std::make_shared<pager::LRUKEvictor>(EVICTOR_K, num_frames);
  bpm_latch_ = std::make_shared<std::mutex>();
  for (size_t id = 0; id < num_frames; id++) {
    pager::frame_id_t frame_id = static_cast<pager::frame_id_t>(id);
    frames_.push_back(std::make_shared<pager::FrameHeader>(frame_id));
    free_list_.push_back(frame_id);
  }
}

pager::BufferPoolManager::~BufferPoolManager() = default;

size_t pager::BufferPoolManager::size() const {
  assert(free_list_.size() + frame_table_.size() == num_frames_);
  return frame_table_.size();
}

pager::frame_id_t pager::BufferPoolManager::insert_frame(PageKey pgkey) {
  std::vector<std::byte> read_buffer;
  storage_engine_->read_page(pgkey, read_buffer);

  pager::frame_id_t frame_id = free_list_.front();
  free_list_.pop_front();

  assert(0 <= frame_id && frame_id < frames_.size());
  auto frame = frames_[frame_id];
  assert(frame != nullptr);

  frame->reset();
  auto &frame_data = frame->get_data_mut();
  std::memcpy(frame_data.data(), read_buffer.data(), PAGE_SIZE);

  frame_table_[pgkey] = frame_id;
  evictor_->record_access(frame_id);
  evictor_->set_evictable(frame_id, false);
  return frame_id;
}

bool pager::BufferPoolManager::ensure_free_frame() {
  if (!free_list_.empty())
    return true;
  auto evicted = evictor_->evict();
  if (!evicted.has_value())
    return false;
  free_list_.push_back(evicted.value());
  return true;
}

std::optional<PageKey> pager::BufferPoolManager::new_page(TableID tbl_id) {
  assert(storage_engine_ != nullptr);
  assert(bpm_latch_ != nullptr);
  assert(evictor_ != nullptr);

  bpm_latch_->lock();
  if (!ensure_free_frame()) {
    bpm_latch_->unlock();
    return std::nullopt;
  }

  assert(free_list_.size() > 0 && size() < num_frames_);
  PageKey pgkey = storage_engine_->allocate_page(tbl_id);
  insert_frame(pgkey);

  bpm_latch_->unlock();
  return pgkey;
}

pager::ReadPageGuard pager::BufferPoolManager::read_page(PageKey pgkey) {
  assert(bpm_latch_ != nullptr);
  assert(storage_engine_ != nullptr);

  bpm_latch_->lock();
  auto it = frame_table_.find(pgkey);
  pager::frame_id_t frame_id;
  if (it != frame_table_.end()) {
    frame_id = it->second;
    evictor_->record_access(frame_id);
    evictor_->set_evictable(frame_id, false);
  } else if (ensure_free_frame()) {
    frame_id = insert_frame(pgkey);
  } else {
    bpm_latch_->unlock();
    return pager::ReadPageGuard{};
  }

  assert(0 <= frame_id && frame_id < frames_.size());
  auto frame = frames_[frame_id];
  pager::ReadPageGuard page_guard(pgkey, frame, evictor_, bpm_latch_,
                                  storage_engine_);
  bpm_latch_->unlock();
  return page_guard;
}

pager::WritePageGuard pager::BufferPoolManager::write_page(PageKey pgkey) {
  assert(bpm_latch_ != nullptr);
  assert(storage_engine_ != nullptr);

  bpm_latch_->lock();
  auto it = frame_table_.find(pgkey);
  pager::frame_id_t frame_id;
  if (it != frame_table_.end()) {
    frame_id = it->second;
    evictor_->record_access(frame_id);
    evictor_->set_evictable(frame_id, false);
  } else if (ensure_free_frame()) {
    frame_id = insert_frame(pgkey);
  } else {
    bpm_latch_->unlock();
    return pager::WritePageGuard{};
  }

  assert(0 <= frame_id && frame_id < frames_.size());
  auto frame = frames_[frame_id];
  pager::WritePageGuard page_guard(pgkey, frame, evictor_, bpm_latch_,
                                   storage_engine_);
  bpm_latch_->unlock();
  return page_guard;
}

bool pager::BufferPoolManager::evict_page(PageKey pgkey) {
  assert(bpm_latch_ != nullptr);
  bpm_latch_->lock();

  auto it = frame_table_.find(pgkey);
  if (it == frame_table_.end()) {
    bpm_latch_->unlock();
    return true;
  }

  pager::frame_id_t frame_id = it->second;
  assert(0 <= frame_id && frame_id < frames_.size());
  auto frame = frames_[frame_id];
  assert(frame != nullptr);

  if (frame->get_pins() > 0) {
    bpm_latch_->unlock();
    return false;
  }

  frame_table_.erase(pgkey);
  frame->reset();
  free_list_.push_back(frame_id);
  evictor_->remove(frame_id);

  bpm_latch_->unlock();

  return true;
}

bool pager::BufferPoolManager::flush_page(PageKey pgkey) {
  assert(storage_engine_ != nullptr);

  auto it = frame_table_.find(pgkey);
  if (it == frame_table_.end())
    return false;
  pager::frame_id_t frame_id = it->second;

  assert(0 <= frame_id && frame_id < frames_.size());
  auto frame = frames_[frame_id];
  assert(frame != nullptr);

  frame->lock_write();

  std::vector<std::byte> data = frame->get_data();
  storage_engine_->write_page(pgkey, data);
  frame->set_dirty(false);

  frame->unlock_write();
  return true;
}

void pager::BufferPoolManager::flush_all_pages() {
  assert(storage_engine_ != nullptr);
  for (auto &[pgkey, frame_id] : frame_table_) {
    assert(0 <= frame_id && frame_id < frames_.size());
    auto frame = frames_[frame_id];
    assert(frame != nullptr);

    frame->lock_write();

    std::vector<std::byte> data = frame->get_data();
    storage_engine_->write_page(pgkey, data);
    frame->set_dirty(false);

    frame->unlock_write();
  }
}

std::optional<size_t> pager::BufferPoolManager::get_pin_count(PageKey pgkey) {
  auto it = frame_table_.find(pgkey);
  if (it == frame_table_.end())
    return std::nullopt;

  pager::frame_id_t frame_id = it->second;
  assert(0 <= frame_id && frame_id < frames_.size());
  auto frame = frames_[frame_id];

  return frame->get_pins();
}

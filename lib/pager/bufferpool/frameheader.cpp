#include "pager/bufferpool/bufferpool.h"
#include <cstring>

pager::FrameHeader::FrameHeader(pager::frame_id_t frame_id)
    : dirty_(false), frame_id_(frame_id), data_(PAGE_SIZE, std::byte{0}),
      pins_(0) {}

pager::frame_id_t pager::FrameHeader::get_frame_id() const { return frame_id_; }

const std::vector<std::byte> &pager::FrameHeader::get_data() const {
  return data_;
}

bool pager::FrameHeader::is_dirty() { return dirty_; }

std::vector<std::byte> &pager::FrameHeader::get_data_mut() { return data_; }

void pager::FrameHeader::reset() {
  dirty_ = false;
  pins_ = 0;
  std::memset(data_.data(), 0, PAGE_SIZE);
}

void pager::FrameHeader::set_dirty(bool dirty) { dirty_ = dirty; }

void pager::FrameHeader::lock_read() { rwlatch_.lock_shared(); }
void pager::FrameHeader::unlock_read() { rwlatch_.unlock_shared(); }
void pager::FrameHeader::lock_write() { rwlatch_.lock(); }
void pager::FrameHeader::unlock_write() { rwlatch_.unlock(); }

size_t pager::FrameHeader::get_pins() const { return pins_.load(); }
size_t pager::FrameHeader::decr_pins() { return pins_.fetch_sub(1); }
size_t pager::FrameHeader::incr_pins() { return pins_.fetch_add(1); }

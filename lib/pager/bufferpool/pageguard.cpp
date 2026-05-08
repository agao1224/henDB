#include "pager/bufferpool/pageguard.h"
#include "pager/bufferpool/bufferpool.h"
#include "pager/bufferpool/evictor.h"
#include "shared.h"
#include <cassert>

pager::ReadPageGuard::ReadPageGuard(
    PageKey pgkey, std::shared_ptr<pager::FrameHeader> frame,
    std::shared_ptr<pager::Evictor> evictor,
    std::shared_ptr<std::mutex> bpm_latch,
    std::shared_ptr<storage::StorageEngine> storage_engine)
    : pgkey_(pgkey), frame_(frame), evictor_(evictor), bpm_latch_(bpm_latch),
      storage_engine_(storage_engine), is_valid(true) {
  frame_->incr_pins();
  evictor_->record_access(frame_->get_frame_id());
  frame_->lock_read();
}

pager::ReadPageGuard::~ReadPageGuard() {
  if (is_valid)
    drop();
}

pager::ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept
    : pgkey_(that.pgkey_), frame_(std::move(that.frame_)),
      evictor_(std::move(that.evictor_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      storage_engine_(std::move(that.storage_engine_)),
      is_valid(that.is_valid) {
  that.is_valid = false;
}

auto pager::ReadPageGuard::operator=(ReadPageGuard &&that) noexcept
    -> ReadPageGuard & {
  if (this != &that) {
    if (is_valid)
      drop();
    pgkey_ = that.pgkey_;
    frame_ = std::move(that.frame_);
    evictor_ = std::move(that.evictor_);
    bpm_latch_ = std::move(that.bpm_latch_);
    storage_engine_ = std::move(that.storage_engine_);
    is_valid = that.is_valid;
    that.is_valid = false;
  }
  return *this;
}

PageKey pager::ReadPageGuard::get_page_key() const { return pgkey_; }

const std::vector<std::byte> &pager::ReadPageGuard::get_data() const {
  assert(frame_ != nullptr);
  return frame_->get_data();
}

bool pager::ReadPageGuard::is_dirty() const {
  assert(frame_ != nullptr);
  return frame_->is_dirty();
}

void pager::ReadPageGuard::flush() {
  assert(frame_ != nullptr);
  assert(storage_engine_ != nullptr);
  frame_->unlock_read();
  frame_->lock_write();

  std::vector<std::byte> data = frame_->get_data();
  storage_engine_->write_page(pgkey_, data);
  frame_->set_dirty(false);

  frame_->unlock_write();
  frame_->lock_read();
  return;
}

void pager::ReadPageGuard::drop() {
  assert(frame_ != nullptr);
  assert(bpm_latch_ != nullptr);

  if (!is_valid)
    return;

  is_valid = false;
  bpm_latch_->lock();

  // NOTE(andrew): fetch_sub from std::atomic returns the value BEFORE the
  // update, so checking prev value = 1 is equivalent to checking current value
  // = 0
  size_t prev_num_pins = frame_->decr_pins();
  if (prev_num_pins == 1)
    evictor_->set_evictable(frame_->get_frame_id(), true);

  bpm_latch_->unlock();
  frame_->unlock_read();
}

pager::WritePageGuard::WritePageGuard(
    PageKey pgkey, std::shared_ptr<pager::FrameHeader> frame,
    std::shared_ptr<pager::Evictor> evictor,
    std::shared_ptr<std::mutex> bpm_latch,
    std::shared_ptr<storage::StorageEngine> storage_engine)
    : pgkey_(pgkey), frame_(frame), evictor_(evictor), bpm_latch_(bpm_latch),
      storage_engine_(storage_engine), is_valid(true) {
  frame_->incr_pins();
  evictor_->record_access(frame_->get_frame_id());
  frame_->lock_write();
}

pager::WritePageGuard::~WritePageGuard() {
  if (is_valid)
    drop();
}

pager::WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept
    : pgkey_(that.pgkey_), frame_(std::move(that.frame_)),
      evictor_(std::move(that.evictor_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      storage_engine_(std::move(that.storage_engine_)),
      is_valid(that.is_valid) {
  that.is_valid = false;
}

auto pager::WritePageGuard::operator=(WritePageGuard &&that) noexcept
    -> WritePageGuard & {
  if (this != &that) {
    if (is_valid)
      drop();
    pgkey_ = that.pgkey_;
    frame_ = std::move(that.frame_);
    evictor_ = std::move(that.evictor_);
    bpm_latch_ = std::move(that.bpm_latch_);
    storage_engine_ = std::move(that.storage_engine_);
    is_valid = that.is_valid;
    that.is_valid = false;
  }
  return *this;
}

PageKey pager::WritePageGuard::get_page_key() const { return pgkey_; }

const std::vector<std::byte> &pager::WritePageGuard::get_data() const {
  assert(frame_ != nullptr);
  return frame_->get_data();
}

std::vector<std::byte> &pager::WritePageGuard::get_data_mut() {
  assert(frame_ != nullptr);
  frame_->set_dirty(true);
  return frame_->get_data_mut();
}

bool pager::WritePageGuard::is_dirty() const {
  assert(frame_ != nullptr);
  return frame_->is_dirty();
}

void pager::WritePageGuard::flush() {
  assert(frame_ != nullptr);
  assert(storage_engine_ != nullptr);
  std::vector<std::byte> data = frame_->get_data();
  storage_engine_->write_page(pgkey_, data);
  frame_->set_dirty(false);
}

void pager::WritePageGuard::drop() {
  assert(frame_ != nullptr);
  assert(bpm_latch_ != nullptr);

  if (!is_valid)
    return;

  is_valid = false;
  bpm_latch_->lock();

  size_t prev_num_pins = frame_->decr_pins();
  if (prev_num_pins == 1)
    evictor_->set_evictable(frame_->get_frame_id(), true);

  bpm_latch_->unlock();
  frame_->unlock_write();
}

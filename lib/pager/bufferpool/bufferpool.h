#pragma once

#include "pager/bufferpool/pageguard.h"
#include "shared.h"
#include <atomic>
#include <list>
#include <map>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace storage {
class StorageEngine;
}

constexpr size_t EVICTOR_K = 2;

namespace pager {

using frame_id_t = uint32_t;
class Evictor;

class FrameHeader {
private:
  std::shared_mutex rwlatch_;
  bool dirty_;
  const pager::frame_id_t frame_id_;
  std::vector<std::byte> data_;
  std::atomic<size_t> pins_;

public:
  FrameHeader(pager::frame_id_t frame_id);
  ~FrameHeader() = default;

  pager::frame_id_t get_frame_id() const;
  const std::vector<std::byte> &get_data() const;
  std::vector<std::byte> &get_data_mut();
  void reset();
  void set_dirty(bool dirty);
  bool is_dirty();

  size_t get_pins() const;
  size_t incr_pins();
  size_t decr_pins();

  void lock_read();
  void unlock_read();
  void lock_write();
  void unlock_write();

  friend class ReadPageGuard;
  friend class WritePageGuard;
};

class BufferPoolManager {
private:
  const uint32_t num_frames_;
  std::shared_ptr<std::mutex> bpm_latch_;
  std::vector<std::shared_ptr<pager::FrameHeader>> frames_;
  std::map<PageKey, pager::frame_id_t> frame_table_;
  std::list<pager::frame_id_t> free_list_;
  std::shared_ptr<storage::StorageEngine> storage_engine_;
  std::shared_ptr<pager::Evictor> evictor_;

  pager::frame_id_t insert_frame(PageKey pgkey);
  bool ensure_free_frame();

public:
  BufferPoolManager(uint32_t num_frames,
                    storage::StorageEngine *storage_engine);
  ~BufferPoolManager();

  size_t size() const;
  std::optional<PageKey> new_page(TableID tbl_id);
  pager::ReadPageGuard read_page(PageKey pgkey);
  pager::WritePageGuard write_page(PageKey pgkey);
  bool evict_page(PageKey pgkey);

  bool flush_page(PageKey pgkey);
  void flush_all_pages();
  std::optional<size_t> get_pin_count(PageKey pgkey);
};

} // namespace pager

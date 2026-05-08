#pragma once

#include "shared.h"
#include "storage/storage.h"
#include <vector>

namespace pager {

class FrameHeader;
class Evictor;

class ReadPageGuard {
private:
  PageKey pgkey_;
  std::shared_ptr<pager::FrameHeader> frame_;
  std::shared_ptr<pager::Evictor> evictor_;
  std::shared_ptr<std::mutex> bpm_latch_;
  std::shared_ptr<storage::StorageEngine> storage_engine_;
  bool is_valid = false;

public:
  ReadPageGuard(PageKey pgkey, std::shared_ptr<pager::FrameHeader> frame,
                std::shared_ptr<pager::Evictor> evictor,
                std::shared_ptr<std::mutex> bpm_latch,
                std::shared_ptr<storage::StorageEngine> storage_engine);

  ReadPageGuard() = default;
  ~ReadPageGuard();

  ReadPageGuard(const ReadPageGuard &) = delete;
  auto operator=(const ReadPageGuard &) -> ReadPageGuard & = delete;
  ReadPageGuard(ReadPageGuard &&that) noexcept;
  auto operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard &;

  PageKey get_page_key() const;
  const std::vector<std::byte> &get_data() const;
  bool is_dirty() const;
  void flush();
  void drop();
};

class WritePageGuard {
private:
  PageKey pgkey_;
  std::shared_ptr<pager::FrameHeader> frame_;
  std::shared_ptr<pager::Evictor> evictor_;
  std::shared_ptr<std::mutex> bpm_latch_;
  std::shared_ptr<storage::StorageEngine> storage_engine_;
  bool is_valid = false;

public:
  WritePageGuard(PageKey pgkey, std::shared_ptr<pager::FrameHeader> frame,
                 std::shared_ptr<pager::Evictor> evictor,
                 std::shared_ptr<std::mutex> bpm_latch,
                 std::shared_ptr<storage::StorageEngine> storage_engine);

  WritePageGuard() = default;
  ~WritePageGuard();

  WritePageGuard(const WritePageGuard &) = delete;
  auto operator=(const WritePageGuard &) -> WritePageGuard & = delete;
  WritePageGuard(WritePageGuard &&that) noexcept;
  auto operator=(WritePageGuard &&that) noexcept -> WritePageGuard &;

  PageKey get_page_key() const;
  const std::vector<std::byte> &get_data() const;
  std::vector<std::byte> &get_data_mut();
  bool is_dirty() const;
  void flush();
  void drop();
};

} // namespace pager

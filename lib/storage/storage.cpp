#include "storage/storage.h"
#include "encoding.h"
#include "storage/storage_utils.h"
#include "vfs/localfs.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <vector>

void storage::StorageEngine::init_control() {
  assert(vfs_ != nullptr);

  if (!vfs_->exists(CONTROL_FILE)) {
    auto control_file = vfs_->open(CONTROL_FILE, O_RDWR | O_CREAT);
    std::vector<std::byte> buffer;
    encoding::append_uint32(buffer, CHECKSUM);
    encoding::append_uint64(buffer, DEFAULT_CATALOG_TABLE);
    encoding::append_uint64(buffer, DEFAULT_CATALOG_PGNO);
    control_file->write(&buffer, buffer.size(), 0);
    return;
  }

  auto control_file = vfs_->open(CONTROL_FILE, O_RDWR);
  std::vector<std::byte> buffer(storage::Control::SIZE);
  control_file->read(buffer, buffer.size(), 0);

  size_t offset = 0;
  uint32_t control_checksum = encoding::read_uint32(buffer, offset);
  if (control_checksum == CHECKSUM) {
    TableID catalog_tbl_id = encoding::read_uint64(buffer, offset);
    PageNumber catalog_pgno = encoding::read_uint64(buffer, offset);
    control_.catalog_key = PageKey(catalog_tbl_id, catalog_pgno);
  }
  return;
}

void storage::StorageEngine::init_table_metadata() {
  assert(vfs_ != nullptr);

  std::vector<std::string> paths = vfs_->ls("");
  for (auto path : paths) {
    if (!vfs_->is_dir(path))
      continue;

    std::vector<std::string> segments = vfs_->ls(path);
    assert(segments.size() == 1);

    uint64_t full_segments = segments.size() - 1;
    uint64_t last_seg_pages =
        vfs_->filesize(segments.back()) / config_.page_size;
    PageNumber total_pages =
        full_segments * config_.segment_size + last_seg_pages;
    storage::TableMetadata metadata(total_pages - 1, total_pages);
    TableID tbl_id = storage::path_to_tbl_id(path);
    table_metadata_[tbl_id] = metadata;
  }
}

VirtualFile *storage::StorageEngine::open_segment(TableID tbl_id,
                                                  storage::SegmentID seg_id) {
  const std::string path = storage::seg_path(tbl_id, seg_id);

  storage::FileKey key(tbl_id, seg_id);
  if (filecache_.exists(key))
    return filecache_.get(key);

  std::unique_ptr<VirtualFile> file = vfs_->open(path, O_RDWR | O_CREAT);
  filecache_.put(key, std::move(file));
  return filecache_.get(key);
}

VirtualFile *storage::StorageEngine::get_segment(TableID tbl_id,
                                                 PageNumber pgno) {
  storage::SegmentID seg_id =
      storage::pgno_to_segid(pgno, config_.segment_size);
  assert(vfs_->exists(storage::tbl_path(tbl_id)) &&
         vfs_->exists(storage::seg_path(tbl_id, seg_id)));
  VirtualFile *seg = open_segment(tbl_id, seg_id);
  if (seg == nullptr)
    throw std::runtime_error(
        "[StorageEngine]: Failed to open segment for tbl=" +
        std::to_string(tbl_id) + " pgno=" + std::to_string(pgno));
  return seg;
}

PageNumber storage::StorageEngine::get_latest_page(TableID tbl_id) {
  assert(vfs_ != nullptr);
  auto it = table_metadata_.find(tbl_id);
  PageNumber latest_page;
  if (it == table_metadata_.end()) {
    storage::SegmentID seg_id = storage::pgno_to_segid(
        DEFAULT_TABLE_METADATA_PGNO, config_.segment_size);

    VirtualFile *first_segmt = open_segment(tbl_id, seg_id);
    assert(first_segmt != nullptr);
    std::vector<std::byte> buffer;
    first_segmt->read(buffer, config_.page_size, 0);

    size_t offset = 0;
    storage::TableMetadata metadata(buffer, offset);
    assert(offset == buffer.size());

    table_metadata_[tbl_id] = metadata;
    latest_page = metadata.last_pgno;
  } else {
    latest_page = (it->second).last_pgno;
  }
  return latest_page;
}

storage::StorageEngine::StorageEngine(const std::string &basepath,
                                      const storage::EngineConfig config) {
  config_ = config;
  vfs_ = std::make_unique<LocalFS>(basepath);
  init_control();
  init_table_metadata();
}

storage::StorageEngine
storage::StorageEngine::open(const std::string &basepath,
                             const storage::EngineConfig config) {
  return StorageEngine(basepath, config);
}

void storage::StorageEngine::read_page(PageKey pgkey,
                                       std::vector<std::byte> &buffer) {
  assert(vfs_ != nullptr);
  TableID tbl_id = pgkey.first;
  PageNumber pgno = pgkey.second;

  if (pgno > get_latest_page(tbl_id))
    throw std::runtime_error(
        "[StorageEngine:read_page]: Attempted to read past last page");

  VirtualFile *seg = get_segment(tbl_id, pgno);
  uint64_t offset = storage::pgno_to_file_offset(pgno, config_.segment_size,
                                                 config_.page_size);
  seg->read(buffer, config_.page_size, offset);
  return;
}

void storage::StorageEngine::write_page(PageKey pgkey,
                                        std::vector<std::byte> &buffer) {
  assert(vfs_ != nullptr);
  TableID tbl_id = pgkey.first;
  PageNumber pgno = pgkey.second;

  if (pgno > get_latest_page(tbl_id))
    throw std::runtime_error(
        "[StorageEngine:write_page]: Attempted to write past last page");

  VirtualFile *seg = get_segment(tbl_id, pgno);
  uint64_t offset = storage::pgno_to_file_offset(pgno, config_.segment_size,
                                                 config_.page_size);
  seg->write(&buffer, config_.page_size, offset);
  return;
}

PageKey storage::StorageEngine::allocate_page(TableID tbl_id) {
  assert(vfs_ != nullptr);

  const std::string dir = storage::tbl_path(tbl_id);
  assert(vfs_->exists(dir));

  PageNumber latest_page = get_latest_page(tbl_id);
  latest_page++;

  storage::SegmentID seg_id =
      storage::pgno_to_segid(latest_page, config_.segment_size);
  VirtualFile *segmt = open_segment(tbl_id, seg_id);
  uint64_t offset = storage::pgno_to_file_offset(
      latest_page, config_.segment_size, config_.page_size);
  segmt->write(nullptr, config_.page_size, offset);

  table_metadata_[tbl_id].last_pgno++;
  table_metadata_[tbl_id].total_pages++;
  assert(latest_page == table_metadata_[tbl_id].last_pgno);
  return PageKey(tbl_id, latest_page);
}

void storage::StorageEngine::create_table(TableID tbl_id) {
  assert(vfs_ != nullptr);

  const std::string dir = storage::tbl_path(tbl_id);
  if (vfs_->exists(dir)) {
    throw std::runtime_error(
        "[StorageEngine:create_table]: Table already exists " + dir);
  }

  vfs_->mkdir(dir);
  storage::SegmentID seg_id =
      storage::pgno_to_segid(DEFAULT_TABLE_METADATA_PGNO, config_.segment_size);
  VirtualFile *first_segmt = open_segment(tbl_id, seg_id);

  if (first_segmt == nullptr) {
    throw std::runtime_error(
        "[StorageEngine:create_table]: Failed to create first segment for " +
        dir);
  }

  storage::TableMetadata metadata(DEFAULT_TABLE_METADATA_PGNO,
                                  DEFAULT_TABLE_METADATA_PGNO + 1);
  table_metadata_[tbl_id] = metadata;
  std::vector<std::byte> buffer = metadata.to_bytes();
  first_segmt->write(&buffer, config_.page_size, 0);
  return;
}

void storage::StorageEngine::flush_table(TableID tbl_id) {
  assert(vfs_ != nullptr);

  if (table_metadata_.find(tbl_id) != table_metadata_.end()) {
    storage::SegmentID seg_id = storage::pgno_to_segid(
        DEFAULT_TABLE_METADATA_PGNO, config_.segment_size);
    const std::string tbl_metadata_path = storage::seg_path(tbl_id, seg_id);
    assert(vfs_->exists(tbl_metadata_path));

    VirtualFile *segmt = open_segment(tbl_id, seg_id);

    storage::TableMetadata metadata = table_metadata_[tbl_id];
    std::vector<std::byte> buffer = metadata.to_bytes();
    uint64_t offset = storage::pgno_to_file_offset(
        DEFAULT_TABLE_METADATA_PGNO, config_.segment_size, config_.page_size);
    segmt->write(&buffer, storage::TableMetadata::SIZE, offset);
  }

  std::vector<std::string> seg_files = vfs_->ls(storage::tbl_path(tbl_id));
  for (auto &seg_path : seg_files) {
    storage::SegmentID seg_id = storage::path_to_seg_id(seg_path);
    VirtualFile *seg = open_segment(tbl_id, seg_id);
    seg->sync();
  }
  return;
}

void storage::StorageEngine::truncate_table(TableID tbl_id) {
  assert(vfs_ != nullptr);
  std::vector<std::string> segfiles = vfs_->ls(storage::tbl_path(tbl_id));
  for (auto segfile : segfiles)
    vfs_->unlink(segfile);

  assert(vfs_->ls(storage::tbl_path(tbl_id)).size() == 0);
  return;
}

void storage::StorageEngine::delete_table(TableID tbl_id) {
  truncate_table(tbl_id);
  const std::string tbl_dir = storage::tbl_path(tbl_id);
  vfs_->rmdir(tbl_dir);
  assert(!vfs_->exists(tbl_dir));
  return;
}

void storage::StorageEngine::shutdown() {
  for (auto const &[table, _] : table_metadata_)
    flush_table(table);
}

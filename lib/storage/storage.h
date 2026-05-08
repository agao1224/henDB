#pragma once

#include "encoding.h"
#include "filecache.h"
#include "shared.h"
#include "vfs/vfs.h"
#include <cstdint>
#include <map>
#include <string>

static constexpr uint64_t DEFAULT_PAGES_PER_SEGMENT = 131072;
static constexpr uint64_t DEFAULT_PAGE_SIZE = 4096;
static constexpr PageNumber DEFAULT_TABLE_METADATA_PGNO = 0;
static const std::string CONTROL_FILE = "db.control";

static const TableID DEFAULT_CATALOG_TABLE = 0;
static const PageNumber DEFAULT_CATALOG_PGNO = 0;

namespace storage {

struct EngineConfig {
  uint64_t segment_size = DEFAULT_PAGES_PER_SEGMENT;
  uint64_t page_size = DEFAULT_PAGE_SIZE;
};

struct Control {
  uint32_t checksum;
  PageKey catalog_key;

  static constexpr size_t SIZE = sizeof(uint32_t) + 2 * sizeof(uint64_t);

  const std::vector<std::byte> to_bytes() const {
    std::vector<std::byte> buffer;
    encoding::append_uint32(buffer, checksum);
    encoding::append_uint64(buffer, static_cast<uint64_t>(catalog_key.first));
    encoding::append_uint64(buffer, static_cast<uint64_t>(catalog_key.second));
    return buffer;
  }

  Control() = default;

  Control(uint32_t checksum_, PageKey catalog_key_)
      : checksum(checksum_), catalog_key(catalog_key_) {}

  Control(std::vector<std::byte> &buffer, size_t &offset) {
    checksum = encoding::read_uint32(buffer, offset);
    TableID tbl_id =
        static_cast<TableID>(encoding::read_uint64(buffer, offset));
    PageNumber pgno =
        static_cast<PageNumber>(encoding::read_uint64(buffer, offset));
    catalog_key = std::make_pair(tbl_id, pgno);
  }
};

struct TableMetadata {
  PageNumber last_pgno;
  PageNumber total_pages;

  static constexpr size_t SIZE = sizeof(PageNumber) + sizeof(PageNumber);

  const std::vector<std::byte> to_bytes() const {
    std::vector<std::byte> buffer;
    encoding::append_uint64(buffer, last_pgno);
    encoding::append_uint64(buffer, total_pages);
    return buffer;
  }

  TableMetadata() = default;

  TableMetadata(PageNumber last_pgno_, PageNumber total_pages_)
      : last_pgno(last_pgno_), total_pages(total_pages_) {}

  TableMetadata(std::vector<std::byte> &buffer, size_t &offset) {
    last_pgno = static_cast<PageNumber>(encoding::read_uint64(buffer, offset));
    total_pages =
        static_cast<PageNumber>(encoding::read_uint64(buffer, offset));
  }
};

class StorageEngine {
private:
  std::unique_ptr<VFS> vfs_;
  storage::EngineConfig config_;
  storage::Control control_;
  std::map<TableID, storage::TableMetadata> table_metadata_;
  storage::FileCache filecache_;

  PageNumber get_latest_page(TableID tbl_id);

  void init_control();
  void init_table_metadata();
  VirtualFile *open_segment(TableID tbl_id, storage::SegmentID seg_id);
  VirtualFile *get_segment(TableID tbl_id, PageNumber pgno);

  StorageEngine(const std::string &basepath, const storage::EngineConfig = {});

public:
  static StorageEngine open(const std::string &basepath,
                            const storage::EngineConfig config = {});

  void read_page(PageKey pgkey, std::vector<std::byte> &buffer);
  void write_page(PageKey pgkey, std::vector<std::byte> &buffer);
  PageKey allocate_page(TableID tbl_id);

  void create_table(TableID tbl_id);
  void flush_table(TableID tbl_id);
  void truncate_table(TableID tbl_id);
  void delete_table(TableID tbl_id);
  void shutdown();
};

} // namespace storage

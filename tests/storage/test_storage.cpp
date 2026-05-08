#include "storage/storage.h"
#include "storage_utils.h"
#include "test_utils.h"
#include "vfs/localfs.h"
#include "vfs/vfs.h"
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <gtest/gtest.h>

class StorageEngineTest : public ::testing::Test {
protected:
  std::string basedir;

  void SetUp() override {
    basedir =
        (std::filesystem::temp_directory_path() / generate_random_dirname())
            .string();
    std::filesystem::create_directories(basedir);
  }

  void TearDown() override { std::filesystem::remove_all(basedir); }
};

TEST_F(StorageEngineTest, InitControl) {
  storage::StorageEngine storage = storage::StorageEngine::open(basedir);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);
  ASSERT_TRUE(fs != nullptr);

  bool control_file_exists = fs->exists(CONTROL_FILE);
  ASSERT_TRUE(control_file_exists);

  std::vector<std::byte> buffer;
  std::unique_ptr<VirtualFile> control_file =
      fs->open(CONTROL_FILE, O_RDWR | O_CREAT);
  ASSERT_TRUE(control_file != nullptr);

  control_file->read(buffer, storage::Control::SIZE, 0);
  size_t offset = 0;
  storage::Control control(buffer, offset);
  ASSERT_EQ(control.checksum, CHECKSUM);
  ASSERT_EQ(control.catalog_key.first, DEFAULT_CATALOG_TABLE);
  ASSERT_EQ(control.catalog_key.second, DEFAULT_CATALOG_PGNO);
}

TEST_F(StorageEngineTest, CreateTableBasic) {
  storage::StorageEngine storage = storage::StorageEngine::open(basedir);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);
  ASSERT_TRUE(fs != nullptr);

  TableID tbl_id = 1;
  storage::SegmentID seg_id = 0;
  storage.create_table(tbl_id);

  ASSERT_TRUE(fs->exists(storage::tbl_path(tbl_id)));
  const std::string segpath = storage::seg_path(tbl_id, seg_id);
  ASSERT_TRUE(fs->exists(segpath));
}

TEST_F(StorageEngineTest, AllocatePageBasic) {
  storage::StorageEngine storage = storage::StorageEngine::open(basedir);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);

  TableID tbl_id = 1;
  storage::SegmentID seg_id = 0;
  storage.create_table(tbl_id);
  ASSERT_TRUE(fs->exists(storage::tbl_path(tbl_id)));

  PageKey key = storage.allocate_page(tbl_id);
  std::vector<std::byte> buffer;
  storage.read_page(key, buffer);

  ASSERT_EQ(buffer.size(), PAGE_SIZE);
  const std::string segpath = storage::seg_path(tbl_id, seg_id);
  ASSERT_TRUE(fs->exists(segpath));

  std::unique_ptr first_segmt = fs->open(segpath, O_RDWR | O_CREAT);
  ASSERT_TRUE(first_segmt != nullptr);
  ASSERT_EQ(fs->filesize(segpath), PAGE_SIZE * 2);

  std::vector<std::byte> pagebuffer;
  first_segmt->read(pagebuffer, storage::TableMetadata::SIZE, 0);
  size_t offset = 0;
  storage::TableMetadata metadata(pagebuffer, offset);
  ASSERT_EQ(metadata.last_pgno, 0);
  ASSERT_EQ(metadata.total_pages, 1);
}

TEST_F(StorageEngineTest, AllocatePageFlush) {
  storage::StorageEngine storage = storage::StorageEngine::open(basedir);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);

  TableID tbl_id = 1;
  storage::SegmentID seg_id = 0;
  storage.create_table(tbl_id);
  ASSERT_TRUE(fs->exists(storage::tbl_path(tbl_id)));

  PageKey key = storage.allocate_page(tbl_id);
  std::vector<std::byte> buffer;
  storage.read_page(key, buffer);

  ASSERT_EQ(buffer.size(), PAGE_SIZE);
  const std::string segpath = storage::seg_path(tbl_id, seg_id);
  ASSERT_TRUE(fs->exists(segpath));

  std::unique_ptr first_segmt = fs->open(segpath, O_RDWR | O_CREAT);
  ASSERT_TRUE(first_segmt != nullptr);
  ASSERT_EQ(fs->filesize(segpath), PAGE_SIZE * 2);

  std::vector<std::byte> pagebuffer;
  first_segmt->read(pagebuffer, storage::TableMetadata::SIZE, 0);
  size_t offset = 0;
  storage::TableMetadata metadata(pagebuffer, offset);
  ASSERT_EQ(metadata.last_pgno, 0);
  ASSERT_EQ(metadata.total_pages, 1);

  storage.flush_table(tbl_id);
  first_segmt->read(pagebuffer, storage::TableMetadata::SIZE, 0);
  offset = 0;
  metadata = storage::TableMetadata(pagebuffer, offset);
  ASSERT_EQ(metadata.last_pgno, 1);
  ASSERT_EQ(metadata.total_pages, 2);
}

TEST_F(StorageEngineTest, WritePageBasic) {
  storage::StorageEngine storage = storage::StorageEngine::open(basedir);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);

  TableID tbl_id = 1;
  storage::SegmentID seg_id = 0;
  storage.create_table(tbl_id);
  ASSERT_TRUE(fs->exists(storage::tbl_path(tbl_id)));

  PageKey key = storage.allocate_page(tbl_id);
  std::vector<std::byte> buffer = generate_random_payload(PAGE_SIZE);
  storage.write_page(key, buffer);

  const std::string segpath = storage::seg_path(tbl_id, seg_id);
  ASSERT_TRUE(fs->exists(segpath));

  std::unique_ptr first_segmt = fs->open(segpath, O_RDWR | O_CREAT);
  ASSERT_TRUE(first_segmt != nullptr);
  ASSERT_EQ(fs->filesize(segpath), PAGE_SIZE * 2);

  std::vector<std::byte> readbuffer;
  first_segmt->read(readbuffer, PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(readbuffer, buffer);
}

TEST_F(StorageEngineTest, WritePagesMany) {
  size_t pages_per_segmt = 16;
  storage::EngineConfig config = {.segment_size = pages_per_segmt,
                                  .page_size = DEFAULT_PAGE_SIZE};
  storage::StorageEngine storage =
      storage::StorageEngine::open(basedir, config);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);

  TableID tbl_id = 1;
  storage.create_table(tbl_id);

  size_t num_pages = (1 << 10);
  for (size_t i = 0; i < num_pages; i++) {
    PageKey key = storage.allocate_page(tbl_id);
    std::vector<std::byte> buffer = generate_random_payload(PAGE_SIZE);
    storage.write_page(key, buffer);
  }
  storage.flush_table(tbl_id);

  uint64_t expected_num_segments = ((num_pages + 1) / pages_per_segmt);
  if ((num_pages + 1) % pages_per_segmt > 0)
    expected_num_segments++;

  const std::string tbl_path = storage::tbl_path(tbl_id);
  std::vector<std::string> segments = fs->ls(tbl_path);
  ASSERT_EQ(segments.size(), expected_num_segments);

  size_t total_bytes = 0;
  for (auto segment : segments)
    total_bytes += fs->filesize(segment);
  ASSERT_EQ((num_pages + 1) * PAGE_SIZE, total_bytes);

  storage::SegmentID seg_id =
      storage::pgno_to_segid(DEFAULT_TABLE_METADATA_PGNO, pages_per_segmt);
  const std::string segpath = storage::seg_path(tbl_id, seg_id);
  PageNumber rel_pgno =
      storage::pgno_to_rel_pgno(DEFAULT_TABLE_METADATA_PGNO, pages_per_segmt);
  size_t offset = storage::pgno_to_offset(rel_pgno, PAGE_SIZE);

  std::unique_ptr segmt = fs->open(segpath, O_RDWR | O_CREAT);
  std::vector<std::byte> buffer;
  segmt->read(buffer, storage::TableMetadata::SIZE, offset);

  offset = 0;
  storage::TableMetadata metadata(buffer, offset);
  ASSERT_EQ(metadata.last_pgno, num_pages);
  ASSERT_EQ(metadata.total_pages, num_pages + 1);
}

TEST_F(StorageEngineTest, ReadWritePageEdgeCases) {
  size_t pages_per_segmt = 4;
  storage::EngineConfig config = {.segment_size = pages_per_segmt,
                                  .page_size = DEFAULT_PAGE_SIZE};
  storage::StorageEngine storage =
      storage::StorageEngine::open(basedir, config);

  TableID tbl_id = 1;
  storage.create_table(tbl_id);

  size_t num_pages = 8;
  std::vector<PageKey> keys;
  std::vector<std::vector<std::byte>> payloads;
  for (size_t i = 0; i < num_pages; i++) {
    PageKey key = storage.allocate_page(tbl_id);
    std::vector<std::byte> buf = generate_random_payload(PAGE_SIZE);
    storage.write_page(key, buf);
    keys.push_back(key);
    payloads.push_back(buf);
  }

  for (int i = num_pages - 1; i >= 0; i--) {
    std::vector<std::byte> buf;
    storage.read_page(keys[i], buf);
    ASSERT_EQ(buf, payloads[i]);
  }

  for (size_t i = 0; i < num_pages; i += 2) {
    payloads[i] = generate_random_payload(PAGE_SIZE);
    storage.write_page(keys[i], payloads[i]);
  }
  for (size_t i = 0; i < num_pages; i++) {
    std::vector<std::byte> buf;
    storage.read_page(keys[i], buf);
    ASSERT_EQ(buf, payloads[i]);
  }

  TableID tbl_id2 = 2;
  storage.create_table(tbl_id2);
  std::vector<PageKey> keys2;
  std::vector<std::vector<std::byte>> payloads2;
  for (size_t i = 0; i < num_pages; i++) {
    PageKey key = storage.allocate_page(tbl_id2);
    std::vector<std::byte> buf = generate_random_payload(PAGE_SIZE);
    storage.write_page(key, buf);
    keys2.push_back(key);
    payloads2.push_back(buf);
  }
  for (size_t i = 0; i < num_pages; i++) {
    std::vector<std::byte> buf;
    storage.read_page(keys[i], buf);
    ASSERT_EQ(buf, payloads[i]);
    storage.read_page(keys2[i], buf);
    ASSERT_EQ(buf, payloads2[i]);
  }
}

TEST_F(StorageEngineTest, TruncateTable) {
  storage::StorageEngine storage = storage::StorageEngine::open(basedir);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);

  TableID tbl_id = 1;
  storage.create_table(tbl_id);

  for (size_t i = 0; i < 4; i++) {
    PageKey key = storage.allocate_page(tbl_id);
    std::vector<std::byte> buffer = generate_random_payload(PAGE_SIZE);
    storage.write_page(key, buffer);
  }

  const std::string tbl_path = storage::tbl_path(tbl_id);
  ASSERT_TRUE(fs->exists(tbl_path));
  ASSERT_FALSE(fs->ls(tbl_path).empty());

  storage.truncate_table(tbl_id);

  ASSERT_TRUE(fs->exists(tbl_path));
  ASSERT_TRUE(fs->ls(tbl_path).empty());
}

TEST_F(StorageEngineTest, DeleteTable) {
  storage::StorageEngine storage = storage::StorageEngine::open(basedir);
  std::unique_ptr fs = std::make_unique<LocalFS>(basedir);

  TableID tbl_id = 1;
  storage.create_table(tbl_id);

  for (size_t i = 0; i < 4; i++) {
    PageKey key = storage.allocate_page(tbl_id);
    std::vector<std::byte> buffer = generate_random_payload(PAGE_SIZE);
    storage.write_page(key, buffer);
  }

  const std::string tbl_path = storage::tbl_path(tbl_id);
  ASSERT_TRUE(fs->exists(tbl_path));

  storage.delete_table(tbl_id);

  ASSERT_FALSE(fs->exists(tbl_path));
}

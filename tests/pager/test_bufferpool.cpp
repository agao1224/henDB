#include "pager/bufferpool/bufferpool.h"
#include "storage/storage.h"
#include "test_utils.h"
#include <cstddef>
#include <filesystem>
#include <gtest/gtest.h>
#include <set>
#include <thread>
#include <vector>

class BufferPoolTest : public ::testing::Test {
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

TEST_F(BufferPoolTest, InitBufferPoolManager) {
  TableID tid = 1;
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));
  ASSERT_TRUE(storage != nullptr);
  storage->create_table(tid);

  pager::BufferPoolManager manager(20, storage);
  ASSERT_EQ(manager.size(), 0);
}

TEST_F(BufferPoolTest, ConcurrentNewPages) {
  TableID tid = 1;
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));
  storage->create_table(tid);

  pager::BufferPoolManager manager(20, storage);

  std::vector<std::thread> threads;
  std::set<PageKey> keyset;

  auto create_page = [&manager, &tid, &keyset]() {
    std::optional<PageKey> pgkey = manager.new_page(tid);
    ASSERT_NE(pgkey, std::nullopt);
    ASSERT_TRUE(keyset.find(pgkey.value()) == keyset.end());
    keyset.insert(pgkey.value());
  };

  for (int i = 0; i < 20; i++) {
    std::thread t(create_page);
    threads.push_back(std::move(t));
  }

  for (int i = 0; i < threads.size(); i++)
    threads[i].join();
}

TEST_F(BufferPoolTest, ReadPages) {
  TableID tid = 1;
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));

  storage->create_table(tid);

  pager::BufferPoolManager manager(20, storage);
  std::optional<PageKey> k1 = manager.new_page(tid);
  std::optional<PageKey> k2 = manager.new_page(tid);

  ASSERT_NE(k1, std::nullopt);
  ASSERT_NE(k2, std::nullopt);

  pager::ReadPageGuard rg1 = manager.read_page(k1.value());
  pager::ReadPageGuard rg2 = manager.read_page(k2.value());

  const std::vector<std::byte> expected(PAGE_SIZE, std::byte{0});

  ASSERT_TRUE(rg1.get_data() == expected);
  ASSERT_TRUE(rg2.get_data() == expected);
}

TEST_F(BufferPoolTest, ConcurrentWritePages) {
  TableID tid = 1;
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));

  storage->create_table(tid);

  pager::BufferPoolManager manager(20, storage);
  ASSERT_NE(storage, nullptr);

  auto write_and_read_page = [&manager](PageKey pgkey,
                                        std::vector<std::byte> payload) {
    pager::WritePageGuard guard = manager.write_page(pgkey);
    auto &data_mut = guard.get_data_mut();
    std::memcpy(data_mut.data(), payload.data(), PAGE_SIZE);

    ASSERT_TRUE(guard.get_data() == payload);
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 20; i++) {
    std::optional<PageKey> key = manager.new_page(tid);
    std::vector<std::byte> payload = generate_random_payload(PAGE_SIZE);
    std::thread t(write_and_read_page, key.value(), payload);
    threads.push_back(std::move(t));
  }

  for (int i = 0; i < threads.size(); i++)
    threads[i].join();
}

TEST_F(BufferPoolTest, FlushPage) {
  TableID tid = 1;
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));

  storage->create_table(tid);

  pager::BufferPoolManager manager(20, storage);
  std::optional<PageKey> pgkey = manager.new_page(tid);
  ASSERT_TRUE(pgkey.has_value());

  pager::WritePageGuard guard = manager.write_page(pgkey.value());
  auto payload = generate_random_payload(PAGE_SIZE);

  auto &data_mut = guard.get_data_mut();
  std::memcpy(data_mut.data(), payload.data(), PAGE_SIZE);

  std::vector<std::byte> buffer;
  storage->read_page(pgkey.value(), buffer);

  std::vector<std::byte> empty_buffer(PAGE_SIZE, std::byte{0});
  ASSERT_TRUE(empty_buffer == buffer);

  guard.flush();

  storage->read_page(pgkey.value(), buffer);
  ASSERT_TRUE(payload == buffer);
}

TEST_F(BufferPoolTest, FlushAllPages) {
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));

  std::vector<PageKey> page_keys;
  std::vector<std::vector<std::byte>> expected_payloads;
  pager::BufferPoolManager manager(20, storage);
  for (TableID tid = 0; tid < 10; tid++) {
    storage->create_table(tid);
    auto pgkey = manager.new_page(tid);
    ASSERT_TRUE(pgkey.has_value());

    auto write_guard = manager.write_page(pgkey.value());
    auto &data_mut = write_guard.get_data_mut();
    auto payload = generate_random_payload(PAGE_SIZE);

    std::memcpy(data_mut.data(), payload.data(), PAGE_SIZE);

    std::vector<std::byte> empty_buffer(PAGE_SIZE, std::byte{0});
    std::vector<std::byte> buffer;
    storage->read_page(pgkey.value(), buffer);

    page_keys.push_back(pgkey.value());
    expected_payloads.push_back(payload);

    ASSERT_TRUE(buffer == empty_buffer);
  }
  ASSERT_EQ(page_keys.size(), expected_payloads.size());

  manager.flush_all_pages();

  for (int i = 0; i < page_keys.size(); i++) {
    std::vector<std::byte> buffer;
    storage->read_page(page_keys[i], buffer);
    ASSERT_EQ(buffer, expected_payloads[i]);
  }
}

TEST_F(BufferPoolTest, CannotEvictUsedPages) {
  TableID tid = 1;
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));

  storage->create_table(tid);

  pager::BufferPoolManager manager(20, storage);
  std::vector<PageKey> page_keys;
  std::vector<pager::ReadPageGuard> guards;
  for (int i = 0; i < 10; i++) {
    auto pgkey = manager.new_page(tid);
    ASSERT_TRUE(pgkey.has_value());
    page_keys.push_back(pgkey.value());
    guards.push_back(manager.read_page(pgkey.value()));
  }

  for (auto pgkey : page_keys)
    ASSERT_FALSE(manager.evict_page(pgkey));

  for (int i = 0; i < guards.size(); i++)
    guards[i].drop();

  for (auto pgkey : page_keys)
    ASSERT_TRUE(manager.evict_page(pgkey));
}

TEST_F(BufferPoolTest, PinCount) {
  TableID tid = 1;
  storage::StorageEngine *storage =
      new storage::StorageEngine(storage::StorageEngine::open(basedir));

  storage->create_table(tid);
  pager::BufferPoolManager manager(20, storage);

  auto pgkey = manager.new_page(tid);
  ASSERT_TRUE(pgkey.has_value());
  std::vector<pager::ReadPageGuard> guards;
  for (int i = 0; i < 10; i++)
    guards.push_back(manager.read_page(pgkey.value()));

  ASSERT_EQ(manager.get_pin_count(pgkey.value()), 10);
}

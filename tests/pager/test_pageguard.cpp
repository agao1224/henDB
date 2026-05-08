#include "pager/bufferpool/bufferpool.h"
#include "pager/bufferpool/evictor.h"
#include "pager/bufferpool/pageguard.h"
#include "storage/storage.h"
#include "test_utils.h"
#include <cstddef>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>

class PageGuardTest : public ::testing::Test {
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

TEST_F(PageGuardTest, InitReadPageGuard) {
  auto frame = std::make_shared<pager::FrameHeader>(0);
  auto evictor = std::make_shared<pager::LRUKEvictor>(2, 10);
  auto bpm_latch = std::make_shared<std::mutex>();
  auto storage_engine = std::make_shared<storage::StorageEngine>(
      storage::StorageEngine::open(basedir));

  PageKey key(1, 1);
  pager::ReadPageGuard guard(key, frame, evictor, bpm_latch, storage_engine);
  ASSERT_EQ(guard.get_page_key(), key);
  ASSERT_EQ(guard.is_dirty(), false);

  const std::vector<std::byte> &data = guard.get_data();
  std::vector<std::byte> expected(PAGE_SIZE, std::byte{0});
  ASSERT_TRUE(data == expected);
}

TEST_F(PageGuardTest, InitWritePageGuard) {
  auto frame = std::make_shared<pager::FrameHeader>(0);
  auto evictor = std::make_shared<pager::LRUKEvictor>(2, 10);
  auto bpm_latch = std::make_shared<std::mutex>();
  auto storage_engine = std::make_shared<storage::StorageEngine>(
      storage::StorageEngine::open(basedir));

  PageKey key(1, 1);
  pager::WritePageGuard guard(key, frame, evictor, bpm_latch, storage_engine);
  ASSERT_EQ(guard.is_dirty(), false);
  ASSERT_EQ(guard.get_page_key(), key);

  std::vector<std::byte> &data = guard.get_data_mut();
  std::vector<std::byte> expected(PAGE_SIZE, std::byte{0});
  ASSERT_TRUE(data == expected);
}

TEST_F(PageGuardTest, ConcurrentReadPageGuards) {
  auto frame = std::make_shared<pager::FrameHeader>(0);
  auto evictor = std::make_shared<pager::LRUKEvictor>(2, 10);
  auto bpm_latch = std::make_shared<std::mutex>();
  auto storage_engine = std::make_shared<storage::StorageEngine>(
      storage::StorageEngine::open(basedir));

  PageKey pgkey(1, 1);
  std::vector<std::byte> &buffer = frame->get_data_mut();

  auto payload = generate_random_payload(PAGE_SIZE);
  std::memcpy(buffer.data(), payload.data(), PAGE_SIZE);

  auto read_buffer = [&pgkey, &frame, &evictor, &bpm_latch,
                      &storage_engine](std::vector<std::byte> expected) {
    pager::ReadPageGuard guard(pgkey, frame, evictor, bpm_latch,
                               storage_engine);
    const std::vector<std::byte> &data = guard.get_data();
    ASSERT_TRUE(data == expected);
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++) {
    std::thread t(read_buffer, payload);
    threads.push_back(std::move(t));
  }

  for (int i = 0; i < threads.size(); i++)
    threads[i].join();
}

TEST_F(PageGuardTest, ConcurrentWritePageGuards) {
  auto frame = std::make_shared<pager::FrameHeader>(0);
  auto evictor = std::make_shared<pager::LRUKEvictor>(2, 10);
  auto bpm_latch = std::make_shared<std::mutex>();
  auto storage_engine = std::make_shared<storage::StorageEngine>(
      storage::StorageEngine::open(basedir));

  PageKey pgkey(1, 1);
  auto write_buffer = [&pgkey, &frame, &evictor, &bpm_latch,
                       &storage_engine]() {
    pager::WritePageGuard guard(pgkey, frame, evictor, bpm_latch,
                                storage_engine);
    auto new_payload = generate_random_payload(PAGE_SIZE);
    std::vector<std::byte> &data_mut = guard.get_data_mut();
    ASSERT_TRUE(data_mut != new_payload);

    std::memcpy(data_mut.data(), new_payload.data(), PAGE_SIZE);

    const std::vector<std::byte> &reloaded = guard.get_data();
    ASSERT_TRUE(reloaded == new_payload);
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++)
    threads.push_back(std::thread(write_buffer));

  for (int i = 0; i < threads.size(); i++)
    threads[i].join();
}

TEST_F(PageGuardTest, ConcurrentRWPageGuards) {
  auto frame = std::make_shared<pager::FrameHeader>(0);
  auto evictor = std::make_shared<pager::LRUKEvictor>(2, 10);
  auto bpm_latch = std::make_shared<std::mutex>();
  auto storage_engine = std::make_shared<storage::StorageEngine>(
      storage::StorageEngine::open(basedir));

  PageKey pgkey(1, 1);
  storage_engine->create_table(pgkey.first);
  storage_engine->allocate_page(pgkey.first);
  auto write_buffer = [&pgkey, &frame, &evictor, &bpm_latch,
                       &storage_engine](std::vector<std::byte> new_payload) {
    pager::WritePageGuard guard(pgkey, frame, evictor, bpm_latch,
                                storage_engine);
    ASSERT_FALSE(guard.is_dirty());
    std::vector<std::byte> &data_mut = guard.get_data_mut();

    ASSERT_TRUE(data_mut != new_payload);
    std::memcpy(data_mut.data(), new_payload.data(), PAGE_SIZE);

    const std::vector<std::byte> &reloaded = guard.get_data();
    ASSERT_TRUE(reloaded == new_payload);
  };

  auto read_buffer = [&pgkey, &frame, &evictor, &bpm_latch,
                      &storage_engine](std::vector<std::byte> expected) {
    pager::ReadPageGuard guard(pgkey, frame, evictor, bpm_latch,
                               storage_engine);
    const std::vector<std::byte> &data = guard.get_data();
    ASSERT_TRUE(guard.is_dirty());
    ASSERT_TRUE(data == expected);

    std::vector<std::byte> raw_buffer;
    storage_engine->read_page(pgkey, raw_buffer);
    ASSERT_TRUE(raw_buffer != expected);

    guard.flush();

    ASSERT_FALSE(guard.is_dirty());
    storage_engine->read_page(pgkey, raw_buffer);
    ASSERT_TRUE(raw_buffer == expected);
  };

  auto new_payload = generate_random_payload(PAGE_SIZE);
  std::thread write_thread(write_buffer, new_payload);
  write_thread.join();

  std::thread read_thread(read_buffer, new_payload);
  read_thread.join();
}

TEST_F(PageGuardTest, DropGuard) {
  auto frame = std::make_shared<pager::FrameHeader>(0);
  auto evictor = std::make_shared<pager::LRUKEvictor>(2, 10);
  auto bpm_latch = std::make_shared<std::mutex>();
  auto storage_engine = std::make_shared<storage::StorageEngine>(
      storage::StorageEngine::open(basedir));

  PageKey pgkey(1, 1);
  pager::ReadPageGuard rg1(pgkey, frame, evictor, bpm_latch, storage_engine);
  pager::ReadPageGuard rg2(pgkey, frame, evictor, bpm_latch, storage_engine);
  pager::ReadPageGuard rg3(pgkey, frame, evictor, bpm_latch, storage_engine);

  ASSERT_EQ(frame->get_pins(), 3);
  ASSERT_EQ(evictor->evict(), std::nullopt);

  rg1.drop();
  rg1.drop();

  ASSERT_EQ(frame->get_pins(), 2);

  rg2.drop();
  rg3.drop();

  ASSERT_EQ(frame->get_pins(), 0);
  ASSERT_NE(evictor->evict(), std::nullopt);
}

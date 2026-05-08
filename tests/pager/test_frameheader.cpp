#include "pager/bufferpool/bufferpool.h"
#include "test_utils.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

class FrameHeaderTest : public ::testing::Test {};

TEST_F(FrameHeaderTest, InitFrameHeader) {
  pager::FrameHeader frame(0);
  ASSERT_EQ(frame.get_frame_id(), 0);
  ASSERT_EQ(frame.get_pins(), 0);
  ASSERT_EQ(frame.is_dirty(), false);
  std::vector<std::byte> data = frame.get_data();

  std::vector<std::byte> expected(PAGE_SIZE, std::byte{0});
  ASSERT_TRUE(data == expected);
}

TEST_F(FrameHeaderTest, ConcurrentWritePins) {
  pager::FrameHeader frame(0);
  std::vector<std::thread> threads;

  for (int i = 0; i < 10; i++) {
    threads.push_back(std::thread([&frame]() { frame.incr_pins(); }));
    threads.push_back(std::thread([&frame]() { frame.decr_pins(); }));
  }

  for (int i = 0; i < threads.size(); i++)
    threads[i].join();

  ASSERT_EQ(frame.get_pins(), 0);
}

TEST_F(FrameHeaderTest, ConcurrentManyRWPins) {
  pager::FrameHeader frame(0);
  std::vector<std::thread> incrs;

  for (int i = 0; i < 50; i++)
    incrs.push_back(std::thread([&frame]() { frame.incr_pins(); }));

  for (int i = 0; i < incrs.size(); i++)
    incrs[i].join();

  ASSERT_EQ(frame.get_pins(), 50);

  std::vector<std::thread> decrs;
  for (int i = 0; i < 30; i++)
    decrs.push_back(std::thread([&frame]() { frame.decr_pins(); }));

  for (int i = 0; i < decrs.size(); i++)
    decrs[i].join();

  ASSERT_EQ(frame.get_pins(), 20);
}

TEST_F(FrameHeaderTest, WriteData) {
  pager::FrameHeader frame(0);
  std::vector<std::byte> &data = frame.get_data_mut();
  ASSERT_EQ(data.size(), PAGE_SIZE);

  std::vector<std::byte> initial(PAGE_SIZE, std::byte{0});
  ASSERT_TRUE(data == initial);

  std::vector<std::byte> new_data = generate_random_payload(PAGE_SIZE);
  std::memcpy(data.data(), new_data.data(), PAGE_SIZE);

  auto reload = frame.get_data_mut();
  ASSERT_TRUE(reload == new_data);
}

TEST_F(FrameHeaderTest, ConcurrentWriteData) {
  pager::FrameHeader frame(0);
  std::vector<std::byte> &data = frame.get_data_mut();

  std::vector<std::byte> initial(PAGE_SIZE, std::byte{0});
  ASSERT_TRUE(data == initial);

  auto write_data = [](pager::FrameHeader &local_frame) {
    local_frame.lock_write();
    std::vector<std::byte> new_payload = generate_random_payload(PAGE_SIZE);
    auto &data_mut = local_frame.get_data_mut();

    std::memcpy(data_mut.data(), new_payload.data(), PAGE_SIZE);
    auto reload = local_frame.get_data();
    ASSERT_TRUE(reload == new_payload);
    local_frame.unlock_write();
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++)
    threads.push_back(std::thread(write_data, std::ref(frame)));

  for (int i = 0; i < threads.size(); i++)
    threads[i].join();
}

TEST_F(FrameHeaderTest, ConcurrentRWData) {
  pager::FrameHeader frame(0);

  std::vector<std::byte> &data = frame.get_data_mut();
  auto payload = generate_random_payload(PAGE_SIZE);
  std::memcpy(data.data(), payload.data(), PAGE_SIZE);

  auto read_data = [](pager::FrameHeader &local_frame,
                      std::vector<std::byte> expected) {
    local_frame.lock_read();
    auto data = local_frame.get_data();
    ASSERT_TRUE(data == expected);
    local_frame.unlock_read();
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++)
    threads.push_back(std::thread(read_data, std::ref(frame), payload));

  for (int i = 0; i < threads.size(); i++)
    threads[i].join();

  auto write_data = [](pager::FrameHeader &local_frame,
                       std::vector<std::byte> payload) {
    local_frame.lock_write();
    auto &data_mut = local_frame.get_data_mut();
    std::memcpy(data_mut.data(), payload.data(), PAGE_SIZE);
    local_frame.unlock_write();
  };

  std::vector<std::byte> new_payload = generate_random_payload(PAGE_SIZE);
  std::thread write_thread(write_data, std::ref(frame), new_payload);
  std::thread read_thread(read_data, std::ref(frame), new_payload);

  write_thread.join();
  read_thread.join();
}

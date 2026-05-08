#include <gtest/gtest.h>
#include "pager/bufferpool/evictor.h"

class LRUKEvictorTest : public ::testing::Test {
protected:
  static constexpr size_t K = 2;
  static constexpr size_t NUM_FRAMES = 10;
  pager::LRUKEvictor evictor{K, NUM_FRAMES};
};

TEST_F(LRUKEvictorTest, SizeTracking) {
  ASSERT_EQ(evictor.size(), 0);

  evictor.record_access(0);
  evictor.set_evictable(0, true);
  ASSERT_EQ(evictor.size(), 1);

  evictor.set_evictable(0, false);
  ASSERT_EQ(evictor.size(), 0);
}

TEST_F(LRUKEvictorTest, EvictLessThanK) {
  evictor.record_access(0);
  evictor.record_access(1);
  evictor.record_access(2);
  evictor.set_evictable(0, true);
  evictor.set_evictable(1, true);
  evictor.set_evictable(2, true);

  auto victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 0);

  victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 1);
}

TEST_F(LRUKEvictorTest, EvictKAccesses) {
  evictor.record_access(0);
  evictor.record_access(0);
  evictor.record_access(1);
  evictor.record_access(1);
  evictor.record_access(2);
  evictor.record_access(2);
  evictor.set_evictable(0, true);
  evictor.set_evictable(1, true);
  evictor.set_evictable(2, true);

  auto victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 0);

  victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 1);
}

TEST_F(LRUKEvictorTest, LessThanKEvictedBeforeK) {
  evictor.record_access(0);
  evictor.record_access(0);
  evictor.record_access(1);
  evictor.set_evictable(0, true);
  evictor.set_evictable(1, true);

  auto victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 1);
}

TEST_F(LRUKEvictorTest, GraduationFromLessThanKToK) {
  evictor.record_access(0);
  evictor.record_access(1);
  evictor.set_evictable(0, true);
  evictor.set_evictable(1, true);

  evictor.record_access(0);
  evictor.record_access(0);

  auto victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 1);
}

TEST_F(LRUKEvictorTest, NonEvictableNotEvicted) {
  evictor.record_access(0);
  evictor.record_access(1);
  evictor.set_evictable(0, false);
  evictor.set_evictable(1, true);

  auto victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 1);

  ASSERT_EQ(evictor.size(), 0);
}

TEST_F(LRUKEvictorTest, Remove) {
  evictor.record_access(0);
  evictor.set_evictable(0, true);
  ASSERT_EQ(evictor.size(), 1);

  evictor.remove(0);
  ASSERT_EQ(evictor.size(), 0);

  auto victim = evictor.evict();
  ASSERT_FALSE(victim.has_value());
}

TEST_F(LRUKEvictorTest, EvictEmptyReturnsNullopt) {
  auto victim = evictor.evict();
  ASSERT_FALSE(victim.has_value());
}

TEST_F(LRUKEvictorTest, RecentAccessUpdatesOrder) {
  evictor.record_access(0);
  evictor.record_access(0);
  evictor.record_access(1);
  evictor.record_access(1);
  evictor.set_evictable(0, true);
  evictor.set_evictable(1, true);

  evictor.record_access(0);
  evictor.record_access(0);

  auto victim = evictor.evict();
  ASSERT_TRUE(victim.has_value());
  ASSERT_EQ(victim.value(), 1);
}

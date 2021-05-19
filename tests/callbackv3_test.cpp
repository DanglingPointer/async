#include "v3/refcounter.hpp"

#include <gtest/gtest.h>
#include <type_traits>

namespace {
using namespace async::v3;

TEST(RefCounterTest, refcounter_counts_correctly)
{
   RefCounter * counter = RefCounter::New();
   EXPECT_FALSE(counter->HasMaster());
   EXPECT_EQ(0u, counter->GetSlaveCount());

   counter->AddMaster();
   EXPECT_TRUE(counter->HasMaster());
   EXPECT_EQ(0u, counter->GetSlaveCount());

   counter->AddSlave();
   EXPECT_TRUE(counter->HasMaster());
   EXPECT_EQ(1, counter->GetSlaveCount());

   counter->AddSlave();
   EXPECT_TRUE(counter->HasMaster());
   EXPECT_EQ(2, counter->GetSlaveCount());

   counter->RemoveMaster();
   EXPECT_FALSE(counter->HasMaster());
   EXPECT_EQ(2, counter->GetSlaveCount());

   counter->RemoveSlave();
   EXPECT_FALSE(counter->HasMaster());
   EXPECT_EQ(1, counter->GetSlaveCount());

   counter->RemoveSlave();
   SUCCEED();
}

TEST(RefCounterTest, slave_wrapper_works_correctly)
{
   RefCounter * counter = RefCounter::New();
   EXPECT_FALSE(counter->HasMaster());
   EXPECT_FALSE(counter->GetSlaveCount());
   counter->AddMaster();
   EXPECT_TRUE(counter->HasMaster());

   {
      RefCounterSlave slave1(counter);
      EXPECT_EQ(1u, counter->GetSlaveCount());
      RefCounterSlave slave2(slave1);
      EXPECT_EQ(2u, counter->GetSlaveCount());
      RefCounterSlave slave3(std::move(slave1));
      EXPECT_EQ(2u, counter->GetSlaveCount());
      slave1 = slave2;
      EXPECT_EQ(3u, counter->GetSlaveCount());
      slave2 = std::move(slave3);
      EXPECT_EQ(2u, counter->GetSlaveCount());
   }
   EXPECT_EQ(0u, counter->GetSlaveCount());
   EXPECT_TRUE(counter->HasMaster());
   counter->RemoveMaster();
}

TEST(RefCounterTest, master_wrapper_works_correctly)
{
   RefCounter * counter = RefCounter::New();
   EXPECT_FALSE(counter->HasMaster());
   RefCounterMaster master(counter);
   EXPECT_TRUE(counter->HasMaster());
}

TEST(RefCounterTest, compile_time_asserts)
{
   static_assert(!std::is_default_constructible_v<RefCounter>);
   static_assert(!std::is_copy_constructible_v<RefCounter>);
   static_assert(!std::is_move_constructible_v<RefCounter>);

   static_assert(std::is_nothrow_default_constructible_v<RefCounterMaster>);
   static_assert(!std::is_copy_constructible_v<RefCounterMaster>);
   static_assert(std::is_nothrow_move_constructible_v<RefCounterMaster>);
   static_assert(!std::is_copy_assignable_v<RefCounterMaster>);
   static_assert(std::is_nothrow_move_assignable_v<RefCounterMaster>);
   static_assert(std::is_nothrow_swappable_v<RefCounterMaster>);

   static_assert(std::is_nothrow_default_constructible_v<RefCounterSlave>);
   static_assert(std::is_nothrow_copy_constructible_v<RefCounterSlave>);
   static_assert(std::is_nothrow_move_constructible_v<RefCounterSlave>);
   static_assert(std::is_nothrow_copy_assignable_v<RefCounterSlave>);
   static_assert(std::is_nothrow_move_assignable_v<RefCounterSlave>);
   static_assert(std::is_nothrow_swappable_v<RefCounterSlave>);

}

}
#include "v3/callbackowner.hpp"
#include "v3/refcounter.hpp"

#include <gtest/gtest.h>
#include <functional>
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


class CallbackV3Fixture : public ::testing::Test
{
protected:
   CallbackV3Fixture()
      : owner(CallbackOwner())
   {}

   void InvokeCallback(std::function<void()> f)
   {
      f();
   }

   std::optional<CallbackOwner> owner;
};

TEST_F(CallbackV3Fixture, compile_time_asserts)
{
   static_assert(std::is_default_constructible_v<CallbackOwner>);
   static_assert(!std::is_copy_constructible_v<CallbackOwner>);
   static_assert(std::is_nothrow_move_constructible_v<CallbackOwner>);
   static_assert(!std::is_copy_assignable_v<CallbackOwner>);
   static_assert(std::is_nothrow_move_assignable_v<CallbackOwner>);
}

TEST_F(CallbackV3Fixture, callback_runs_while_owner_is_alive)
{
   int result = 0;
   auto cb = owner->Cb([&](int i) mutable {
      result = i;
   });
   cb(42);
   EXPECT_EQ(42, result);
   cb(43);
   EXPECT_EQ(43, result);

   owner.reset();
   cb(44);
   EXPECT_EQ(43, result);
}

TEST_F(CallbackV3Fixture, correct_number_of_copy_and_move_operations)
{
   struct Counter
   {
      size_t * copyCount;
      size_t * moveCount;
      Counter(size_t * copyCount, size_t * moveCount)
         : copyCount(copyCount)
         , moveCount(moveCount)
      {}
      Counter(const Counter & rhs)
         : copyCount(rhs.copyCount)
         , moveCount(rhs.moveCount)
      {
         ++(*copyCount);
      }
      Counter(Counter && rhs)
         : copyCount(rhs.copyCount)
         , moveCount(rhs.moveCount)
      {
         ++(*moveCount);
      }
   };
   size_t copyCount = 0;
   size_t moveCount = 0;

   Counter myCounter(&copyCount, &moveCount);
   auto lambda = [myCounter] {};
   EXPECT_EQ(1u, copyCount);
   EXPECT_EQ(0u, moveCount);

   copyCount = 0;
   moveCount = 0;

   InvokeCallback(owner->Cb(lambda));
   EXPECT_EQ(1u, copyCount);
   EXPECT_EQ(1u, moveCount);

   copyCount = 0;
   moveCount = 0;

   InvokeCallback(owner->Cb(std::move(lambda)));
   EXPECT_EQ(0u, copyCount);
   EXPECT_EQ(2u, moveCount);
}

TEST_F(CallbackV3Fixture, has_pending_shows_correct_state)
{
   {
      auto cb = owner->Cb([] {});
      EXPECT_TRUE(owner->HasPendingCallbacks());
      cb();
      EXPECT_TRUE(owner->HasPendingCallbacks());
   }
   EXPECT_FALSE(owner->HasPendingCallbacks());
}

TEST_F(CallbackV3Fixture, no_deadlock_when_destroying_owner_from_callback)
{
   auto cb = (*owner)([this] {
      owner.reset();
   });
   cb();
   SUCCEED();
}

TEST_F(CallbackV3Fixture, deactivate_old_and_create_new_callbacks)
{
   int result = 0;
   {
      auto cb = owner->Cb([&](int i) mutable {
         result = i;
      });
      cb(42);
      EXPECT_EQ(42, result);

      owner->DeactivateCallbacks();
      cb(43);
      EXPECT_EQ(42, result);
   }
   result = 0;
   {
      auto cb = owner->Cb([&](int i) mutable {
         result = i;
      });
      cb(42);
      EXPECT_EQ(42, result);

      owner.reset();
      cb(43);
      EXPECT_EQ(42, result);
   }
}

} // namespace
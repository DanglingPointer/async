#include <gtest/gtest.h>
#include <string>
#include "callbackmanager.hpp"

namespace {
using namespace async;

class CallbackV2Fixture : public ::testing::Test
{
protected:
   CallbackV2Fixture()
      : mgr(std::make_unique<v2::CallbackManager>())
   {}

   void invokeCallback(v2::Callback<int> cb, int arg)
   {
      cb(arg);
   }

   void invokeCallback(v2::Callback<> cb)
   {
      cb();
   }

   std::unique_ptr<v2::CallbackManager> mgr;
};

TEST_F(CallbackV2Fixture, callback_runs_while_mgr_is_alive)
{
   int result = 0;
   auto lambda = [&] (int i) { result = i; };
   invokeCallback(mgr->Cb(lambda), 42);
   EXPECT_EQ(42, result);

   v2::Callback<int> secondCb(mgr->Cb(std::move(lambda)));
   EXPECT_TRUE(mgr->HasPending());
   mgr.reset();
   secondCb(43);
   EXPECT_EQ(42, result);
}

TEST_F(CallbackV2Fixture, correct_number_of_copy_and_move_operations)
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
   auto lambda = [myCounter] { };
   EXPECT_EQ(1u, copyCount);
   EXPECT_EQ(0u, moveCount);

   copyCount = 0;
   moveCount = 0;

   invokeCallback(mgr->Cb(lambda));
   EXPECT_EQ(1u, copyCount);
   EXPECT_EQ(1u, moveCount);

   copyCount = 0;
   moveCount = 0;

   invokeCallback(mgr->Cb(std::move(lambda)));
   EXPECT_EQ(0u, copyCount);
   EXPECT_EQ(2u, moveCount);
}

void foo(std::string & s) { s = "Hello World"; }

TEST_F(CallbackV2Fixture, adapter_works_with_function_pointers)
{
   std::string s;
   v2::Callback<std::string &> cb = (*mgr)(&foo);
   cb(s);
   EXPECT_STREQ("Hello World", s.c_str());
}

TEST_F(CallbackV2Fixture, detached_callback_runs_always)
{
   size_t invocationCount = 0U;

   v2::Callback<> cb = v2::detachedCb([&] { invocationCount++; });
   EXPECT_TRUE(v2::detachedCb.HasPending());
   cb();
   EXPECT_EQ(1U, invocationCount);
}

TEST_F(CallbackV2Fixture, has_pending_shows_correct_state)
{
   {
      v2::Callback<> cb = mgr->Cb([]{});
      EXPECT_TRUE(mgr->HasPending());
      cb();
      EXPECT_TRUE(mgr->HasPending());
   }
   EXPECT_FALSE(mgr->HasPending());
}

TEST_F(CallbackV2Fixture, is_owner_alive_shows_correct_state)
{
   v2::Callback<> cb = mgr->Cb([]{});
   EXPECT_TRUE(cb.IsOwnerAlive());
   cb();
   EXPECT_TRUE(cb.IsOwnerAlive());
   mgr.reset();
   EXPECT_FALSE(cb.IsOwnerAlive());
}

TEST_F(CallbackV2Fixture, callback_is_not_one_shot)
{
   size_t invocationCount = 0U;
   v2::Callback<> cb = mgr->Cb([&] { invocationCount++; });
   cb();
   EXPECT_EQ(1, invocationCount);
   cb();
   EXPECT_EQ(2, invocationCount);
}

TEST_F(CallbackV2Fixture, empty_callback_behaves_correctly)
{
   v2::Callback<int> cb = v2::detachedCb();
   cb(123);
   invokeCallback(std::move(cb), 42);
}

TEST_F(CallbackV2Fixture, wrapped_lambda_behaves_correctly)
{
   size_t invocationCount = 0U;
   {
      auto f = mgr->Wrap([&](int i) { invocationCount += i; });
      f(40);
      EXPECT_EQ(40, invocationCount);
      EXPECT_TRUE(mgr->HasPending());
      f(2);
      EXPECT_EQ(42, invocationCount);
      EXPECT_TRUE(mgr->HasPending());
   }
   EXPECT_FALSE(mgr->HasPending());

   {
      auto f = mgr->Wrap([&](int i) { invocationCount += i; });
      f(1);
      EXPECT_EQ(43, invocationCount);
      EXPECT_TRUE(mgr->HasPending());
      mgr.reset();
      f(1);
      EXPECT_EQ(43, invocationCount);
   }
}

TEST_F(CallbackV2Fixture, exceeding_max_simult_callbacks_throws_exception)
{
   v2::internal::Counter * counter = mgr->Cb().counter; // hack
   auto * refcount = reinterpret_cast<std::atomic<v2::internal::Counter::RefCountType> *>(counter); // hack
   (*refcount) += std::numeric_limits<v2::internal::Counter::RefCountType>::max() / 2;

   EXPECT_THROW({
      v2::Callback<> cb = mgr->Cb([] {});
   }, std::runtime_error);

   (*refcount)--;
   EXPECT_NO_THROW({
      v2::Callback<> cb = mgr->Cb([] {});
   });
}

TEST_F(CallbackV2Fixture, no_deadlock_when_destroying_canceller_from_callback)
{
   v2::Callback<> cb = mgr->Cb([this] { mgr.reset(); });
   cb();
   SUCCEED();
}

} // namespace

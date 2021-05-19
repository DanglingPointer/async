#include <gtest/gtest.h>
#include "v1/canceller.hpp"

namespace {
using namespace async::v1;

class CancellerFixture
   : protected Canceller<>
   , public ::testing::Test
{
protected:
   using typename Canceller<>::CallbackId;
};

TEST_F(CancellerFixture, callback_runs_while_canceller_is_alive)
{
   size_t invocationCount = 0U;
   Callback<> cb = MakeCb([&] { ++invocationCount; });

   EXPECT_FALSE(cb.Cancelled());
   cb();
   EXPECT_EQ(1U, invocationCount);
}

TEST_F(CancellerFixture, callback_does_not_run_if_invalidated)
{
   size_t invocationCount = 0U;
   Callback<> cb = MakeCb([&] { ++invocationCount; });

   InvalidateCallbacks();
   EXPECT_TRUE(cb.Cancelled());
   cb();
   EXPECT_EQ(0U, invocationCount);
}

TEST_F(CancellerFixture, callback_does_not_run_if_canceller_is_dead)
{
   size_t invocationCount = 0U;
   std::optional<Callback<>> cb;
   {
      Canceller<> c;
      cb.emplace(c.MakeCb([&] { ++invocationCount; }));
   }
   EXPECT_TRUE(cb->Cancelled());
   cb->Invoke();
   EXPECT_EQ(0U, invocationCount);
}

TEST_F(CancellerFixture, detached_callback_runs_if_canceller_is_dead)
{
   size_t invocationCount = 0U;
   std::optional<Callback<>> cb;
   {
      Canceller<> c;
      cb.emplace(c.DetachedCb([&] { ++invocationCount; }));
   }
   EXPECT_FALSE(cb->Cancelled());
   cb->Invoke();
   EXPECT_EQ(1U, invocationCount);
}

TEST_F(CancellerFixture, is_active_shows_correct_state)
{
   CallbackId id{};
   size_t invocationCount = 0U;
   {
      Callback<> cb = MakeCb([&] { ++invocationCount; }, &id);
      EXPECT_TRUE(IsActive(id));
      cb();
      EXPECT_TRUE(IsActive(id));
   }
   EXPECT_FALSE(IsActive(id));
}

TEST_F(CancellerFixture, callback_can_be_cancelled_individually)
{
   CallbackId id1{};
   CallbackId id2{};
   size_t invocationCount1 = 0U;
   size_t invocationCount2 = 0U;

   Callback<> cb1 = MakeCb([&] { ++invocationCount1; }, &id1);
   Callback<int> cb2 = MakeCb([&](int i) { invocationCount2 += i; }, &id2);

   CancelCallback(id1);

   cb1();
   cb2(42);

   EXPECT_FALSE(IsActive(id1));
   EXPECT_TRUE(IsActive(id2));

   EXPECT_EQ(0U, invocationCount1);
   EXPECT_EQ(42U, invocationCount2);
}

TEST_F(CancellerFixture, empty_callback_behaves_correctly)
{
   CallbackId id{};
   {
      auto cb = MakeCb(&id);
      EXPECT_TRUE(IsActive(id));
      cb();
      EXPECT_TRUE(IsActive(id));
   }
   EXPECT_FALSE(IsActive(id));
}

TEST_F(CancellerFixture, wrapped_lambda_behaves_correctly)
{
   size_t invocationCount = 0U;
   auto f = Wrap([&](int i) { invocationCount += i; });
   f(42);
   EXPECT_EQ(42, invocationCount);

   InvalidateCallbacks();
   f(3);
   EXPECT_EQ(42, invocationCount);
}

TEST_F(CancellerFixture, exceeding_max_simult_callbacks_throws_exception)
{
   size_t invocationCount = 0U;
   CallbackId id{};
   std::vector<Callback<>> cbs;
   for (size_t i = 0; i < Canceller<>::MAX_SIMULT_CALLBACKS; ++i) {
      CallbackId prev = id;
      cbs.emplace_back(MakeCb([&] { invocationCount++; }, &id));
      EXPECT_NE(prev.value, id.value);
      EXPECT_TRUE(IsActive(id));
   }

   EXPECT_THROW(cbs.emplace_back(MakeCb([&] { invocationCount++; }, &id)), std::runtime_error);

   cbs.pop_back();
   EXPECT_NO_THROW(cbs.emplace_back(MakeCb([&] { invocationCount++; }, &id)));
}

TEST_F(CancellerFixture, a_scheduled_callback_is_executed_lazily)
{
   std::function<void()> f;
   auto executor = [&] (std::function<void()> task) {
      f = std::move(task);
   };

   int number = 0;
   CallbackId id{};
   auto cb = MakeCb([&](int i) { number += i; }, &id);
   EXPECT_FALSE(f);
   Schedule(executor, std::move(cb), 42);
   EXPECT_TRUE(f);
   EXPECT_EQ(0, number);
   EXPECT_TRUE(IsActive(id));
   f();
   f = nullptr;
   EXPECT_FALSE(IsActive(id));
   EXPECT_EQ(42, number);
}

TEST_F(CancellerFixture, lazy_execution_of_callback_can_be_cancelled)
{
   std::function<void()> f;
   auto executor = [&] (std::function<void()> task) {
      f = std::move(task);
   };

   int number = 0;
   CallbackId id{};
   auto cb = MakeCb([&](int i) { number += i; }, &id);
   EXPECT_FALSE(f);
   Schedule(executor, std::move(cb), 42);
   EXPECT_TRUE(f);
   EXPECT_EQ(0, number);
   EXPECT_TRUE(IsActive(id));
   InvalidateCallbacks();
   f();
   EXPECT_EQ(0, number);
}

TEST_F(CancellerFixture, on_all_completed_executes_once_all_have_completed)
{
   auto executor = [] (std::function<void()> task) {
      task();
   };

   bool allFinished = false;
   bool cb1Finished = false;
   bool cb2Finished = false;

   Callback<> cb1 = DetachedCb();
   Callback<> cb2 = DetachedCb();

   {
      OnAllCompleted sync([&] { allFinished = true; });
      cb1 = sync.Track(MakeCb([&] { cb1Finished = true; }));
      cb2 = sync.Track(MakeCb([&] { cb2Finished = true; }));
      EXPECT_FALSE(allFinished);
   }

   EXPECT_FALSE(allFinished);

   Schedule(executor, std::move(cb1));
   EXPECT_TRUE(cb1Finished);
   EXPECT_FALSE(allFinished);

   Schedule(executor, std::move(cb2));
   EXPECT_TRUE(cb2Finished);
   EXPECT_TRUE(allFinished);
}

TEST_F(CancellerFixture, on_all_completed_executes_after_synchronizer_is_dead)
{
   auto executor = [] (std::function<void()> task) {
      task();
   };

   bool allFinished = false;
   bool cb1Finished = false;
   bool cb2Finished = false;

   {
      OnAllCompleted sync([&] { allFinished = true; });
      EXPECT_FALSE(allFinished);

      auto cb1 = sync.Track(MakeCb([&] { cb1Finished = true; }));
      auto cb2 = MakeCb([&] { cb2Finished = true; });
      sync.Track(cb2);
      EXPECT_FALSE(allFinished);

      Schedule(executor, std::move(cb1));
      EXPECT_TRUE(cb1Finished);
      EXPECT_FALSE(allFinished);

      Schedule(executor, std::move(cb2));
      EXPECT_TRUE(cb2Finished);
      EXPECT_FALSE(allFinished);
   }

   EXPECT_TRUE(allFinished);
}

TEST_F(CancellerFixture, on_all_completed_doesnt_execute_twice)
{
   int cbInvocationCount = 0;
   int syncInvocationCount = 0;

   auto cb = MakeCb([&] { cbInvocationCount++; });

   {
      OnAllCompleted sync([&] { syncInvocationCount++; });
      sync.Track(cb);
   }

   EXPECT_EQ(0, cbInvocationCount);
   EXPECT_EQ(0, syncInvocationCount);

   cb();
   EXPECT_EQ(1, cbInvocationCount);
   EXPECT_EQ(1, syncInvocationCount);

   cb();
   EXPECT_EQ(2, cbInvocationCount);
   EXPECT_EQ(1, syncInvocationCount);
}

TEST_F(CancellerFixture, on_any_completed_executes_once_first_has_completed)
{
   auto executor = [] (std::function<void()> task) {
      task();
   };

   bool anyFinished = false;
   bool cb1Finished = false;
   bool cb2Finished = false;

   Callback<> cb1 = DetachedCb();
   Callback<> cb2 = DetachedCb();

   {
      OnAnyCompleted sync([&] { anyFinished = true; });
      cb1 = sync.Track(MakeCb([&] { cb1Finished = true; }));
      cb2 = sync.Track(MakeCb([&] { cb2Finished = true; }));
      EXPECT_FALSE(anyFinished);
   }

   EXPECT_FALSE(anyFinished);

   Schedule(executor, std::move(cb1));
   EXPECT_TRUE(cb1Finished);
   EXPECT_FALSE(cb2Finished);
   EXPECT_TRUE(anyFinished);
}

TEST_F(CancellerFixture, on_any_completed_executes_after_synchronizer_is_dead)
{
   auto executor = [] (std::function<void()> task) {
      task();
   };

   bool anyFinished = false;
   bool cb1Finished = false;
   bool cb2Finished = false;

   {
      OnAnyCompleted sync([&] { anyFinished = true; });
      EXPECT_FALSE(anyFinished);

      auto cb1 = sync.Track(MakeCb([&] { cb1Finished = true; }));
      auto cb2 = MakeCb([&] { cb2Finished = true; });
      sync.Track(cb2);
      EXPECT_FALSE(anyFinished);

      Schedule(executor, std::move(cb1));
      EXPECT_TRUE(cb1Finished);
      EXPECT_FALSE(anyFinished);
      EXPECT_FALSE(cb2Finished);
   }

   EXPECT_TRUE(anyFinished);
}

TEST_F(CancellerFixture, on_any_completed_doesnt_execute_twice)
{
   int cb1InvocationCount = 0;
   int cb2InvocationCount = 0;
   int syncInvocationCount = 0;

   auto cb1 = MakeCb([&] { cb1InvocationCount++; });
   auto cb2 = MakeCb([&] { cb2InvocationCount++; });

   {
      OnAnyCompleted sync([&] { syncInvocationCount++; });
      sync.Track(cb1);
      sync.Track(cb2);
   }

   EXPECT_EQ(0, cb1InvocationCount);
   EXPECT_EQ(0, cb2InvocationCount);
   EXPECT_EQ(0, syncInvocationCount);

   cb1();
   EXPECT_EQ(1, cb1InvocationCount);
   EXPECT_EQ(1, syncInvocationCount);

   cb2();
   EXPECT_EQ(1, cb2InvocationCount);
   EXPECT_EQ(1, syncInvocationCount);

   cb1();
   EXPECT_EQ(2, cb1InvocationCount);
   EXPECT_EQ(1, syncInvocationCount);
}

TEST_F(CancellerFixture, synchronizer_can_be_move_constructed_and_move_assigned)
{
   int cbInvocationCount = 0;
   int syncInvocationCount = 0;

   auto cb = MakeCb([&] () noexcept { cbInvocationCount++; });

   OnAnyCompleted sync([&] { syncInvocationCount++; });
   sync.Track(cb);

   cb();
   EXPECT_EQ(1, cbInvocationCount);
   EXPECT_EQ(0, syncInvocationCount);

   {
      OnAnyCompleted temp = std::move(sync);
   }

   EXPECT_EQ(1, syncInvocationCount);

   cb();
   EXPECT_EQ(2, cbInvocationCount);
   EXPECT_EQ(1, syncInvocationCount);

   auto cb1 = MakeCb([&] { cbInvocationCount++; });
   EXPECT_THROW(sync.Track(cb1), std::runtime_error);

   sync = OnAnyCompleted([&] { syncInvocationCount++; });
   EXPECT_NO_THROW(sync.Track(cb1));
}

TEST_F(CancellerFixture, no_deadlock_when_destroying_canceller_from_callback)
{
   std::optional<Canceller<>> canceller;
   canceller.emplace();

   auto cb = canceller->MakeCb([&] { canceller.reset(); });
   cb();
   SUCCEED();
}

} // namespace

#include <gtest/gtest.h>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include "future.hpp"
#include "canceller.hpp"
#include "mempool.hpp"

namespace {
using namespace async;

class AsyncFixture : public ::testing::Test
{
protected:
   AsyncFixture() {}

   std::function<void(std::function<void()>)> GetExecutor()
   {
      return [this](std::function<void()> task) { queue.emplace_back(std::move(task)); };
   }
   void EnqueueTask(std::function<void()> task) { queue.emplace_back(std::move(task)); }
   size_t ProcessTasks(size_t count = std::numeric_limits<size_t>::max())
   {
      size_t processed = 0;
      while (processed < count && !queue.empty()) {
         auto & task = queue.front();
         task();
         queue.pop_front();
         ++processed;
      }
      return processed;
   }

   std::deque<std::function<void()>> queue;
};

TEST_F(AsyncFixture, promised_task_is_completed_when_there_is_future)
{
   bool done = false;
   Promise<bool> promise(GetExecutor());
   Future<bool> future = promise.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [&] {
      done = true;
      return done;
   }));

   ProcessTasks(1U);
   EXPECT_TRUE(done);
}

TEST_F(AsyncFixture, promised_task_is_not_executed_when_there_is_no_future)
{
   bool done = false;
   Promise<bool> promise(GetExecutor());

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [&] {
      done = true;
      return done;
   }));

   ProcessTasks();
   EXPECT_FALSE(done);
}

TEST_F(AsyncFixture, future_is_active_before_execution_and_inactive_after)
{
   Promise<bool> promise(GetExecutor());
   Future<bool> future = promise.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [] { return true; }));
   EXPECT_TRUE(future.IsActive());
   ProcessTasks();

   EXPECT_FALSE(future.IsActive());
}

TEST_F(AsyncFixture, task_is_not_executed_if_canceled)
{
   bool done = false;
   Promise<bool> promise(GetExecutor());
   Future<bool> future = promise.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [&] {
      done = true;
      return done;
   }));
   future.Cancel();
   ProcessTasks();

   EXPECT_FALSE(done);
}

TEST_F(AsyncFixture, future_is_inactive_if_promise_died_before_execution)
{
   Promise<bool> promise(GetExecutor());
   Future<bool> future = promise.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [] { return true; }));
   EXPECT_TRUE(future.IsActive());

   queue.clear();
   EXPECT_FALSE(future.IsActive());
}

TEST_F(AsyncFixture, callback_is_called_after_completion_using_executor)
{
   std::optional<bool> result;
   Promise<bool> promise(GetExecutor());
   Future<bool> future = promise.GetFuture().Then([&](std::optional<bool> r) { result = r; });

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [] { return true; }));
   ProcessTasks(1U);
   EXPECT_FALSE(result.has_value());

   ProcessTasks();
   EXPECT_TRUE(result.has_value());
   EXPECT_EQ(*result, true);
}

TEST_F(AsyncFixture, callback_is_not_called_if_canceled_before_execution)
{
   bool callbackCalled = false;
   Promise<bool> promise(GetExecutor());
   Future<bool> future =
      promise.GetFuture().Then([&](std::optional<bool>) { callbackCalled = true; });

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [] { return true; }));
   future.Cancel();
   ProcessTasks();

   EXPECT_FALSE(callbackCalled);
}

TEST_F(AsyncFixture, callback_is_not_called_if_canceled_after_execution)
{
   bool callbackCalled = false;
   Promise<bool> promise(GetExecutor());
   Future<bool> future =
      promise.GetFuture().Then([&](std::optional<bool>) { callbackCalled = true; });

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [] { return true; }));
   ProcessTasks(1U);
   EXPECT_FALSE(callbackCalled);

   future.Cancel();
   ProcessTasks();
   EXPECT_FALSE(callbackCalled);
}

TEST_F(AsyncFixture, callback_is_called_without_result_if_promise_died_prematurely)
{
   std::optional<bool> result;
   bool callbackCalled = false;
   Promise<bool> promise([](auto task) { task(); });
   Future<bool> future = promise.GetFuture().Then([&](std::optional<bool> r) {
      result = r;
      callbackCalled = true;
   });

   EnqueueTask(EmbedPromiseIntoTask(std::move(promise), [&] { return true; }));

   queue.clear();
   ProcessTasks();
   EXPECT_TRUE(callbackCalled);
   EXPECT_FALSE(result);
}

TEST_F(AsyncFixture, operator_AND_future_becomes_inactive_iff_both_tasks_have_finished)
{
   Promise<bool> p1(GetExecutor());
   Promise<bool> p2(GetExecutor());

   Future<bool> f1 = p1.GetFuture();
   Future<bool> f2 = p2.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(p1), [] { return true; }));
   EnqueueTask(EmbedPromiseIntoTask(std::move(p2), [] { return true; }));

   Future<Empty> future = std::move(f1) && std::move(f2);
   EXPECT_TRUE(future.IsActive());

   ProcessTasks(1U);
   EXPECT_TRUE(future.IsActive());

   ProcessTasks();
   EXPECT_FALSE(future.IsActive());
}

TEST_F(AsyncFixture, operator_OR_future_become_inactive_once_one_of_the_tasks_has_finished)
{
   Promise<bool> p1(GetExecutor());
   Promise<bool> p2(GetExecutor());

   Future<bool> f1 = p1.GetFuture();
   Future<bool> f2 = p2.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(p1), [] { return true; }));

   Future<Empty> future = std::move(f1) || std::move(f2);
   EXPECT_TRUE(future.IsActive());

   ProcessTasks();
   EXPECT_FALSE(future.IsActive());
}

TEST_F(AsyncFixture, operator_AND_callback_is_executed_iff_both_tasks_have_finished)
{
   std::optional<Empty> result;
   bool done = false;

   Promise<bool> p1(GetExecutor());
   Promise<bool> p2(GetExecutor());

   Future<bool> f1 = p1.GetFuture();
   Future<bool> f2 = p2.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(p1), [] { return true; }));
   EnqueueTask(EmbedPromiseIntoTask(std::move(p2), [] { return true; }));

   Future<Empty> future = (std::move(f1) && std::move(f2)).Then([&](std::optional<Empty> r) {
      result = r;
      done = true;
   });

   ProcessTasks(1U);
   EXPECT_FALSE(done);
   EXPECT_FALSE(result);

   ProcessTasks();
   EXPECT_TRUE(done);
   EXPECT_TRUE(result);
}

TEST_F(AsyncFixture, operator_OR_callback_is_executed_once_one_of_the_tasks_has_finished)
{
   std::optional<Empty> result;
   bool done = false;

   Promise<bool> p1(GetExecutor());
   Promise<bool> p2(GetExecutor());

   Future<bool> f1 = p1.GetFuture();
   Future<bool> f2 = p2.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(p1), [] { return true; }));

   Future<Empty> future = (std::move(f1) || std::move(f2)).Then([&](std::optional<Empty> r) {
      result = r;
      done = true;
   });

   ProcessTasks();
   EXPECT_TRUE(done);
   EXPECT_TRUE(result);
}

TEST_F(AsyncFixture, operator_OR_cancels_the_last_task)
{
   bool done2 = false;

   Promise<bool> p1(GetExecutor());
   Promise<bool> p2(GetExecutor());

   Future<bool> f1 = p1.GetFuture();
   Future<bool> f2 = p2.GetFuture();

   EnqueueTask(EmbedPromiseIntoTask(std::move(p1), [] { return true; }));

   Future<Empty> future = std::move(f1) || std::move(f2);

   ProcessTasks();

   EnqueueTask(EmbedPromiseIntoTask(std::move(p2), [&] {
      done2 = true;
      return done2;
   }));
   ProcessTasks();
   EXPECT_FALSE(done2);
}

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

   async::Schedule(executor, std::move(cb1));
   EXPECT_TRUE(cb1Finished);
   EXPECT_FALSE(allFinished);

   async::Schedule(executor, std::move(cb2));
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

      async::Schedule(executor, std::move(cb1));
      EXPECT_TRUE(cb1Finished);
      EXPECT_FALSE(allFinished);

      async::Schedule(executor, std::move(cb2));
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

   async::Schedule(executor, std::move(cb1));
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

      async::Schedule(executor, std::move(cb1));
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

TEST(MempoolTest, mempool_shrinks_and_resizes_correctly)
{
   {
      mem::Pool<2, 8, 32, 64> pool(5);
      auto p = pool.Make<std::pair<double, double>>(35.0, 36.0);
      EXPECT_EQ(5 * 4, pool.GetBlockCount());
      EXPECT_EQ((2+8+32+64) * 5, pool.GetSize());

      EXPECT_EQ(35.0, p->first);
      EXPECT_EQ(36.0, p->second);

      pool.ShrinkToFit();
      EXPECT_EQ(1U, pool.GetBlockCount());
      EXPECT_EQ(32, pool.GetSize());

      p.Reset();
      pool.ShrinkToFit();
      EXPECT_EQ(0U, pool.GetBlockCount());
      EXPECT_EQ(0U, pool.GetSize());

      pool.Resize(6U);
      EXPECT_EQ(6U * 4, pool.GetBlockCount());
      EXPECT_EQ((2+8+32+64) * 6, pool.GetSize());
   }

   {
      mem::Pool<4, 16> pool(5);
      auto p = pool.Make<int32_t>(42);
      EXPECT_EQ(5U * 2, pool.GetBlockCount());
      EXPECT_EQ((4+16) * 5, pool.GetSize());

      EXPECT_EQ(42, *p);

      pool.ShrinkToFit();
      EXPECT_EQ(1U, pool.GetBlockCount());
      EXPECT_EQ(4U, pool.GetSize());

      p.Reset();
      pool.ShrinkToFit();
      EXPECT_EQ(0U, pool.GetBlockCount());
      EXPECT_EQ(0U, pool.GetSize());

      pool.Resize(6U);
      EXPECT_EQ(6U * 2, pool.GetBlockCount());
      EXPECT_EQ((4+16) * 6, pool.GetSize());
   }
}

TEST(MempoolTest, mempool_shrinks_correctly_with_shared_ptr)
{
   {
      mem::Pool<2, 8, 32, 64> pool(5);
      auto p1 = pool.MakeShared<float>(35.0f);
      auto p2 = p1;
      EXPECT_EQ(5 * 4, pool.GetBlockCount());
      EXPECT_EQ((2+8+32+64) * 5, pool.GetSize());
      EXPECT_EQ(35.0f, *p1);
      EXPECT_EQ(35.0f, *p2);

      pool.ShrinkToFit();
      EXPECT_EQ(1U, pool.GetBlockCount());
      EXPECT_EQ(8, pool.GetSize());

      p1.reset();
      pool.ShrinkToFit();
      EXPECT_EQ(1U, pool.GetBlockCount());
      EXPECT_EQ(8, pool.GetSize());

      p2.reset();
      pool.ShrinkToFit();
      EXPECT_EQ(0U, pool.GetBlockCount());
      EXPECT_EQ(0U, pool.GetSize());
   }

   {
      mem::Pool<4, 16> pool(5);
      auto p1 = pool.MakeShared<int32_t>(42);
      auto p2 = p1;
      EXPECT_EQ(5U * 2, pool.GetBlockCount());
      EXPECT_EQ((4+16) * 5, pool.GetSize());
      EXPECT_EQ(42, *p1);
      EXPECT_EQ(42, *p2);

      pool.ShrinkToFit();
      EXPECT_EQ(1U, pool.GetBlockCount());
      EXPECT_EQ(4U, pool.GetSize());

      p1.reset();
      pool.ShrinkToFit();
      EXPECT_EQ(1U, pool.GetBlockCount());
      EXPECT_EQ(4U, pool.GetSize());

      p2.reset();
      pool.ShrinkToFit();
      EXPECT_EQ(0U, pool.GetBlockCount());
      EXPECT_EQ(0U, pool.GetSize());
   }
}

TEST(MempoolTest, mempool_cleans_up_when_constructor_throws)
{
   struct MyException {};
   struct Thrower
   {
      Thrower() {
         throw MyException{};
      }
   };

   mem::Pool<2, 8, 32, 64> pool(5);
   mem::PoolPtr<Thrower> p;

   EXPECT_THROW({
      p = pool.Make<Thrower>();
   }, MyException);

   pool.ShrinkToFit();
   EXPECT_EQ(0U, pool.GetBlockCount());
   EXPECT_EQ(0U, pool.GetSize());
}

} // namespace
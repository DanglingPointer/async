#include <gtest/gtest.h>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <optional>
#include "future.hpp"
#include "callbacks.hpp"
#include "callbackmanager.hpp"

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

} // namespace

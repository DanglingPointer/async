#include <gtest/gtest.h>
#include <unordered_set>
#include "workerpool.hpp"
#include "workerpool.cpp"
#include "worker.hpp"

namespace {
using namespace async;

struct TestWorkerPoolTraits : DefaultWorkerPoolTraits
{
   static constexpr size_t MIN_SIZE = 2U;
   static constexpr size_t MAX_SIZE = 4U;
   static constexpr auto MAX_LINGER = std::chrono::seconds(10);
   static constexpr auto TIMER_RESOLUTION = std::chrono::milliseconds(100);
   using TaskType = std::function<void()>;
   static constexpr bool JOIN_THREADS = true;
};

using ThreadPool = WorkerPool<TestWorkerPoolTraits>;

class WorkerPoolFixture : public ::testing::Test
{
protected:
   struct TestWorkerPoolTraits : DefaultWorkerPoolTraits
   {
      static constexpr size_t MIN_SIZE = 2U;
      static constexpr size_t MAX_SIZE = 4U;
      static constexpr auto MAX_LINGER = std::chrono::milliseconds(500);
      static constexpr auto TIMER_RESOLUTION = std::chrono::nanoseconds(1);
      using TaskType = std::function<void()>;
      static constexpr bool JOIN_THREADS = true;
      static constexpr bool CATCH_EXCEPTIONS = true;
      static constexpr bool WITH_TIMER = true;
   };
   using ThreadPool = WorkerPool<TestWorkerPoolTraits>;

   WorkerPoolFixture() = default;

   std::function<void(std::string)> GetLogger()
   {
      return [this] (auto str) {
         std::lock_guard lg(logMutex);
         loglines.push_back(std::move(str));
      };
   }
   std::function<std::chrono::steady_clock::time_point()> GetTime()
   {
      return [this] {
         std::lock_guard lg(nowMutex);
         return now;
      };
   }

   std::vector<std::string> loglines;
   std::chrono::steady_clock::time_point now;
   std::mutex nowMutex;
   std::mutex logMutex;
};


TEST_F(WorkerPoolFixture, workerpool_executes_in_parallel_in_different_threads)
{
   ThreadPool p(GetLogger(), GetTime());
   std::this_thread::sleep_for(500ms);
   EXPECT_EQ(2u, p.GetWorkerCount());

   std::atomic_bool canProceed1 = false;
   std::atomic_bool canProceed2 = false;

   std::atomic_bool started1 = false;
   std::atomic_bool started2 = false;

   std::thread::id id1;
   std::thread::id id2;

   p.Execute([&] {
      started1 = true;
      id1 = std::this_thread::get_id();
      while (!canProceed1)
         std::this_thread::yield();
   });
   p.Execute([&] {
      started2 = true;
      id2 = std::this_thread::get_id();
      while (!canProceed2)
         std::this_thread::yield();
   });

   while (!started1)
      std::this_thread::yield();
   while (!started2)
      std::this_thread::yield();
   EXPECT_NE(id1, id2);
   EXPECT_EQ(ThreadPool::MIN_WORKER_COUNT, p.GetWorkerCount());

   canProceed1 = true;
   canProceed2 = true;
}

TEST_F(WorkerPoolFixture, workerpool_grows_until_max_capacity)
{
   ThreadPool p(GetLogger(), GetTime());
   std::this_thread::sleep_for(500ms);

   std::atomic_uint32_t startedCount = 0U;
   std::atomic_uint32_t stoppedCount = 0U;
   std::atomic_bool canProceed = false;

   std::unordered_set<std::thread::id> threadIds;
   std::mutex mut;

   for (size_t i = 0; i < ThreadPool::MAX_WORKER_COUNT + 1; ++i) {
      p.Execute([&] {
         {
            std::lock_guard lg(mut);
            threadIds.insert(std::this_thread::get_id());
         }
         startedCount++;
         while (!canProceed)
            std::this_thread::yield();
         stoppedCount++;
      });
      std::this_thread::sleep_for(100ms);
   }
   while (startedCount != ThreadPool::MAX_WORKER_COUNT)
      std::this_thread::yield();
   canProceed = true;
   while (stoppedCount != ThreadPool::MAX_WORKER_COUNT + 1)
      std::this_thread::yield();
   EXPECT_EQ(ThreadPool::MAX_WORKER_COUNT, threadIds.size());
   EXPECT_EQ(ThreadPool::MAX_WORKER_COUNT, p.GetWorkerCount());
   std::this_thread::sleep_for(TestWorkerPoolTraits::MAX_LINGER + 100ms);
   EXPECT_EQ(ThreadPool::MIN_WORKER_COUNT, p.GetWorkerCount());
}

TEST_F(WorkerPoolFixture, timer_fires_after_timeout)
{
   ThreadPool p(GetLogger(), GetTime());
   std::this_thread::sleep_for(500ms);

   std::atomic_bool done = false;
   p.ExecuteIn(10'000ms, [&] { done = true; });
   {
      std::lock_guard lg(nowMutex);
      now += 9999ms;
   }
   std::this_thread::sleep_for(100ms);
   EXPECT_FALSE(done);
   {
      std::lock_guard lg(nowMutex);
      now += 1ms;
   }
   while (!done)
      std::this_thread::yield();
   SUCCEED();

   done = false;
   p.ExecuteAt(now, [&] { done = true; });
   while (!done)
      std::this_thread::yield();
   SUCCEED();

   done = false;
   p.ExecuteAt(now + 10000ms, [&] { done = true; });
   {
      std::lock_guard lg(nowMutex);
      now += 9999ms;
   }
   std::this_thread::sleep_for(100ms);
   EXPECT_FALSE(done);
   {
      std::lock_guard lg(nowMutex);
      now += 1ms;
   }
   while (!done)
      std::this_thread::yield();
   SUCCEED();
}

TEST_F(WorkerPoolFixture, worker_catches_exceptions)
{
   ThreadPool p(GetLogger(), GetTime());
   std::this_thread::sleep_for(500ms);

   std::atomic_bool done = false;
   p.Execute([&] {
      done = true;
      throw 42;
   });

   while(!done)
      std::this_thread::yield();
   std::this_thread::sleep_for(100ms);

   std::lock_guard lg(logMutex);
   EXPECT_EQ(1u, loglines.size());

   const char loglineStart[] = "Uncaught exception in thread";
   EXPECT_STREQ(loglineStart, loglines.back().substr(0, sizeof(loglineStart) - 1).c_str());
}

TEST_F(WorkerPoolFixture, worker_doesnt_do_unnecessary_copies)
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
      void signalFinished(std::promise<void> & p)
      {
         p.set_value();
      }
   };

   ThreadPool p(GetLogger(), GetTime());
   std::this_thread::sleep_for(500ms);

   {
      size_t copyCount = 0;
      size_t moveCount = 0;
      std::promise<void> finished;

      Counter c(&copyCount, &moveCount);
      auto future = finished.get_future();

      p.Execute([&finished, c = std::move(c)]() mutable {
         c.signalFinished(finished);
      });

      auto status = future.wait_for(5s);
      EXPECT_EQ(std::future_status::ready, status);
      EXPECT_EQ(0U, copyCount);
      EXPECT_GE(3U, moveCount);
   }
   {
      size_t copyCount = 0;
      size_t moveCount = 0;
      std::promise<void> finished;

      Counter c(&copyCount, &moveCount);
      auto future = finished.get_future();
      auto task = [&finished, c=std::move(c)]() mutable {
         c.signalFinished(finished);
      };

      p.Execute(task);

      auto status = future.wait_for(5s);
      EXPECT_EQ(std::future_status::ready, status);
      EXPECT_EQ(1U, copyCount);
      EXPECT_GE(2U, moveCount);
   }
}

TEST(WorkerTest, worker_executes_instantaneous_task_within_100ms)
{
   Worker w({"", 1, nullptr});
   std::this_thread::sleep_for(500ms);

   std::promise<void> done;
   auto future = done.get_future();
   w.Schedule([&] { done.set_value(); });

   auto status = future.wait_for(100ms);
   EXPECT_EQ(std::future_status::ready, status);
}

TEST(WorkerTest, worker_executes_delayed_task_within_100ms)
{
   Worker w({"", 1, nullptr});
   std::this_thread::sleep_for(500ms);

   std::atomic_bool done = false;
   w.Schedule(1s, [&] {
      done = true;
   });

   std::this_thread::sleep_for(900ms);
   EXPECT_EQ(false, done);

   std::this_thread::sleep_for(200ms);
   EXPECT_EQ(true, done);
}

std::unique_ptr<Worker> CreateReadyWorker(size_t capacity)
{
   auto w = std::make_unique<Worker>(Worker::Config{"", capacity, nullptr});

   std::promise<void> ready;
   std::future<void> f = ready.get_future();
   w->Schedule([&] { ready.set_value(); });

   auto status = f.wait_for(1s);
   EXPECT_EQ(std::future_status::ready, status);
   if (status != std::future_status::ready)
      std::abort();
   return w;
}

TEST(WorkerTest, worker_executes_in_correct_order)
{
   auto w = CreateReadyWorker(3);

   bool done1 = false, done2 = false;
   std::promise<void> finished;
   auto future = finished.get_future();

   w->Schedule(1ms, [&] {
      EXPECT_TRUE(done1);
      EXPECT_TRUE(done2);
      finished.set_value();
   });
   w->Schedule([&] { done1 = true; });
   w->Schedule([&] {
      EXPECT_TRUE(done1);
      done2 = true;
   });
   auto status = future.wait_for(100ms);
   EXPECT_EQ(std::future_status::ready, status);
}

TEST(WorkerTest, worker_respects_max_capacity)
{
   auto w = CreateReadyWorker(1);

   std::promise<void> unblocker;
   auto f = unblocker.get_future();
   w->Schedule([&] {
      f.wait();
   });
   std::this_thread::sleep_for(100ms);
   EXPECT_TRUE(w->TrySchedule([] {}));
   EXPECT_FALSE(w->TrySchedule([] {}));

   unblocker.set_value();
   std::this_thread::sleep_for(100ms);
   EXPECT_TRUE(w->TrySchedule([] {}));
}

TEST(WorkerTest, worker_handles_uncaught_exceptions)
{
   std::string workerName;
   std::string exceptionWhat;

   std::promise<void> done;
   auto future = done.get_future();

   Worker w({"test worker", 1, [&] (std::string_view name, std::string_view what) {
      workerName = name;
      exceptionWhat = what;
      done.set_value();
   }});
   w.Schedule([] {
      throw std::runtime_error("test exception");
   });
   auto status = future.wait_for(1s);
   EXPECT_EQ(std::future_status::ready, status);

   EXPECT_STREQ("test worker", workerName.c_str());
   EXPECT_STREQ("test exception", exceptionWhat.c_str());
}


struct NonCopyable
{
   NonCopyable() = default;
   NonCopyable(NonCopyable &&) = default;
   NonCopyable(const NonCopyable &) = delete;
   NonCopyable & operator=(NonCopyable &&) = default;
   NonCopyable & operator=(const NonCopyable &) = delete;
};

TEST(AlwaysCopyableTest, always_copyable_makes_uncopyable_copyable)
{
   bool works = false;
   NonCopyable nc;
   std::function<void()> f(
       async::internal::AlwaysCopyable([nc = std::move(nc), &works] {
          works = true;
       }));
   f();
   EXPECT_TRUE(works);
}

} // namespace

#include "workerpool.hpp"
#include "thirdparty/blockingconcurrentqueue.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace async {

using namespace std::chrono_literals;

template <typename Traits>
class WorkerPool<Traits>::TimerCtx
{
public:
   using Task = typename Traits::TaskType;

   explicit TimerCtx(std::function<std::chrono::steady_clock::time_point()> && now)
      : m_now(std::move(now))
      , m_stopped(false)
   {}
   void Stop() noexcept
   {
      m_stopped.store(true, std::memory_order_relaxed);
   }
   void Schedule(std::chrono::milliseconds when, Task && t)
   {
      Schedule(m_now() + when, std::move(t));
   }
   void Schedule(std::chrono::steady_clock::time_point when, Task && t)
   {
      std::lock_guard lg(m_mutex);
      m_pending.emplace(when, std::move(t));
   }
   static void RunTimer(std::shared_ptr<TimerCtx> ctx)
   {
      std::vector<Task> ts;
      while (!ctx->m_stopped.load(std::memory_order_relaxed)) {
         std::this_thread::sleep_for(Traits::TIMER_RESOLUTION);

         {
            std::lock_guard lg(ctx->m_mutex);
            if (ctx->m_pending.empty())
               continue;

            auto timeNow = ctx->m_now();
            for (auto head = std::begin(ctx->m_pending);
                 head != std::end(ctx->m_pending) && head->first <= timeNow;
                 head = std::begin(ctx->m_pending)) {
               ts.emplace_back(std::move(head->second));
               ctx->m_pending.erase(head);
            }
         }
         for (auto & t : ts)
            t();
         ts.clear();
      }
   }

private:
   std::multimap<std::chrono::steady_clock::time_point, Task> m_pending;
   const std::function<std::chrono::steady_clock::time_point()> m_now;
   std::atomic_bool m_stopped;
   std::mutex m_mutex;
};

template <typename Traits>
class WorkerPool<Traits>::WorkerCtx
{
public:
   using Task = typename Traits::TaskType;

   explicit WorkerCtx(std::function<void(std::string)> && logger)
      : m_queue(Traits::MAX_SIZE)
      , m_logger(std::move(logger))
      , m_stopped(false)
      , m_workerCount(0U)
      , m_busyCount(0U)
   {}
   void Stop() noexcept
   {
      m_stopped.store(true, std::memory_order_relaxed);
   }
   void AddTask(Task && t)
   {
      m_queue.enqueue(std::move(t));
   }
   uint32_t GetBusyCount() const noexcept
   {
      return m_busyCount.load(std::memory_order_acquire);
   }
   uint32_t GetWorkerCount() const noexcept
   {
      return m_workerCount.load(std::memory_order_acquire);
   }
   static void RunMandatoryWorker(std::shared_ptr<WorkerCtx> ctx)
   {
      ctx->m_workerCount.fetch_add(1, std::memory_order_acq_rel);
      Task t;
      while (!ctx->m_stopped.load(std::memory_order_relaxed)) {
         ctx->m_queue.wait_dequeue(t);
         ctx->InvokeGuarded(t);
      }
      ctx->m_workerCount.fetch_sub(1, std::memory_order_acq_rel);
   }
   static void RunOptionalWorker(std::shared_ptr<WorkerCtx> ctx)
   {
      ctx->m_workerCount.fetch_add(1, std::memory_order_acq_rel);
      Task t;
      while (!ctx->m_stopped.load(std::memory_order_relaxed)) {
         bool dequeued = ctx->m_queue.wait_dequeue_timed(t, Traits::MAX_LINGER);
         if (!dequeued)
            break;
         ctx->InvokeGuarded(t);
      }
      ctx->m_workerCount.fetch_sub(1, std::memory_order_acq_rel);
   }

private:
   template <typename F>
   void InvokeGuarded(F t) noexcept
   {
      m_busyCount.fetch_add(1, std::memory_order_acq_rel);
      if constexpr (Traits::CATCH_EXCEPTIONS) {
         try {
            t();
         }
         catch (const std::exception & e) {
            std::ostringstream logging;
            logging << "Uncaught exception in thread " << std::this_thread::get_id() << ": "
                    << e.what();
            m_logger(logging.str());
         }
         catch (...) {
            std::ostringstream logging;
            logging << "Uncaught exception in thread " << std::this_thread::get_id();
            m_logger(logging.str());
         }
      } else {
         t();
      }
      m_busyCount.fetch_sub(1, std::memory_order_acq_rel);
   }

   moodycamel::BlockingConcurrentQueue<Task> m_queue;
   const std::function<void(std::string)> m_logger;
   std::atomic_bool m_stopped;
   std::atomic_uint32_t m_workerCount;
   std::atomic_uint32_t m_busyCount;
};

template <typename Traits>
WorkerPool<Traits>::WorkerPool(std::function<void(std::string)> logger,
                               std::function<std::chrono::steady_clock::time_point()> now)
   : m_timer(std::make_shared<TimerCtx>(std::move(now)))
   , m_ctx(std::make_shared<WorkerCtx>(std::move(logger)))
{
   for (size_t i = 0; i < Traits::MIN_SIZE; ++i)
      std::thread(WorkerCtx::RunMandatoryWorker, m_ctx).detach();
   std::thread(TimerCtx::RunTimer, m_timer).detach();
}

template <typename Traits>
WorkerPool<Traits>::~WorkerPool()
{
   m_timer->Stop();
   m_ctx->Stop();
   for (size_t i = 0; i < m_ctx->GetWorkerCount(); ++i)
      m_ctx->AddTask([] {
         std::this_thread::sleep_for(100ms);
      });
   if constexpr (Traits::JOIN_THREADS) {
      while (m_ctx->GetWorkerCount())
         std::this_thread::yield();
   }
}

template <typename Traits>
void WorkerPool<Traits>::ExecuteTask(Task task)
{
   m_ctx->AddTask(std::move(task));
   uint32_t workerCount = m_ctx->GetWorkerCount();
   if (workerCount < Traits::MAX_SIZE)
      if (workerCount == m_ctx->GetBusyCount())
         std::thread(WorkerCtx::RunOptionalWorker, m_ctx).detach();
}

template <typename Traits>
void WorkerPool<Traits>::ExecuteTaskIn(std::chrono::milliseconds time, Task task)
{
   m_timer->Schedule(time, [ctx = m_ctx, t = std::move(task)]() mutable {
      ctx->AddTask(std::move(t));
   });
}

template <typename Traits>
void WorkerPool<Traits>::ExecuteTaskAt(std::chrono::steady_clock::time_point time, Task task)
{
   m_timer->Schedule(time, [ctx = m_ctx, t = std::move(task)]() mutable {
      ctx->AddTask(std::move(t));
   });
}

template <typename Traits>
size_t WorkerPool<Traits>::GetWorkerCount() const
{
   return m_ctx->GetWorkerCount();
}


template class WorkerPool<DefaultWorkerPoolTraits>;

} // namespace async
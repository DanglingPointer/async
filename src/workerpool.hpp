#ifndef ASYNC_WORKERPOOL_HPP
#define ASYNC_WORKERPOOL_HPP

#include <chrono>
#include <functional>
#include <memory>

namespace async {
namespace internal {

template <typename F>
struct AlwaysCopyable : F
{
public:
   AlwaysCopyable(const F & f)
      : F(f)
   {}
   AlwaysCopyable(F && f)
      : F(std::move(f))
   {}
   AlwaysCopyable(AlwaysCopyable && cc)
      : AlwaysCopyable(static_cast<F &&>(cc))
   {}
   AlwaysCopyable(const AlwaysCopyable & c)
      : AlwaysCopyable(static_cast<F &&>(const_cast<AlwaysCopyable &>(c)))
   {}
};

} // namespace internal

struct DefaultWorkerPoolTraits
{
   static constexpr size_t MIN_SIZE = 2U;
   static constexpr size_t MAX_SIZE = 5U;
   static constexpr auto MAX_LINGER = std::chrono::minutes(3);
   static constexpr auto TIMER_RESOLUTION = std::chrono::milliseconds(100);
   using TaskType = std::function<void()>;
   static constexpr bool JOIN_THREADS = true;
   static constexpr bool CATCH_EXCEPTIONS = true;
   static constexpr bool WITH_TIMER = true;

   static_assert(MAX_SIZE >= MIN_SIZE);
};

template <typename Traits>
class WorkerPool
{
public:
   using Task = typename Traits::TaskType;
   static constexpr size_t MIN_WORKER_COUNT = Traits::MIN_SIZE;
   static constexpr size_t MAX_WORKER_COUNT = Traits::MAX_SIZE;

   WorkerPool(
      std::function<void(std::string)> logger,
      std::function<bool()> onWorkerStartHandler = [] { return true; },
      std::function<void()> onWorkerStopHandler = []{},
      std::function<std::chrono::steady_clock::time_point()> now = std::chrono::steady_clock::now);
   ~WorkerPool();

   template <typename F>
   void Execute(F && f)
   {
      ExecuteTask(internal::AlwaysCopyable(std::forward<F>(f)));
   }
   template <typename Rep, typename Period, typename F>
   void ExecuteIn(std::chrono::duration<Rep, Period> delay, F && f)
   {
      ExecuteTaskIn(delay, internal::AlwaysCopyable(std::forward<F>(f)));
   }
   template <typename Clock, typename Dur, typename F>
   void ExecuteAt(std::chrono::time_point<Clock, Dur> when, F && f)
   {
      ExecuteTaskAt(when, internal::AlwaysCopyable(std::forward<F>(f)));
   }
   void ExecuteTask(Task task);
   void ExecuteTaskIn(std::chrono::milliseconds delay, Task task);
   void ExecuteTaskAt(std::chrono::steady_clock::time_point when, Task task);
   size_t GetWorkerCount() const;

private:
   class WorkerCtx;
   class TimerCtx;

   const std::shared_ptr<TimerCtx> m_timer;
   const std::shared_ptr<WorkerCtx> m_ctx;
};


} // namespace async

#endif // ASYNC_WORKERPOOL_HPP

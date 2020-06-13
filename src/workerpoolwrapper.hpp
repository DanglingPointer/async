#ifndef ASYNC_WORKERPOOLWRAPPER_HPP
#define ASYNC_WORKERPOOLWRAPPER_HPP

#include "workerpool.hpp"
#include "workerpool.cpp"
#include <tuple>

class ThreadPoolExecutor
{
   template <typename Traits>
   struct Manager
   {
      using Pool = async::WorkerPool<Traits>;

      static void * Create(std::function<void(std::string)> logger,
                           std::function<bool()> onWorkerStartHandler,
                           std::function<void()> onWorkerStopHandler)
      {
         auto p = std::make_unique<Pool>(std::move(logger),
                                         std::move(onWorkerStartHandler),
                                         std::move(onWorkerStopHandler));
         return p.release();
      }
      static void Execute(void * pool, std::function<void()> && task)
      {
         static_cast<Pool *>(pool)->ExecuteTask(std::move(task));
      }
      static void Destroy(void * pool)
      {
         auto * ptr = static_cast<Pool *>(pool);
         delete ptr;
      }
   };
   struct NullManager
   {
      static void *
         Create(std::function<void(std::string)>, std::function<bool()>, std::function<void()>)
      {
         return nullptr;
      }
      static void Execute(void *, std::function<void()> &&) {}
      static void Destroy(void *) {}
   };
   template <size_t POOL_SIZE, size_t MAX_POOL_SIZE, size_t LINGER_SEC>
   struct ThreadPoolTraits
   {
      static constexpr size_t MIN_SIZE = POOL_SIZE;
      static constexpr size_t MAX_SIZE = MAX_POOL_SIZE;
      static constexpr auto MAX_LINGER = std::chrono::seconds(LINGER_SEC);
      using TaskType = std::function<void()>;
      static constexpr auto TIMER_RESOLUTION = std::chrono::milliseconds(100);
      static constexpr bool JOIN_THREADS = true;
      static constexpr bool CATCH_EXCEPTIONS = false;
      static constexpr bool WITH_TIMER = false;
   };

public:
   std::function<bool()> m_ProcOnWorkerStartHandler;
   std::function<void()> m_ProcOnWorkerStopHandler;

   ThreadPoolExecutor()
      : m_ProcOnWorkerStartHandler([] {
         return true;
      })
      , m_ProcOnWorkerStopHandler([] {})
      , m_pool(nullptr)
   {
      SetupPoolMethods<NullManager>();
   }
   ~ThreadPoolExecutor()
   {
      m_destroy(m_pool);
   }
   bool Initialized()
   {
      return m_pool != nullptr;
   }
   void close()
   {
      m_destroy(m_pool);
      m_pool = nullptr;
      SetupPoolMethods<NullManager>();
   }
   template <size_t POOL_SIZE, size_t MAX_POOL_SIZE, size_t LINGER_SEC = 180>
   void open(std::function<void(std::string)> logger = [](auto) {})
   {
      assert(!m_pool);
      assert(m_ProcOnWorkerStartHandler);
      assert(m_ProcOnWorkerStopHandler);
      using Traits = ThreadPoolTraits<POOL_SIZE, MAX_POOL_SIZE, LINGER_SEC>;
      SetupPoolMethods<Manager<Traits>>();
      m_pool = m_create(std::move(logger), m_ProcOnWorkerStartHandler, m_ProcOnWorkerStopHandler);
   }
   template <class F, class... Args>
   void queue(F && f, Args &&... args)
   {
      assert(m_pool);
      std::function<void()> task(
         [f = std::forward<F>(f), params = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            std::apply(f, std::move(params));
         });
      m_execute(m_pool, std::move(task));
   }

private:
   void * m_pool;

   void * (*m_create)(std::function<void(std::string)>,
                      std::function<bool()>,
                      std::function<void()>);
   void (*m_execute)(void *, std::function<void()> &&);
   void (*m_destroy)(void *);

   template <typename T>
   void SetupPoolMethods()
   {
      m_create = &T::Create;
      m_execute = &T::Execute;
      m_destroy = &T::Destroy;
   }
};

#endif // ASYNC_WORKERPOOLWRAPPER_HPP

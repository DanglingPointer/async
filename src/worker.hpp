#ifndef ASYNC_WORKER_HPP
#define ASYNC_WORKER_HPP

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>

namespace async {

class Worker
{
public:
   struct Config
   {
      std::string name;
      size_t capacity;
      std::function<void(std::string_view /*worker*/, std::string_view /*ex*/)> exceptionHandler;
   };
   using Task = std::function<void()>;

   Worker(const Config & config);
   ~Worker();

   void Schedule(Task && work);
   bool TrySchedule(Task && work);
   void Schedule(std::chrono::milliseconds delay, Task && work);
   bool TrySchedule(std::chrono::milliseconds delay, Task && work);

private:
   void Run();
   void GetNextTask(Task & out);

   const Config m_config;
   bool m_stop;

   std::multimap<std::chrono::steady_clock::time_point, Task> m_tasks;
   std::mutex m_tasksSync;
   std::condition_variable m_filledSignal;
   std::condition_variable m_emptiedSignal;

   std::thread m_thread;
};

} // namespace async

#endif // ASYNC_WORKER_HPP

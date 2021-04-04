#include "worker.hpp"

namespace async {

using namespace std::chrono_literals;

Worker::Worker(const async::Worker::Config & config)
   : m_config(config)
   , m_stop(false)
   , m_thread([this] {
      Run();
   })
{}

Worker::~Worker()
{
   Schedule([this] {
      m_stop = true;
   });
   m_thread.join();
}

void Worker::Schedule(Task && work)
{
   Schedule(0ms, std::move(work));
}

bool Worker::TrySchedule(Task && work)
{
   return TrySchedule(0ms, std::move(work));
}

void Worker::Schedule(std::chrono::milliseconds delay, Task && work)
{
   auto timeToFire = std::chrono::steady_clock::now() + delay;
   {
      std::unique_lock lock(m_tasksSync);
      m_emptiedSignal.wait(lock, [this] {
         return m_tasks.size() < m_config.capacity;
      });
      m_tasks.emplace(timeToFire, std::move(work));
   }
   m_filledSignal.notify_one();
}

bool Worker::TrySchedule(std::chrono::milliseconds delay, Task && work)
{
   auto timeToFire = std::chrono::steady_clock::now() + delay;
   {
      std::unique_lock lock(m_tasksSync);
      if (m_tasks.size() >= m_config.capacity)
         return false;
      m_tasks.emplace(timeToFire, std::move(work));
   }
   m_filledSignal.notify_one();
   return true;
}

void Worker::Run()
{
   Task work;

   while (!m_stop) {
      GetNextTask(work);
      m_emptiedSignal.notify_one();
      try {
         work();
      }
      catch (const std::exception & e) {
         m_config.exceptionHandler(m_config.name, e.what());
      }
      catch (...) {
         m_config.exceptionHandler(m_config.name, "unknown");
      }
   }
}

void Worker::GetNextTask(Task & out)
{
   std::unique_lock lock(m_tasksSync);
   m_filledSignal.wait(lock, [this] {
      return !m_tasks.empty();
   });
   for (auto nextTimerTime = std::begin(m_tasks)->first;
        nextTimerTime > std::chrono::steady_clock::now();
        nextTimerTime = std::begin(m_tasks)->first) {
      m_filledSignal.wait_until(lock, nextTimerTime);
   }
   auto it = std::begin(m_tasks);
   out = std::move(it->second);
   m_tasks.erase(it);
}

} // namespace async

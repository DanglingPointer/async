#ifndef ASYNC_CALLBACKS_HPP
#define ASYNC_CALLBACKS_HPP

#include <atomic>
#include <functional>
#include <limits>
#include <thread>
#include <utility>

namespace async {
namespace v2 {

namespace internal {

struct Counter
{
   using RefCountType = uint64_t;

   static constexpr RefCountType CALLBACKS_ALIVE_MASK =
      std::numeric_limits<RefCountType>::max() >> 1u;
   static constexpr RefCountType MANAGER_ALIVE_MASK =
      std::numeric_limits<RefCountType>::max() & ~CALLBACKS_ALIVE_MASK;

   void RemoveManager() noexcept
   {
      RefCountType count = m_refcount.fetch_and(~MANAGER_ALIVE_MASK);
      if (count == MANAGER_ALIVE_MASK)
         delete this;
   }
   void RemoveCallback() noexcept
   {
      RefCountType count = m_refcount.fetch_sub(1u);
      if (count == 1u)
         delete this;
   }
   bool HasManager() const noexcept
   {
      return m_refcount.load() & MANAGER_ALIVE_MASK;
   }
   bool HasCallbacks() const noexcept
   {
      return m_refcount.load() & CALLBACKS_ALIVE_MASK;
   }
   void AddCallback()
   {
      auto prevCount = m_refcount.fetch_add(1);
      if (prevCount == std::numeric_limits<RefCountType>::max()) {
         m_refcount.fetch_sub(1);
         throw std::runtime_error("Number of callbacks exceeds max capacity");
      }
   }
   bool CheckThread() const noexcept
   {
      return m_mgrThread == std::this_thread::get_id();
   }

private:
   std::atomic<RefCountType> m_refcount = MANAGER_ALIVE_MASK;
   const std::thread::id m_mgrThread = std::this_thread::get_id();
};

} // namespace internal

template <typename... TArgs>
class Callback
{
   using Function = std::function<void(TArgs...)>;

public:
   Callback() noexcept
      : Callback(nullptr, nullptr)
   {}

   Callback(const Callback & other)
      : Callback(other.m_func, other.counter)
   {}

   Callback(Callback && other) noexcept(std::is_nothrow_swappable_v<Function>)
      : Callback()
   {
      other.Swap(*this);
   }

   Callback & operator=(const Callback & other)
   {
      Callback(other).Swap(*this);
      return *this;
   }

   Callback & operator=(Callback && other) noexcept(std::is_nothrow_swappable_v<Function>)
   {
      Callback(std::move(other)).Swap(*this);
      return *this;
   }

   ~Callback()
   {
      if (m_counter)
         m_counter->RemoveCallback();
   }

   template <typename... Ts>
   void operator()(Ts &&... args) const
   {
      assert(m_counter);
      assert(m_func);
      assert(m_counter->CheckThread() && "Manager might die while executing cb");
      if (m_counter->HasManager())
         m_func(std::forward<Ts>(args)...);
   }

   bool IsOwnerAlive() const
   {
      return m_counter && m_counter->HasManager();
   }

private:
   friend class CallbackManager;

   template <typename F>
   Callback(F && f, internal::Counter * counter)
      : m_func(std::forward<F>(f))
      , m_counter(counter)
   {
      if (m_counter)
         m_counter->AddCallback();
   }

   void Swap(Callback & other) noexcept(std::is_nothrow_swappable_v<Function>)
   {
      std::swap(m_func, other.m_func);
      std::swap(m_counter, other.m_counter);
   }

   Function m_func;
   internal::Counter * m_counter;
};

} // namespace v2
} // namespace async

#endif // ASYNC_CALLBACKS_HPP

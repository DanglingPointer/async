#ifndef ASYNC_CALLBACKMANAGER_HPP
#define ASYNC_CALLBACKMANAGER_HPP

#include <memory>
#include <type_traits>
#include "callbacks.hpp"

namespace async {
namespace v2 {

class CallbackManager
{
public:
   // Adapter is needed because C++ can't deduce return type of a template function
   // (i.e. CallbackManager::Cb() )
   template <typename F>
   struct Adapter
   {
      static_assert(std::is_reference_v<F> || std::is_pointer_v<F>);

      template <typename... TArgs>
      operator Callback<TArgs...>()
      {
         if constexpr (std::is_same_v<std::decay_t<F>, nullptr_t>)
            return Callback<TArgs...>([](TArgs...) {}, counter);
         else
            return Callback<TArgs...>(std::forward<F>(func), counter);
      }

      F func;
      internal::Counter * counter;
   };

   CallbackManager()
      : m_counter(new internal::Counter)
   {}

   CallbackManager(CallbackManager && other) noexcept
      : m_counter(std::exchange(other.m_counter, nullptr))
   {}

   CallbackManager(const CallbackManager &) = delete;

   ~CallbackManager()
   {
      if (m_counter)
         m_counter->RemoveManager();
   }

   template <typename F = nullptr_t>
   auto Cb(F && f = nullptr) const -> Adapter<decltype(f)>
   {
      assert(m_counter);
      return Adapter<decltype(f)>{std::forward<F>(f), m_counter};
   }

   template <typename F = nullptr_t>
   auto operator()(F && f = nullptr) const -> Adapter<decltype(f)>
   {
      return Cb(std::forward<F>(f));
   }

   template <typename F>
   auto Wrap(F && f)
   {
      auto deleter = [](internal::Counter * counter) {
         counter->RemoveCallback();
      };

      m_counter->AddCallback();
      std::unique_ptr<internal::Counter, decltype(deleter)> p(m_counter, deleter);

      return [f = std::forward<F>(f), p = std::move(p)](auto &&... args) {
         if (p->HasManager())
            f(std::forward<decltype(args)>(args)...);
      };
   }

   bool HasPending() const noexcept
   {
      return m_counter && m_counter->HasCallbacks();
   }

private:
   internal::Counter * m_counter;
};

inline const CallbackManager detachedCb;

} // namespace v2
} // namespace async

#endif // ASYNC_CALLBACKMANAGER_HPP

#ifndef ASYNC_CALLBACKOWNER_HPP
#define ASYNC_CALLBACKOWNER_HPP

#include "refcounter.hpp"
#include <optional>

namespace async {
inline namespace v3 {

class CallbackOwner
{
public:
   CallbackOwner()
      : m_handle(RefCounter::New())
   {}

   template <typename F>
   auto Cb(F && f)
   {
      return [f = std::forward<F>(f),
              token = RefCounterSlave(m_handle->Get())](auto &&... args) mutable {
         if (token.Get()->HasMaster())
            f(std::forward<decltype(args)>(args)...);
      };
   }

   template <typename F>
   auto operator()(F && f)
   {
      return Cb(std::forward<F>(f));
   }

   bool HasPendingCallbacks() const noexcept
   {
      return m_handle->Get()->GetSlaveCount();
   }

   void DeactivateCallbacks()
   {
      m_handle.emplace(RefCounter::New());
   }

private:
   std::optional<RefCounterMaster> m_handle;
};


} // namespace v3
} // namespace async
#endif // ASYNC_CALLBACKOWNER_HPP

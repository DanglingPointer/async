#ifndef CALLBACK_HPP
#define CALLBACK_HPP

/**
 *   Copyright 2019 Mikhail Vasilyev
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>

namespace async {
namespace internal {

class AtomicFlagRef
{
public:
   AtomicFlagRef(std::atomic<uint8_t> * block) noexcept;
   bool IsEmpty() const noexcept;
   bool IsAlive() const noexcept;
   bool IsCancelled() const noexcept;
   uint32_t GetId() const noexcept;
   void Activate() noexcept;
   void Deactivate() noexcept;
   void Cancel() noexcept;

private:
   std::atomic<uint8_t> * m_block;

   friend bool operator==(AtomicFlagRef lhs, AtomicFlagRef rhs) noexcept;
};

bool operator==(AtomicFlagRef lhs, AtomicFlagRef rhs) noexcept;
bool operator!=(AtomicFlagRef lhs, AtomicFlagRef rhs) noexcept;

struct CancellerToken
{};

const std::shared_ptr<CancellerToken> & GlobalCancellerToken();

} // namespace internal

template <typename... Rs>
class Callback
{
private:
   template <size_t MAX_SIMULT_CALLBACKS>
   friend class Canceller;
   friend class OnAllCompleted;
   friend class OnAnyCompleted;

   using Func = std::function<void(Rs...)>;

   template <typename F>
   Callback(const std::shared_ptr<internal::CancellerToken> & token, F && f,
            internal::AtomicFlagRef activeFlag)
      : m_token(token)
      , m_callback(std::forward<F>(f))
      , m_flagRef(activeFlag)
   {
      assert(m_flagRef.IsEmpty() || activeFlag.IsAlive());
   }

public:
   Callback(const Callback<Rs...> & other)
      : m_token(other.m_token)
      , m_callback(other.m_callback)
      , m_flagRef(nullptr)
   {
      assert(false && "Try to avoid using copy ctor"); // temp
   }
   Callback(Callback<Rs...> && other) noexcept // noexcept to force std::vector to use this ctor when resizing
      : m_token(std::move(other.m_token))
      , m_callback(std::exchange(other.m_callback, nullptr))
      , m_flagRef(std::exchange(other.m_flagRef, nullptr))
   {}
   Callback<Rs...> & operator=(Callback<Rs...> && other) noexcept
   {
      if (m_flagRef.IsEmpty() || other.m_flagRef.IsEmpty() || m_flagRef != other.m_flagRef) {
         Deactivate();
         m_token = std::move(other.m_token);
         m_callback = std::exchange(other.m_callback, nullptr);
         m_flagRef = std::exchange(other.m_flagRef, nullptr);
      }
      return *this;
   }
   ~Callback()
   {
      Deactivate();
   }
   bool Cancelled() const noexcept
   {
      if (auto t = m_token.lock())
         return !m_flagRef.IsEmpty() && m_flagRef.IsCancelled();
      return true;
   }
   template <typename... TArgs>
   void Invoke(TArgs &&... args) const
   {
      if (m_callback) {
         if (auto t = m_token.lock()) {
            bool cancelled = !m_flagRef.IsEmpty() && m_flagRef.IsCancelled();
            if (!cancelled)
               m_callback(std::forward<TArgs>(args)...);
         }
      }
   }
   template <typename... TArgs>
   void InvokeOneShot(TArgs &&... args)
   {
      Invoke(std::forward<TArgs>(args)...);
      m_callback = nullptr;
   }
   template <typename... TArgs>
   void operator()(TArgs &&... args) const
   {
      Invoke(std::forward<TArgs>(args)...);
   }

private:
   void Deactivate()
   {
      if (!m_flagRef.IsEmpty()) {
         if (auto t = m_token.lock())
            m_flagRef.Deactivate();
      }
   }
   std::weak_ptr<internal::CancellerToken> m_token;
   std::function<void(Rs...)> m_callback;
   internal::AtomicFlagRef m_flagRef;
};

template <typename X, typename... Rs, typename... TArgs>
inline void Schedule(X executor, Callback<Rs...> && cb, TArgs &&... args)
{
   if (cb.Cancelled())
      return;
   executor(
      [cb = std::move(cb), argTuple = std::make_tuple(std::forward<TArgs>(args)...)]() mutable {
         std::apply(cb, std::move(argTuple));
      });
}

} // namespace async

#endif // CALLBACK_HPP
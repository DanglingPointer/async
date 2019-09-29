#ifndef CANCELLER_HPP
#define CANCELLER_HPP

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
 
#include <array>
#include <optional>
#include <stdexcept>
#include "callback.hpp"

namespace async {
namespace internal {

template <size_t N>
struct AtomicFlagsArray : public std::array<std::atomic<uint8_t>, N>
{
   static_assert(ATOMIC_CHAR_LOCK_FREE == 2, "Use future/promise instead");
   static_assert(sizeof(std::atomic<uint8_t>) == sizeof(uint8_t), "Use future/promise instead");
};

uint32_t MakeCallbackId(AtomicFlagRef flagRef, size_t index) noexcept;
size_t GetFlagIndex(uint32_t callbackId) noexcept;
uint32_t GetOperationId(uint32_t callbackId) noexcept;


// Ugly stuff in order to be able to use Canceller::MakeCb() without explicitly specifying some
// extra template arguments.

template <typename F>
struct DeduceCallbackTypeHelper;
template <typename Class, typename R, typename... Args>
struct DeduceCallbackTypeHelper<R (Class::*)(Args...)>
{
   using type = Callback<Args...>;
};
template <typename Class, typename R, typename... Args>
struct DeduceCallbackTypeHelper<R (Class::*)(Args...) const>
{
   using type = Callback<Args...>;
};
template <typename Class, typename R, typename... Args>
struct DeduceCallbackTypeHelper<R (Class::*)(Args...) noexcept>
{
   using type = Callback<Args...>;
};
template <typename Class, typename R, typename... Args>
struct DeduceCallbackTypeHelper<R (Class::*)(Args...) const noexcept>
{
   using type = Callback<Args...>;
};

template <typename F>
struct CallbackTypeDeducer
{
   using type = typename DeduceCallbackTypeHelper<decltype(&F::operator())>::type;
};
template <>
struct CallbackTypeDeducer<nullptr_t>
{
   using type = Callback<>;
};
template <typename R, typename... Args>
struct CallbackTypeDeducer<R (*)(Args...)>
{
   using type = Callback<Args...>;
};
template <typename R, typename... Args>
struct CallbackTypeDeducer<R(Args...)>
{
   using type = Callback<Args...>;
};
template <typename R, typename... Args>
struct CallbackTypeDeducer<R (*)(Args...) noexcept>
{
   using type = Callback<Args...>;
};
template <typename R, typename... Args>
struct CallbackTypeDeducer<R(Args...) noexcept>
{
   using type = Callback<Args...>;
};

} // namespace internal

template <typename F>
using DeducedCallbackType = typename internal::CallbackTypeDeducer<F>::type;

template <size_t N = 128>
class Canceller
{
   using FlagsArray = internal::AtomicFlagsArray<N>;

public:
   static constexpr size_t MAX_SIMULT_CALLBACKS = N;
   using MyT = Canceller<MAX_SIMULT_CALLBACKS>;
   using CallbackId = std::optional<uint32_t>;

   Canceller()
      : m_token(std::make_shared<internal::CancellerToken>())
      , m_lastUsedFlag(std::begin(m_activeFlags))
   {}
   Canceller(MyT &&) noexcept = delete;
   Canceller(const MyT &)
      : Canceller()
   { /* To ensure correct behaviour when inheriting from Canceller */
   }
   ~Canceller()
   {
      while (m_token.use_count() > 1)
         ;
   }
   void InvalidateCallbacks()
   {
      m_token = std::make_shared<internal::CancellerToken>();
      memset(m_activeFlags.data(), 0, m_activeFlags.size());
      m_lastUsedFlag = std::begin(m_activeFlags);
   }
   void CancelCallback(CallbackId & callbackId) noexcept
   {
      if (!callbackId)
         return;
      auto index = internal::GetFlagIndex(*callbackId);
      internal::AtomicFlagRef flag(&m_activeFlags[index]);
      if (flag.GetId() == internal::GetOperationId(*callbackId))
         flag.Cancel();
      callbackId = std::nullopt;
   }
   // Returns false if the callback object no longer exists. Note that
   bool IsActive(CallbackId & callbackId) const noexcept
   {
      if (!callbackId)
         return false;
      auto index = internal::GetFlagIndex(*callbackId);
      internal::AtomicFlagRef flag(&m_activeFlags[index]);
      bool active = flag.GetId() == internal::GetOperationId(*callbackId) && flag.IsAlive() &&
                    !flag.IsCancelled();
      if (!active)
         callbackId = std::nullopt;
      return active;
   }
   template <typename F>
   auto Wrap(F && func) const noexcept
   {
      return [token = std::weak_ptr<internal::CancellerToken>(m_token),
              f = std::forward<F>(func)](auto &&... args) {
         if (auto p = token.lock())
            f(std::forward<decltype(args)>(args)...);
      };
   }
   template <typename F,
             typename = std::enable_if_t<!std::is_same_v<CallbackId *, std::decay_t<F>>>>
   DeducedCallbackType<F> MakeCb(F && callback, CallbackId * callbackId = nullptr) const
   {
      internal::AtomicFlagRef flag = RegisterCallback(callbackId);
      return DeducedCallbackType<F>(m_token, std::forward<F>(callback), flag);
   }
   Callback<> MakeCb(CallbackId * callbackId = nullptr) const
   {
      return MakeCb(nullptr, callbackId);
   }
   template <typename F>
   DeducedCallbackType<F> DetachedCb(F && callback) const
   {
      return DeducedCallbackType<F>(internal::GlobalCancellerToken(), std::forward<F>(callback),
                                    nullptr);
   }
   Callback<> NoCb() const { return Callback<>(nullptr, nullptr, nullptr); }

private:
   internal::AtomicFlagRef RegisterCallback(CallbackId * callbackId) const
   {
      if (!callbackId)
         return nullptr;
      auto index = [this] {
         for (size_t i = 0; i < m_activeFlags.size(); ++i) {
            ++m_lastUsedFlag;
            if (m_lastUsedFlag == std::end(m_activeFlags))
               m_lastUsedFlag = std::begin(m_activeFlags);
            if (!internal::AtomicFlagRef(&(*m_lastUsedFlag)).IsAlive()) {
               return std::distance(std::begin(m_activeFlags), m_lastUsedFlag);
            }
         }
         throw std::runtime_error("Number of callbacks exceeds Canceller capacity");
      }();
      internal::AtomicFlagRef flag(&(*m_lastUsedFlag));
      flag.Activate();
      callbackId->emplace(internal::MakeCallbackId(flag, index));
      return flag;
   }
   std::shared_ptr<internal::CancellerToken> m_token;
   mutable FlagsArray m_activeFlags{};
   mutable typename FlagsArray::iterator m_lastUsedFlag;
};


namespace internal {

template <typename T>
class SynchronizerBase
{
public:
   SynchronizerBase() noexcept
      : m_state(nullptr)
   {}
   template <typename F>
   SynchronizerBase(F && f)
      : m_state(new State(std::forward<F>(f)))
   {
      assert(m_state->listener);
   }
   ~SynchronizerBase() { static_cast<T *>(this)->Detach(); }
   SynchronizerBase(T && other) noexcept
      : m_state(other.m_state)
   {
      other.m_state = nullptr;
   }
   T & operator=(T && other)
   {
      if (m_state != other.m_state) {
         static_cast<T *>(this)->Detach();
         m_state = std::exchange(other.m_state, nullptr);
      }
      return *static_cast<T *>(this);
   }

   template <typename... Rs>
   void Track(Callback<Rs...> & cb)
   {
      static_cast<T *>(this)->Modify(cb);
   }
   template <typename... Rs>
   Callback<Rs...> && Track(Callback<Rs...> && cb)
   {
      static_cast<T *>(this)->Modify(cb);
      return std::move(cb);
   }

protected:
   struct State
   {
      template <typename F>
      explicit State(F && f)
         : trackedCount(10'000U)
         , firedCount(0U)
         , listener(std::forward<F>(f))
      {}
      uint32_t trackedCount;
      uint32_t firedCount;
      std::function<void()> listener;
   };

   State * m_state;
};

} // namespace internal


// Allows to set a listener that will be called once all tracked callbacks have executed AND the
// synchronizer object has gone out of scope (whichever happens last). Can track up to 10000
// callbacks. NOT thread-safe (all tracked callbacks must execute in the same thread).
class OnAllCompleted : public internal::SynchronizerBase<OnAllCompleted>
{
   friend class internal::SynchronizerBase<OnAllCompleted>;
   using Base = internal::SynchronizerBase<OnAllCompleted>;

public:
   OnAllCompleted() noexcept
      : Base()
   {}

   template <typename F>
   OnAllCompleted(F && f)
      : Base(std::forward<F>(f))
   {}

   OnAllCompleted(OnAllCompleted && other) noexcept
      : Base(std::move(other))
   {}

   using Base::operator=;

private:
   void Detach();

   template <typename... Rs>
   void Modify(Callback<Rs...> & cb)
   {
      if (!m_state)
         throw std::runtime_error("OnAllCompleted is in invalid state");

      ++m_state->trackedCount;
      typename Callback<Rs...>::Func temp = std::move(cb.m_callback);
      cb.m_callback = [state = m_state, f = std::move(temp)](Rs... args) mutable {
         if (f)
            f(std::move(args)...);
         if (!state)
            return;

         ++state->firedCount;
         if (state->firedCount == state->trackedCount) {
            state->listener();
            delete state;
         }
         state = nullptr;
      };
   }
};

// Allows to set a listener that will be called once one of the tracked callbacks has executed AND
// the synchronizer object has gone out of scope (whichever happens last). Can track up to 10000
// callbacks. NOT thread-safe (all tracked callbacks must execute in the same thread).
class OnAnyCompleted : public internal::SynchronizerBase<OnAnyCompleted>
{
   friend class internal::SynchronizerBase<OnAnyCompleted>;
   using Base = internal::SynchronizerBase<OnAnyCompleted>;

public:
   OnAnyCompleted() noexcept
      : Base()
   {}

   template <typename F>
   OnAnyCompleted(F && f)
      : Base(std::forward<F>(f))
   {}

   OnAnyCompleted(OnAnyCompleted && other) noexcept
      : Base(std::move(other))
   {}

   using Base::operator=;

private:
   void Detach();

   template <typename... Rs>
   void Modify(Callback<Rs...> & cb)
   {
      if (!m_state)
         throw std::runtime_error("OnAnyCompleted is in invalid state");

      ++m_state->trackedCount;
      typename Callback<Rs...>::Func temp = std::move(cb.m_callback);
      cb.m_callback = [state = m_state, f = std::move(temp)](Rs... args) mutable {
         if (f)
            f(std::move(args)...);
         if (!state)
            return;

         ++state->firedCount;
         if (state->firedCount == 1U && state->trackedCount < 10'000U) {
            state->listener();
         }
         if (state->firedCount == state->trackedCount) {
            delete state;
         }
         state = nullptr;
      };
   }
};

} // namespace async

#endif // CANCELLER_HPP

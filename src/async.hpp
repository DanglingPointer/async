#ifndef ASYNC_HPP
#define ASYNC_HPP
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
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace async {
namespace internal {

template <typename R>
struct SharedState
{
   using Result = R;
   using Callback = std::function<void(std::optional<Result>)>;

   std::atomic_bool hasFuture = false;
   std::atomic_bool active = false;

   Callback GetCallback() const
   {
      std::shared_lock lock(m_mutex);
      return m_onFinished;
   }
   void SetCallback(Callback onFinished)
   {
      std::unique_lock lock(m_mutex);
      m_onFinished = std::move(onFinished);
   }
   bool SetCallbackIfEmpty(Callback onFinished)
   {
      std::unique_lock lock(m_mutex);
      if (m_onFinished)
         return false;
      m_onFinished = std::move(onFinished);
      return true;
   }

private:
   mutable std::shared_mutex m_mutex;
   Callback m_onFinished;
};

} // namespace internal

class AsyncException : public std::runtime_error
{
public:
   explicit AsyncException(const char * what)
      : std::runtime_error(what)
   {}
};

struct Empty
{};

template <typename R>
class Promise;

template <typename R>
class Future
{
   using State = internal::SharedState<R>;

   explicit Future(std::shared_ptr<State> state)
      : m_state(std::move(state))
   {
      m_state->hasFuture = true;
   }

public:
   using Result = typename State::Result;
   using Callback = typename State::Callback;

   Future() = default;
   Future(Future<R> && rhs)
      : m_state(std::move(rhs.m_state))
      , m_canceller(std::exchange(rhs.m_canceller, nullptr))
   {}
   Future(const Future &) = delete;
   Future<R> & operator=(Future<R> && rhs)
   {
      Cancel();
      m_state = std::move(rhs.m_state);
      m_canceller = std::exchange(rhs.m_canceller, nullptr);
   }
   Future<R> & operator=(const Future<R> &) = delete;

   ~Future() { Cancel(); }
   void Cancel()
   {
      if (m_state) {
         m_state->hasFuture = false;
         m_state = nullptr;
      }
      if (m_canceller) {
         m_canceller();
         m_canceller = nullptr;
      }
   }
   Future<R> && Then(Callback cb) &&
   {
      if (!m_state)
         throw AsyncException("Error setting async callback");
      if (!m_state->SetCallbackIfEmpty(std::move(cb)))
         throw AsyncException("Async callback already set");
      return std::move(*this);
   }
   void Then(Callback cb) &
   {
      if (!m_state)
         throw AsyncException("Error setting async callback");
      if (!m_state->SetCallbackIfEmpty(std::move(cb)))
         throw AsyncException("Async callback already set");
   }
   bool IsActive() const { return m_state && m_state->active; }

private:
   std::shared_ptr<State> m_state;
   std::function<void()> m_canceller;

   template <typename R1, typename R2>
   friend Future<Empty> operator&&(Future<R1> && lhs, Future<R2> && rhs);

   template <typename R1, typename R2>
   friend Future<Empty> operator||(Future<R1> && lhs, Future<R2> && rhs);

   friend class Promise<Result>;
};

// Finishes when both tasks have finished
template <typename R1, typename R2>
inline Future<Empty> operator&&(Future<R1> && lhs, Future<R2> && rhs)
{
   auto lhsState = std::move(lhs.m_state);
   auto lhsCanceller = std::exchange(lhs.m_canceller, nullptr);

   auto rhsState = std::move(rhs.m_state);
   auto rhsCanceller = std::exchange(rhs.m_canceller, nullptr);

   auto combinedState = std::make_shared<internal::SharedState<Empty>>();
   combinedState->active = lhsState->active && rhsState->active;

   auto lhsOnFinished = lhsState->GetCallback();
   lhsState->SetCallback([rhsState, combinedState, cb = std::move(lhsOnFinished)](auto r) {
      if (cb)
         cb(std::move(r));

      if (!rhsState->active) {
         combinedState->active = false;
         if (combinedState->hasFuture)
            if (auto onFinished = combinedState->GetCallback())
               onFinished(Empty{});
      }
   });
   auto rhsOnFinished = rhsState->GetCallback();
   rhsState->SetCallback([lhsState, combinedState, cb = std::move(rhsOnFinished)](auto r) {
      if (cb)
         cb(std::move(r));

      if (!lhsState->active) {
         combinedState->active = false;
         if (combinedState->hasFuture)
            if (auto onFinished = combinedState->GetCallback())
               onFinished(Empty{});
      }
   });

   Future<Empty> combinedFuture(combinedState);
   combinedFuture.m_canceller = [
      lhsState, rhsState, lhsCanceller = std::move(lhsCanceller),
      rhsCanceller = std::move(rhsCanceller)
   ]
   {
      lhsState->hasFuture = false;
      rhsState->hasFuture = false;
      if (lhsCanceller)
         lhsCanceller();
      if (rhsCanceller)
         rhsCanceller();
   };
   return combinedFuture;
}

// Whichever task finishes first, cancels the other one
template <typename R1, typename R2>
inline Future<Empty> operator||(Future<R1> && lhs, Future<R2> && rhs)
{
   auto lhsState = std::move(lhs.m_state);
   auto lhsCanceller = std::exchange(lhs.m_canceller, nullptr);

   auto rhsState = std::move(rhs.m_state);
   auto rhsCanceller = std::exchange(rhs.m_canceller, nullptr);

   auto combinedState = std::make_shared<internal::SharedState<Empty>>();
   combinedState->active = lhsState->active && rhsState->active;

   auto lhsOnFinished = lhsState->GetCallback();
   lhsState->SetCallback([rhsState, combinedState, cb = std::move(lhsOnFinished)](auto r) {
      rhsState->hasFuture = false; // cancel rhs
      if (cb)
         cb(std::move(r));

      combinedState->active = false;
      if (combinedState->hasFuture)
         if (auto onFinished = combinedState->GetCallback())
            onFinished(Empty{});
   });

   auto rhsOnFinished = rhsState->GetCallback();
   rhsState->SetCallback([lhsState, combinedState, cb = std::move(rhsOnFinished)](auto r) {
      lhsState->hasFuture = false; // cancel lhs
      if (cb)
         cb(std::move(r));

      combinedState->active = false;
      if (combinedState->hasFuture)
         if (auto onFinished = combinedState->GetCallback())
            onFinished(Empty{});
   });

   Future<Empty> combinedFuture(combinedState);
   combinedFuture.m_canceller = [
      lhsState, rhsState, lhsCanceller = std::move(lhsCanceller),
      rhsCanceller = std::move(rhsCanceller)
   ]
   {
      lhsState->hasFuture = false;
      rhsState->hasFuture = false;
      if (lhsCanceller)
         lhsCanceller();
      if (rhsCanceller)
         rhsCanceller();
   };
   return combinedFuture;
}

using Executor = std::function<void(std::function<void()>)>;

template <typename R>
class Promise
{
public:
   using State = internal::SharedState<R>;
   using Result = typename State::Result;
   using Callback = typename State::Callback;

   explicit Promise(Executor executor)
      : m_executor(std::move(executor))
      , m_state(std::make_shared<State>())
   {
      m_state->active = true;
   }
   Promise(Promise<R> && rhs)
      : m_executor(std::move(rhs.m_executor))
      , m_state(std::move(rhs.m_state))
   {}
   Promise(const Promise<R> &) = delete;
   Promise<R> & operator=(Promise<R> && rhs)
   {
      Terminate();
      m_executor = std::move(rhs.m_executor);
      m_state = std::move(rhs.m_state);
   }
   Promise<R> & operator=(const Promise<R> &) = delete;

   ~Promise() { Terminate(); }
   void Finished(Result r)
   {
      if (!m_state)
         throw AsyncException("No state");
      if (!m_state->active)
         throw AsyncException("Async task already finished");
      m_state->active = false;
      if (m_state->hasFuture)
         if (auto onFinished = m_state->GetCallback())
            m_executor([cb = std::move(onFinished), r = std::move(r), state = m_state] {
               if (state->hasFuture)
                  cb(r);
            });
   }
   Future<R> GetFuture(std::function<void()> canceller = nullptr)
   {
      if (!m_state)
         throw AsyncException("No state");
      if (m_state->hasFuture)
         throw AsyncException("Future already exists");
      Future<R> f(m_state);
      if (canceller)
         f.m_canceller = std::move(canceller);
      return f;
   }
   bool IsCancelled() const { return !m_state || !m_state->hasFuture; }

private:
   void Terminate()
   {
      if (!m_state)
         return;
      bool wasActive = m_state->active.exchange(false);
      if (wasActive && m_state->hasFuture)
         if (auto onFinished = m_state->GetCallback())
            m_executor([cb = std::move(onFinished), state = m_state] {
               if (state->hasFuture)
                  cb(std::nullopt);
            });
   }
   Executor m_executor;
   std::shared_ptr<State> m_state;
};

template <typename R, typename F>
class CopyableWrapper
{
public:
   using MyT = CopyableWrapper<R, F>;

   CopyableWrapper(Promise<R> && promise, F func)
      : m_promise(std::move(promise))
      , m_func(std::move(func))
   {}
   CopyableWrapper(MyT &&) = default;
   MyT & operator=(MyT &&) = default;
   CopyableWrapper(const MyT & rhs)
      : m_promise(std::move(rhs.m_promise))
      , m_func(std::move(rhs.m_func))
   {}
   MyT & operator=(const MyT & rhs)
   {
      m_promise = std::move(rhs.m_promise);
      m_func = std::move(rhs.m_func);
   }
   template <typename... TArgs>
   void operator()(TArgs &&... args) const
   {
      if (!m_promise.IsCancelled())
         m_promise.Finished(m_func(std::forward<TArgs>(args)...));
   }

private:
   mutable Promise<R> m_promise;
   mutable F m_func;
};

template <typename R, typename F>
inline auto EmbedPromiseIntoTask(Promise<R> && p, F && f)
{
   using Func = std::remove_reference_t<F>;
   return CopyableWrapper<R, Func>(std::move(p), std::forward<F>(f));
}


class Canceller
{
   struct Token
   {};

public:
   Canceller()
      : m_token(std::make_shared<Token>())
   {}
   void Reset() { m_token = std::make_shared<Token>(); }
   template <typename F>
   auto MakeCb(F && callback) const
   {
      return [token = std::weak_ptr<Token>(m_token), cb = std::forward<F>(callback)](auto &&... args)
      {
         if (!token.expired())
            cb(std::forward<decltype(args)>(args)...);
      };
   }

private:
   std::shared_ptr<Token> m_token;
};

} // namespace async

#endif // ASYNC_HPP

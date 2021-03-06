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

#include "callback.hpp"
#include "canceller.hpp"

/**
 * Flag layout:
 * 0
 * 0 1 2 3 4 5 6 7 8
 * +-+-+-+-+-+-+-+-+
 * |    ID=6   |C|A|
 * +-+-+-+-+-+-+-+-+
 *
 *
 * CallbackId layout:
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    ID=6   |                   INDEX=26                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *
 * A - alive bit. 1 = callback object exists, 0 = callback has gone out of scope.
 *
 * C - cancelled bit. Set when callback is being cancelled explicitly by its creator
 *     (i.e. via Canceller::CancelCallback()).
 *
 * ID - unique operation id in order to be able to reuse a flag once its previous operation has
 *      finished. Incremented each time the flag is used for a new operation.
 *      NB! Will wrap around eventually which might cause CallbackId clashes. To mitigate this
 *      somewhat, a CallbackId is set to nullopt once we know that the operation is inactive or
 *      cancelled.
 *
 * INDEX - position of flag in the array storing all flags (i.e. m_activeFlags in Canceller).
 *
 */

namespace async::v1 {

namespace {
constexpr uint8_t MASK_ALIVE = 1 << 7;
constexpr uint8_t MASK_CANCELLED = 1 << 6;
constexpr int ID_LENGTH = 6;
constexpr uint8_t MASK_ID = (1 << ID_LENGTH) - 1;
} // namespace


namespace internal {

AtomicFlagRef::AtomicFlagRef(std::atomic<uint8_t> * block) noexcept
   : m_block(block)
{}
bool AtomicFlagRef::IsEmpty() const noexcept
{
   return m_block == nullptr;
}
bool AtomicFlagRef::IsAlive() const noexcept
{
   assert(!IsEmpty());
   return (m_block->load() & MASK_ALIVE) != 0;
}
bool AtomicFlagRef::IsCancelled() const noexcept
{
   assert(!IsEmpty());
   return (m_block->load() & MASK_CANCELLED) != 0;
}
uint32_t AtomicFlagRef::GetId() const noexcept
{
   assert(!IsEmpty());
   return m_block->load() & MASK_ID;
}
void AtomicFlagRef::Activate() noexcept
{
   assert(!IsEmpty());
   m_block->fetch_add(1);
   m_block->fetch_or(MASK_ALIVE);
   m_block->fetch_and(static_cast<uint8_t>(~MASK_CANCELLED));
}
void AtomicFlagRef::Deactivate() noexcept
{
   assert(!IsEmpty());
   m_block->fetch_and(static_cast<uint8_t>(~MASK_ALIVE));
}
void AtomicFlagRef::Cancel() noexcept
{
   assert(!IsEmpty());
   m_block->fetch_or(MASK_CANCELLED);
}

bool operator==(AtomicFlagRef lhs, AtomicFlagRef rhs) noexcept
{
   return lhs.m_block == rhs.m_block;
}
bool operator!=(AtomicFlagRef lhs, AtomicFlagRef rhs) noexcept
{
   return !(lhs == rhs);
}

const std::shared_ptr<CancellerToken> & GlobalCancellerToken()
{
   static auto s_token = std::make_shared<CancellerToken>();
   return s_token;
}

uint32_t MakeCallbackId(AtomicFlagRef flagRef, size_t index) noexcept
{
   uint32_t ret = index << ID_LENGTH;
   ret |= flagRef.GetId();
   return ret;
}
size_t GetFlagIndex(uint32_t callbackId) noexcept
{
   return callbackId >> ID_LENGTH;
}
uint32_t GetOperationId(uint32_t callbackId) noexcept
{
   return callbackId & MASK_ID;
}

} // namespace internal

void OnAllCompleted::Detach()
{
   if (m_state) {
      assert(m_state->trackedCount >= 10'000U);

      m_state->trackedCount -= 10'000U;
      if (m_state->firedCount == m_state->trackedCount) {
         m_state->listener();
         delete m_state;
         m_state = nullptr;
      }
   }
}

void OnAnyCompleted::Detach()
{
   if (m_state) {
      assert(m_state->trackedCount >= 10'000U);

      m_state->trackedCount -= 10'000U;
      if (m_state->firedCount > 0U) {
         m_state->listener();
      }
      if (m_state->firedCount == m_state->trackedCount) {
         delete m_state;
         m_state = nullptr;
      }
   }
}

} // namespace async::v1
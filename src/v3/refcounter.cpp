#include "refcounter.hpp"
#include <cassert>
#include <utility>

#ifndef NDEBUG
#include <memory>
namespace {
auto memcheckDeleter = [](std::atomic_uint * count) {
   assert(*count == 0u);
   delete count;
};
std::unique_ptr<std::atomic_uint, decltype(memcheckDeleter)> memchecker(new std::atomic_uint(0u),
                                                                        memcheckDeleter);
} // namespace
#endif

namespace async {
inline namespace v3 {

namespace {
constexpr uint64_t MASTER_MASK = UINT64_MAX - UINT64_MAX / 2;
constexpr uint64_t SLAVE_MASK = ~MASTER_MASK;

} // namespace


RefCounter * RefCounter::New()
{
#ifndef NDEBUG
   (*memchecker)++;
#endif
   return new RefCounter;
}

RefCounter::~RefCounter()
{
#ifndef NDEBUG
   assert(m_state.load(std::memory_order_relaxed) == 0u);
   (*memchecker)--;
#endif
}

void RefCounter::AddMaster() noexcept
{
   assert(!HasMaster());
   m_state.fetch_or(MASTER_MASK, std::memory_order_relaxed);
}

void RefCounter::AddSlave() noexcept
{
   assert(m_state.load(std::memory_order_relaxed) < UINT64_MAX);
   m_state.fetch_add(1u, std::memory_order_relaxed);
}

void RefCounter::RemoveMaster() noexcept
{
   assert(HasMaster());
   uint64_t state = m_state.fetch_and(~MASTER_MASK, std::memory_order_acq_rel);
   if (state == MASTER_MASK)
      delete this;
}

void RefCounter::RemoveSlave() noexcept
{
   assert(GetSlaveCount() > 0u);
   uint64_t count = m_state.fetch_sub(1u, std::memory_order_acq_rel);
   if (count == 1u)
      delete this;
}

bool RefCounter::HasMaster() const noexcept
{
   return m_state.load(std::memory_order_relaxed) & MASTER_MASK;
}

uint64_t RefCounter::GetSlaveCount() const noexcept
{
   return m_state.load(std::memory_order_relaxed) & SLAVE_MASK;
}

RefCounter::RefCounter()
   : m_state(0u)
{}


template <typename T>
RefCounterWrapper<T>::RefCounterWrapper(RefCounter * refcounter) noexcept
   : m_refcounter(refcounter)
{
   static_cast<T *>(this)->AddMyself(m_refcounter);
}

template <typename T>
RefCounterWrapper<T>::~RefCounterWrapper()
{
   static_cast<T *>(this)->RemoveMyself(m_refcounter);
}

template <typename T>
RefCounterWrapper<T>::RefCounterWrapper(const RefCounterWrapper & other) noexcept
   : RefCounterWrapper(other.m_refcounter)
{}

template <typename T>
RefCounterWrapper<T>::RefCounterWrapper(RefCounterWrapper && other) noexcept
   : RefCounterWrapper()
{
   other.Swap(*this);
}

template <typename T>
RefCounterWrapper<T> & RefCounterWrapper<T>::operator=(const RefCounterWrapper & other) noexcept
{
   RefCounterWrapper(other).Swap(*this);
   return *this;
}

template <typename T>
RefCounterWrapper<T> & RefCounterWrapper<T>::operator=(RefCounterWrapper && other) noexcept
{
   RefCounterWrapper(std::move(other)).Swap(*this);
   return *this;
}

template <typename T>
RefCounter * RefCounterWrapper<T>::Get() noexcept
{
   return m_refcounter;
}

template <typename T>
const RefCounter * RefCounterWrapper<T>::Get() const noexcept
{
   return m_refcounter;
}

template <typename T>
void RefCounterWrapper<T>::Swap(RefCounterWrapper & other) noexcept
{
   std::swap(m_refcounter, other.m_refcounter);
}

template class RefCounterWrapper<RefCounterSlave>;
template class RefCounterWrapper<RefCounterMaster>;


void RefCounterSlave::AddMyself(RefCounter * counter) noexcept
{
   if (counter)
      counter->AddSlave();
}

void RefCounterSlave::RemoveMyself(RefCounter * counter) noexcept
{
   if (counter)
      counter->RemoveSlave();
}


void RefCounterMaster::AddMyself(RefCounter * counter) noexcept
{
   if (counter)
      counter->AddMaster();
}

void RefCounterMaster::RemoveMyself(RefCounter * counter) noexcept
{
   if (counter)
      counter->RemoveMaster();
}


void swap(RefCounterSlave & lhs, RefCounterSlave & rhs) noexcept
{
   lhs.Swap(rhs);
}

void swap(RefCounterMaster & lhs, RefCounterMaster & rhs) noexcept
{
   lhs.Swap(rhs);
}

} // namespace v3
} // namespace async

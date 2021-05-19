#ifndef ASYNC_REFCOUNTER_HPP
#define ASYNC_REFCOUNTER_HPP

#include <atomic>
#include <cstdint>

namespace async {
inline namespace v3 {

class RefCounter
{
public:
   static RefCounter * New();

   ~RefCounter();

   void AddMaster() noexcept;
   void AddSlave() noexcept;

   void RemoveMaster() noexcept;
   void RemoveSlave() noexcept;

   bool HasMaster() const noexcept;
   uint64_t GetSlaveCount() const noexcept;

private:
   RefCounter();

   std::atomic<uint64_t> m_state;
};

template <typename T>
class RefCounterWrapper
{
public:
   explicit RefCounterWrapper(RefCounter * refcounter = nullptr) noexcept;
   ~RefCounterWrapper();

   RefCounterWrapper(const RefCounterWrapper & other) noexcept;
   RefCounterWrapper(RefCounterWrapper && other) noexcept;

   RefCounterWrapper & operator=(const RefCounterWrapper & other) noexcept;
   RefCounterWrapper & operator=(RefCounterWrapper && other) noexcept;

   RefCounter * Get() noexcept;
   const RefCounter * Get() const noexcept;

   void Swap(RefCounterWrapper & other) noexcept;

private:
   RefCounter * m_refcounter;
};

class RefCounterSlave : public RefCounterWrapper<RefCounterSlave>
{
public:
   using RefCounterWrapper<RefCounterSlave>::RefCounterWrapper;

   void AddMyself(RefCounter * counter) noexcept;
   void RemoveMyself(RefCounter * counter) noexcept;
};

class RefCounterMaster : public RefCounterWrapper<RefCounterMaster>
{
public:
   using RefCounterWrapper<RefCounterMaster>::RefCounterWrapper;
   RefCounterMaster(const RefCounterMaster &) = delete;
   RefCounterMaster(RefCounterMaster &&) = default;
   RefCounterMaster & operator=(RefCounterMaster &&) = default;

   void AddMyself(RefCounter * counter) noexcept;
   void RemoveMyself(RefCounter * counter) noexcept;
};

void swap(RefCounterSlave & lhs, RefCounterSlave & rhs) noexcept;
void swap(RefCounterMaster & lhs, RefCounterMaster & rhs) noexcept;

} // namespace v3
} // namespace async

#endif // ASYNC_REFCOUNTER_HPP

#ifndef POOLPTR_HPP
#define POOLPTR_HPP

#include <cassert>
#include <cstddef>
#include <type_traits>

namespace mem {

template <typename T>
class PoolPtr
{
   template <size_t... Ss>
   friend class Pool;

   using Myt = PoolPtr<T>;
   using DeleterFn = void(*)(void *, T *);

   PoolPtr(std::add_pointer_t<T> obj, void * mempool, DeleterFn dealloc) noexcept
       : m_obj(obj)
       , m_pool(mempool)
       , m_dealloc(dealloc)
   {
      assert(m_obj);
      assert(m_pool);
      assert(m_dealloc);
   }

public:
   PoolPtr() noexcept
       : m_obj(nullptr)
       , m_pool(nullptr)
       , m_dealloc([](auto, auto){})
   {}
   PoolPtr(Myt && other) noexcept
       : m_obj(std::exchange(other.m_obj, nullptr))
       , m_pool(other.m_pool)
       , m_dealloc(other.m_dealloc)
   {}
   ~PoolPtr()
   {
      if (m_obj)
         m_dealloc(m_pool, m_obj);
   }
   Myt & operator=(Myt && other) noexcept
   {
      if (m_obj != other.m_obj)
         m_obj = std::exchange(other.m_obj, nullptr);
      if (m_pool != other.m_pool) {
         m_pool = other.m_pool;
         m_dealloc = other.m_dealloc;
      }
      return *this;
   }
   T * Get() const noexcept
   {
      return m_obj;
   }
   T & operator*() const noexcept
   {
      return *Get();
   }
   T * operator->() const noexcept
   {
      return Get();
   }
   operator bool() const noexcept
   {
      return m_obj != nullptr;
   }
   void Reset() noexcept
   {
      if (m_obj) {
         m_dealloc(m_pool, m_obj);
         m_obj = nullptr;
      }
   }
   T * Release() noexcept
   {
      return std::exchange(m_obj, nullptr);
   }

private:
   T * m_obj;
   void * m_pool;
   DeleterFn m_dealloc;
};

} // mem

#endif // POOLPTR_HPP
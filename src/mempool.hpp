#ifndef MEMPOOL_HPP
#define MEMPOOL_HPP

#include <atomic>
#include <cassert>
#include <cstddef>
#include <limits>
#include <list>
#include <memory>
#include <type_traits>
#include "poolptr.hpp"

namespace mem {
namespace internal {

template <size_t N>
struct Block
{
   using Myt = Block<N>;
   static constexpr size_t SIZE = N;

   std::aligned_storage_t<SIZE, alignof(std::max_align_t)> buffer;
   std::atomic_flag taken = ATOMIC_FLAG_INIT;
};

template <size_t N, typename T>
Block<N> * get_block(T * ptr) noexcept
{
   return reinterpret_cast<Block<N> *>( reinterpret_cast<char *>(ptr) - offsetof(Block<N>, buffer) );
}
template <size_t N>
void * get_buffer(Block<N> & block) noexcept
{
   return &block.buffer;
}

template <typename P, typename T, bool FITS>
struct PoolFinder;

template <typename P, typename T>
struct PoolFinder<P, T, true>
{
   using Type = P;
};
template <typename P, typename T>
struct PoolFinder<P, T, false>
{
   using BasePool = typename P::Base;
   using Type = typename PoolFinder<BasePool , T, (sizeof(T) <= BasePool::BLOCK_SIZE)>::Type;
};
template <typename P, typename T>
using SuitablePool = typename PoolFinder<P, T, (sizeof(T) <= P::BLOCK_SIZE)>::Type;

} // internal


// All methods in Pool must be called from the same thread, but a PoolPtr obtained through Pool::Make can be
// marshalled to any other thread and die wherever it wants.

template <size_t... Ss>
class Pool;

template <size_t S, size_t... Ss>
class Pool<S, Ss...> : protected Pool<Ss...>
{
public:
   static constexpr size_t BLOCK_SIZE = S;
   using Block = internal::Block<BLOCK_SIZE>;
   using Base = Pool<Ss...>;

   using Myt = Pool<S, Ss...>;
   static_assert(BLOCK_SIZE < Base::BLOCK_SIZE, "Block sizes must be in ascending order");

   explicit Pool(size_t block_count)
      : Base(block_count)
      , m_blocks(block_count)
   {
      assert(block_count > 0);
   }

   template <typename T, typename... TArgs>
   PoolPtr<T> Make(TArgs &&... args)
   {
      using P = internal::SuitablePool<Myt, T>;

      auto deallocator = [] (void * pool, T * obj) {
         static_cast<Myt *>(pool)->P::Deallocate(obj);
      };
      return PoolPtr<T>(
         P::template Allocate<T>(std::forward<TArgs>(args)...),
         this,
         deallocator);
   }

   template <typename T, typename... TArgs>
   std::shared_ptr<T> MakeShared(TArgs &&... args)
   {
      using P = internal::SuitablePool<Myt, T>;

      auto deallocator = [this] (T * obj) {
         this->P::Deallocate(obj);
      };
      return std::shared_ptr<T>(
         P::template Allocate<T>(std::forward<TArgs>(args)...),
         deallocator);
   }

   void ShrinkToFit()
   {
      Base::ShrinkToFit();
      m_blocks.remove_if([] (Block & b) {
         return !b.taken.test_and_set(std::memory_order_acquire);
      });
   }

   void Resize(size_t new_block_count)
   {
      assert(new_block_count > 0);
      Base::Resize(new_block_count);

      int extra_blocks = static_cast<int>(new_block_count) - static_cast<int>(m_blocks.size());
      while (extra_blocks > 0) {
         m_blocks.emplace_front();
         --extra_blocks;
      }
      if (extra_blocks < 0) {
         m_blocks.remove_if([&] (Block & b) {
            bool should_remove = extra_blocks < 0 && !b.taken.test_and_set(std::memory_order_acquire);
            extra_blocks += should_remove;
            return should_remove;
         });
      }
   }

   size_t GetBlockCount() const noexcept
   {
      return Base::GetBlockCount() + m_blocks.size();
   }

   size_t GetSize() const noexcept
   {
      return Base::GetSize() + m_blocks.size() * BLOCK_SIZE;
   }

protected:
   template <typename T, typename... TArgs>
   T * Allocate(TArgs &&... args)
   {
      static_assert(alignof(T) <= alignof(std::max_align_t));
      static_assert(sizeof(T) <= BLOCK_SIZE);

      T * ret = nullptr;

      auto it = std::begin(m_blocks);
      for (; it != std::end(m_blocks); ++it) {
         if (!it->taken.test_and_set(std::memory_order_acquire)) {
            m_blocks.splice(std::cend(m_blocks), m_blocks, it); // move to the end
            break;
         }
      }
      if (it == std::end(m_blocks)) {
         it = m_blocks.emplace(it);
      }
      try {
         ret = new(internal::get_buffer(*it)) T(std::forward<TArgs>(args)...);
      }
      catch (...) {
         it->taken.clear(std::memory_order_release);
         throw;
      }
      return ret;
   }
   template <typename T>
   void Deallocate(T * p) // could be static
   {
      static_assert(alignof(T) <= alignof(std::max_align_t));
      static_assert(sizeof(T) <= BLOCK_SIZE);

      p->~T();
      Block * b = internal::get_block<BLOCK_SIZE>(p);
      assert((void *)p == internal::get_buffer(*b));
      b->taken.clear(std::memory_order_release);
   }

private:
   std::list<Block> m_blocks;
};

template <>
class Pool<>
{
public:
   static constexpr size_t BLOCK_SIZE = std::numeric_limits<size_t>::max();

protected:
   explicit Pool(size_t)
   {}

   template <typename T, typename... TArgs>
   T * Allocate(TArgs &&...)
   {
      static_assert(sizeof(T) == 0, "Type is too big, no suitable pool found");
      std::abort();
   }

   template <typename T>
   void Deallocate(T *)
   {
      static_assert(sizeof(T) == 0, "Type is too big, no suitable pool found");
      std::abort();
   }

   void ShrinkToFit() {}

   void Resize(size_t) {}

   size_t GetBlockCount() const noexcept { return 0U; }

   size_t GetSize() const noexcept { return 0U; }
};

} // mem

#endif // MEMPOOL_HPP
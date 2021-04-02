#ifndef ASYNC_ALWAYSCOPYABLE_HPP
#define ASYNC_ALWAYSCOPYABLE_HPP

#include <utility>

namespace async::internal {

template <typename F>
struct AlwaysCopyable : F
{
   AlwaysCopyable(const F & f)
      : F(f)
   {}
   AlwaysCopyable(F && f)
      : F(std::move(f))
   {}
   AlwaysCopyable(AlwaysCopyable && cc)
      : F(std::move(cc))
   {}
   AlwaysCopyable(const AlwaysCopyable & c)
      : AlwaysCopyable(std::move(const_cast<AlwaysCopyable &>(c)))
   {}
};

} // namespace async::internal

#endif // ASYNC_ALWAYSCOPYABLE_HPP

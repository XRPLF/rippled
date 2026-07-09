#pragma once

#include <type_traits>

namespace beast {

/** Makes T const or non const depending on a bool. */
template <bool IsConst, class T>
struct MaybeConst
{
    explicit MaybeConst() = default;
    using type = std::
        conditional_t<IsConst, typename std::remove_const<T>::type const, std::remove_const_t<T>>;
};

/** Alias for omitting `typename`. */
template <bool IsConst, class T>
using maybe_const_t = MaybeConst<IsConst, T>::type;

}  // namespace beast

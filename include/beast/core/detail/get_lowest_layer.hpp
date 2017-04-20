//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_GET_LOWEST_LAYER_HPP
#define BEAST_DETAIL_GET_LOWEST_LAYER_HPP

#include <type_traits>

namespace beast {
namespace detail {

template<class T>
class has_lowest_layer
{
    template<class U, class R =
        typename U::lowest_layer_type>
    static std::true_type check(int);
    template<class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));
public:
    static bool constexpr value = type::value;
};

template<class T, bool B>
struct maybe_get_lowest_layer
{
    using type = T;
};

template<class T>
struct maybe_get_lowest_layer<T, true>
{
    using type = typename T::lowest_layer_type;
};

// If T has a nested type lowest_layer_type,
// returns that, else returns T.
template<class T>
struct get_lowest_layer
{
    using type = typename maybe_get_lowest_layer<T,
        has_lowest_layer<T>::value>::type;
};

} // detail
} // beast

#endif

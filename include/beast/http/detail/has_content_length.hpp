//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_HAS_CONTENT_LENGTH_HPP
#define BEAST_HTTP_DETAIL_HAS_CONTENT_LENGTH_HPP

#include <cstdint>
#include <type_traits>

namespace beast {
namespace http {
namespace detail {

template<class T>
class has_content_length_value
{
    template<class U, class R = typename std::is_convertible<
        decltype(std::declval<U>().content_length()),
            std::uint64_t>>
    static R check(int);
    template <class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));
public:
    // `true` if `T` meets the requirements.
    static bool const value = type::value;
};

// Determines if the writer can provide the content length
template<class T>
using has_content_length =
    std::integral_constant<bool,
        has_content_length_value<T>::value>;

} // detail
} // http
} // beast

#endif

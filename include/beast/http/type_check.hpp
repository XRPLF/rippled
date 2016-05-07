//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_TYPE_CHECK_HPP
#define BEAST_HTTP_TYPE_CHECK_HPP

#include <beast/core/error.hpp>
#include <boost/asio/buffer.hpp>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

/// Determine if `T` meets the requirements of `Parser`.
template<class T>
class is_Parser
{
    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U>().complete()),
            bool>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U>().write(
                std::declval<boost::asio::const_buffer const&>(),
                std::declval<error_code&>())),
            std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U>().write_eof(
                std::declval<error_code&>())),
            std::size_t>>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value =
        type1::value && type2::value && type3::value;
};

} // http
} // beast

#endif

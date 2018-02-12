//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_COMMON_RFC7231_HPP
#define BEAST_EXAMPLE_COMMON_RFC7231_HPP

#include <beast/core/string.hpp>
#include <beast/http/message.hpp>

namespace rfc7231 {

// This aggregates a collection of algorithms
// corresponding to specifications in rfc7231:
//
// https://tools.ietf.org/html/rfc7231
//

/** Returns `true` if the message specifies Expect: 100-continue

    @param req The request to check

    @see https://tools.ietf.org/html/rfc7231#section-5.1.1
*/
template<class Body, class Allocator>
bool
is_expect_100_continue(beast::http::request<
    Body, beast::http::basic_fields<Allocator>> const& req)
{
    return beast::iequals(
        req[beast::http::field::expect], "100-continue");
}

} // rfc7231

#endif

//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_TYPE_TRAITS_HPP
#define BEAST_WEBSOCKET_DETAIL_TYPE_TRAITS_HPP

#include <beast/websocket/rfc6455.hpp>
#include <beast/core/detail/type_traits.hpp>

namespace beast {
namespace websocket {
namespace detail {

template<class F>
using is_RequestDecorator =
    typename beast::detail::is_invocable<F,
        void(request_type&)>::type;

template<class F>
using is_ResponseDecorator =
    typename beast::detail::is_invocable<F,
        void(response_type&)>::type;

} // detail
} // websocket
} // beast

#endif

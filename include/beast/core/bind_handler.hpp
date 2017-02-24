//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BIND_HANDLER_HPP
#define BEAST_BIND_HANDLER_HPP

#include <beast/core/handler_concepts.hpp>
#include <beast/core/detail/bind_handler.hpp>
#include <type_traits>
#include <utility>

namespace beast {

/** Bind parameters to a completion handler, creating a new handler.

    This function creates a new handler which, when invoked with no
    parameters, calls the original handler with the list of bound
    arguments. The passed handler and arguments are forwarded into
    the returned handler, which provides the same `io_service`
    execution guarantees as the original handler.

    Unlike `io_service::wrap`, the returned handler can be used in
    a subsequent call to `io_service::post` instead of
    `io_service::dispatch`, to ensure that the handler will not be
    invoked immediately by the calling function.

    Example:

    @code

    template<class AsyncReadStream, class ReadHandler>
    void
    do_cancel(AsyncReadStream& stream, ReadHandler&& handler)
    {
        stream.get_io_service().post(
            bind_handler(std::forward<ReadHandler>(handler),
                boost::asio::error::operation_aborted, 0));
    }

    @endcode

    @param handler The handler to wrap.

    @param args A list of arguments to bind to the handler. The
    arguments are forwarded into the returned object.
*/
template<class Handler, class... Args>
#if GENERATING_DOCS
implementation_defined
#else
detail::bound_handler<
    typename std::decay<Handler>::type, Args...>
#endif
bind_handler(Handler&& handler, Args&&... args)
{
    static_assert(is_CompletionHandler<
        Handler, void(Args...)>::value,
            "Handler requirements not met");
    return detail::bound_handler<typename std::decay<
        Handler>::type, Args...>(std::forward<
            Handler>(handler), std::forward<Args>(args)...);
}

} // beast

#endif

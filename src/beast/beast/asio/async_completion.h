//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_ASIO_ASYNC_COMPLETION_H_INLUDED
#define BEAST_ASIO_ASYNC_COMPLETION_H_INLUDED

#include <beast/asio/type_check.h>
#include <boost/asio/async_result.hpp>
#include <boost/asio/handler_type.hpp>
#include <type_traits>
#include <utility>

namespace beast {

/** Completion helper for implementing the extensible asynchronous model.

    This class template is used to transform caller-provided
    completion tokens in calls to asynchronous initiation
    functions. The transformation allows customization of
    the return type of the initiating function, and the type
    of the final handler.

    @tparam CompletionToken A CompletionHandler, or a user defined type
    with specializations for customizing the return type (for example,
    `boost::asio::use_future` or `boost::asio::yield_context`).

    @tparam Signature The callable signature of the final completion handler.

    Usage:
    @code
    template<class CompletionToken>
    auto
    async_initfn(..., CompletionToken&& token)
    {
        async_completion<CompletionToken,
            void(error_code, std::size_t)> completion(token);
        ...
        return completion.result.get();
    }
    @endcode

    See <a href="http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3896.pdf">
        Library Foundations For Asynchronous Operations</a>
*/

template <class CompletionToken, class Signature>
struct async_completion
{
    /** The type of the final handler.

        Objects of this type will be callable with the
        specified signature.
    */
    using handler_type =
        typename boost::asio::handler_type<
            CompletionToken, Signature>::type;

    /** Construct the completion helper.

        @param token The completion token. Copies will be made as
        required. If `CompletionToken` is movable, it may also be moved.
    */
    async_completion(std::remove_reference_t<CompletionToken>& token)
        : handler(std::forward<CompletionToken>(token))
        , result(handler)
    {
        static_assert(is_Handler<handler_type, Signature>::value,
            "Handler requirements not met");
    }

    /** The final completion handler, callable with the specified signature. */
    handler_type handler;

    /** The return value of the asynchronous initiation function. */
    boost::asio::async_result<handler_type> result;
};

} // beast

#endif

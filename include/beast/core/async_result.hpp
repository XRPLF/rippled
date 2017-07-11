//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_ASYNC_COMPLETION_HPP
#define BEAST_ASYNC_COMPLETION_HPP

#include <beast/config.hpp>
#include <beast/core/type_traits.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/handler_type.hpp>
#include <type_traits>
#include <utility>

namespace beast {

/** An interface for customising the behaviour of an asynchronous initiation function.

    This class is used for determining:
    
    @li The concrete completion handler type to be called at the end of the
    asynchronous operation;
    
    @li the initiating function return type; and
    
    @li how the return value of the initiating function is obtained.
    
    The trait allows the handler and return types to be determined at the point
    where the specific completion handler signature is known.

    This template takes advantage of specializations of both
    `boost::asio::async_result` and `boost::asio::handler_type` for user-defined
    completion token types. The primary template assumes that the
    @b CompletionToken is the completion handler.

    @par Example

    The example shows how to define an asynchronous initiation function
    whose completion handler receives an error code:

    @code
    template<
        class AsyncStream,          // A stream supporting asynchronous read and write
        class Handler               // The handler to call with signature void(error_code)
    >
    async_return_type<              // This provides the return type customization
        Handler, void(error_code)>
    do_async(
        AsyncStream& stream,        // The stream to work on
        Handler&& handler)          // Could be an rvalue or const reference
    {
        // Make sure we have an async stream
        static_assert(is_async_stream<AsyncWriteStream>::value,
            "AsyncStream requirements not met");

        // This helper converts the handler into the real handler type
        async_completion<WriteHandler, void(error_code)> init{handler};

        ... // Create and invoke the composed operation

        // This provides the return value and executor customization
        return init.result.get();
    }
    @endcode

    @see @ref async_completion, @ref async_return_type, @ref handler_type
*/
template<class CompletionToken, class Signature>
class async_result
{
    BOOST_STATIC_ASSERT(
        ! std::is_reference<CompletionToken>::value);

    boost::asio::async_result<typename
        boost::asio::handler_type<CompletionToken,
            Signature>::type> impl_;

    async_result(async_result const&) = delete;
    async_result& operator=(async_result const&) = delete;

public:
    /// The concrete completion handler type for the specific signature.
    using completion_handler_type =
        typename boost::asio::handler_type<
            CompletionToken, Signature>::type;

    /// The return type of the initiating function.
    using return_type =
        typename boost::asio::async_result<
            completion_handler_type>::type;

    /** Construct an async result from a given handler.
    
        When using a specalised async_result, the constructor has
        an opportunity to initialise some state associated with the
        completion handler, which is then returned from the initiating
        function.
    */
    explicit
    async_result(completion_handler_type& h)
        : impl_(h)
    {
    }

    /// Obtain the value to be returned from the initiating function.
    return_type
    get()
    {
        return impl_.get();
    }
};

/** Helper for customizing the return type of asynchronous initiation functions.

    This class template is used to transform caller-provided completion
    handlers in calls to asynchronous initiation functions. The transformation
    allows customization of the return type of the initiating function, and the
    function signature of the final handler.

    Example:
    @code
    ...
    template<class CompletionToken>
    typename async_completion<CompletionToken, void(error_code)>::result_type
    async_initfn(..., CompletionToken&& handler)
    {
        async_completion<CompletionToken, void(error_code)> completion{handler};
        ...
        return completion.result.get();
    }
    @endcode
    
    @tparam CompletionToken Specifies the model used to obtain the result of
    the asynchronous operation.

    @tparam Signature The call signature for the completion handler type invoked
    on completion of the asynchronous operation.

    @note See <a href="http://cplusplus.github.io/networking-ts/draft.pdf">
        Working Draft, C++ Extensions for Networking</a>

    @see @ref async_return_type, @ref handler_type
*/
template<class CompletionToken, class Signature>
struct async_completion
{
    /** The type of the final handler called by the asynchronous initiation function.

        Objects of this type will be callable with the specified signature.
    */
    using completion_handler_type = typename async_result<
        typename std::decay<CompletionToken>::type,
            Signature>::completion_handler_type;

    /** Constructor

        The constructor creates the concrete completion handler and
        makes the link between the handler and the asynchronous
        result.

        @param token The completion token. If this is a regular completion
        handler, copies may be made as needed. If the handler is movable,
        it may also be moved.
    */
    explicit
    async_completion(CompletionToken& token)
        : completion_handler(static_cast<typename std::conditional<
            std::is_same<CompletionToken, completion_handler_type>::value,
                completion_handler_type&, CompletionToken&&>::type>(token))
        , result(completion_handler)
    {
        // CompletionToken is not invokable with the given signature
        static_assert(is_completion_handler<
                completion_handler_type, Signature>::value,
            "CompletionToken requirements not met: signature mismatch");
    }

    /// The final completion handler, callable with the specified signature.
    typename std::conditional<std::is_same<
        CompletionToken, completion_handler_type>::value,
            completion_handler_type&,
            completion_handler_type
                >::type completion_handler;

    /// The return value of the asynchronous initiation function.
    async_result<typename std::decay<
        CompletionToken>::type, Signature> result;
};

template<class CompletionToken, typename Signature>
using handler_type = typename beast::async_result<
    typename std::decay<CompletionToken>::type,
        Signature>::completion_handler_type;

template<class CompletionToken, typename Signature>
using async_return_type = typename beast::async_result<
    typename std::decay<CompletionToken>::type,
        Signature>::return_type;

} // beast

#endif

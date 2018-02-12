//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_SERVICE_LIST_HPP
#define BEAST_EXAMPLE_SERVER_SERVICE_LIST_HPP

#include "framework.hpp"

#include <beast/http/message.hpp>
#include <boost/optional.hpp>
#include <utility>

namespace framework {

/** A list of HTTP services which may process requests.

    When a service is invoked, it is provided with the stream and
    endpoint metadata in addtion to an HTTP request. The service
    decides whether or not the process the request, returning
    `true` if the request is processed or `false` if it does not
    process the request.

    @see file_service, ws_upgrade_service
*/
template<class... Services>
class service_list
{
    // This helper is for tag-dispatching tuple index
    template<std::size_t I>
    using C = std::integral_constant<std::size_t, I>;

    // Each service is wrapped in a boost::optional so we
    // can construct them one by one later, instead of
    // having to construct them all at once.
    //
    std::tuple<boost::optional<Services>...> list_;

public:
    /// Constructor
    service_list() = default;

    /// Constructor
    service_list(service_list&&) = default;

    /// Constructor
    service_list(service_list const&) = default;

    /** Initialize a service.

        Every service in the list must be initialized exactly once
        before the service list is invoked.

        @param args Optional arguments forwarded to the service
        constructor.

        @tparam Index The 0-based index of the service to initialize.

        @return A reference to the service list. This permits
        calls to be chained in a single expression.
    */
    template<std::size_t Index, class... Args>
    void
    init(error_code& ec, Args&&... args)
    {
        // First, construct the service inside the optional
        std::get<Index>(list_).emplace(std::forward<Args>(args)...);

        // Now allow the service to finish the initialization
        std::get<Index>(list_)->init(ec);
    }

    /** Handle a request.

        This function attempts to process the given HTTP request by
        invoking each service one at a time starting with the first
        service in the list. When a service indicates that it handles
        the request, by returning `true`, the function stops and
        returns the value `true`. Otherwise, if no service handles
        the request then the function returns the value `false`.

        @param stream The stream belonging to the connection. A service
        which handles  the request may optionally take ownership of the
        stream.

        @param ep The remote endpoint of the connection corresponding
        to the stream.

        @param req The request message to attempt handling. A service
        which handles the request may optionally take ownership of the
        message.

        @param send The function to invoke with the response. The function 
        should have this equivalent signature:

        @code

        template<class Body>
        void
        send(response<Body>&&);

        @endcode

        In C++14 this can be expressed using a generic lambda. In
        C++11 it will require a template member function of an invocable
        object.

        @return `true` if the request was handled by a service.
    */
    template<
        class Stream,
        class Body,
        class Send>
    bool
    respond(
        Stream&& stream,
        endpoint_type const& ep,
        beast::http::request<Body>&& req,
        Send const& send) const
    {
        return try_respond(
            std::move(stream),
            ep,
            std::move(req),
            send, C<0>{});
    }

private:
    /*  The implementation of `try_respond` is implemented using
        tail recursion which can usually be optimized away to
        something resembling a switch statement.
    */
    template<
        class Stream,
        class Body,
        class Send>
    bool
    try_respond(
        Stream&&,
        endpoint_type const&,
        beast::http::request<Body>&&,
        Send const&,
        C<sizeof...(Services)> const&) const
    {
        // This function breaks the recursion for the case where
        // where the Index is one past the last type in the list.
        //
        return false;
    }

    // Invoke the I-th type in the type list
    //
    template<
        class Stream,
        class Body,
        class Send,
        std::size_t I>
    bool
    try_respond(
        Stream&& stream,
        endpoint_type const& ep,
        beast::http::request<Body>&& req,
        Send const& send,
        C<I> const&) const
    {
        // If the I-th service handles the request then return
        //
        if(std::get<I>(list_)->respond(
                std::move(stream),
                ep,
                std::move(req),
                send))
            return true;

        // Try the I+1th service. If I==sizeof...(Services)
        // then we call the other overload and return false.
        //
        return try_respond(
            std::move(stream),
            ep,
            std::move(req),
            send,
            C<I+1>{});
    }
};

} // framework

#endif

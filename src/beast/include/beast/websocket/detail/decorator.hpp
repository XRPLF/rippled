//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_DECORATOR_HPP
#define BEAST_WEBSOCKET_DETAIL_DECORATOR_HPP

#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/string_body.hpp>
#include <beast/version.hpp>
#include <utility>

namespace beast {
namespace websocket {
namespace detail {

using request_type = http::request<http::empty_body>;

using response_type = http::response<http::string_body>;

struct abstract_decorator
{
    virtual
    ~abstract_decorator() = default;

    virtual
    void
    operator()(request_type& req) = 0;

    virtual
    void
    operator()(response_type& resp) = 0;
};

template<class T>
class decorator : public abstract_decorator
{
    T t_;

    class call_req_possible
    {
        template<class U, class R = decltype(
            std::declval<U>().operator()(
                std::declval<request_type&>()),
                    std::true_type{})>
        static R check(int);
        template<class>
        static std::false_type check(...);
    public:
        using type = decltype(check<T>(0));
    };

    class call_res_possible
    {
        template<class U, class R = decltype(
            std::declval<U>().operator()(
                std::declval<response_type&>()),
                    std::true_type{})>
        static R check(int);
        template<class>
        static std::false_type check(...);
    public:
        using type = decltype(check<T>(0));
    };

public:
    decorator() = default;

    decorator(T&& t)
        : t_(std::move(t))
    {
    }

    decorator(T const& t)
        : t_(t)
    {
    }

    void
    operator()(request_type& req) override
    {
        (*this)(req, typename call_req_possible::type{});
    }

    void
    operator()(response_type& resp) override
    {
        (*this)(resp, typename call_res_possible::type{});
    }

private:
    void
    operator()(request_type& req, std::true_type)
    {
        t_(req);
    }

    void
    operator()(request_type& req, std::false_type)
    {
        req.fields.replace("User-Agent",
            std::string{"Beast/"} + BEAST_VERSION_STRING);
    }

    void
    operator()(response_type& res, std::true_type)
    {
        t_(res);
    }

    void
    operator()(response_type& res, std::false_type)
    {
        res.fields.replace("Server",
            std::string{"Beast/"} + BEAST_VERSION_STRING);
    }
};

struct default_decorator
{
};

using decorator_type =
    std::unique_ptr<abstract_decorator>;

} // detail
} // websocket
} // beast

#endif

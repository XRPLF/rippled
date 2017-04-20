//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
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
#include <type_traits>
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
    operator()(request_type& req) const = 0;

    virtual
    void
    operator()(response_type& res) const = 0;
};

template<class F>
class decorator : public abstract_decorator
{
    F f_;

    class call_req_possible
    {
        template<class U, class R = decltype(
            std::declval<U const>().operator()(
                std::declval<request_type&>()),
                    std::true_type{})>
        static R check(int);
        template<class>
        static std::false_type check(...);
    public:
        using type = decltype(check<F>(0));
    };

    class call_res_possible
    {
        template<class U, class R = decltype(
            std::declval<U const>().operator()(
                std::declval<response_type&>()),
                    std::true_type{})>
        static R check(int);
        template<class>
        static std::false_type check(...);
    public:
        using type = decltype(check<F>(0));
    };

public:
    decorator(F&& t)
        : f_(std::move(t))
    {
    }

    decorator(F const& t)
        : f_(t)
    {
    }

    void
    operator()(request_type& req) const override
    {
        (*this)(req, typename call_req_possible::type{});
    }

    void
    operator()(response_type& res) const override
    {
        (*this)(res, typename call_res_possible::type{});
    }

private:
    void
    operator()(request_type& req, std::true_type) const
    {
        f_(req);
    }

    void
    operator()(request_type& req, std::false_type) const
    {
        req.fields.replace("User-Agent",
            std::string{"Beast/"} + BEAST_VERSION_STRING);
    }

    void
    operator()(response_type& res, std::true_type) const
    {
        f_(res);
    }

    void
    operator()(response_type& res, std::false_type) const
    {
        res.fields.replace("Server",
            std::string{"Beast/"} + BEAST_VERSION_STRING);
    }
};

class decorator_type
{
    std::shared_ptr<abstract_decorator> p_;

public:
    decorator_type() = delete;
    decorator_type(decorator_type&&) = default;
    decorator_type(decorator_type const&) = default;
    decorator_type& operator=(decorator_type&&) = default;
    decorator_type& operator=(decorator_type const&) = default;

    template<class F, class =
        typename std::enable_if<! std::is_same<
            typename std::decay<F>::type,
                decorator_type>::value>>
    decorator_type(F&& f)
        : p_(std::make_shared<decorator<F>>(
            std::forward<F>(f)))
    {
        BOOST_ASSERT(p_);
    }

    void
    operator()(request_type& req)
    {
        (*p_)(req);
        BOOST_ASSERT(p_);
    }

    void
    operator()(response_type& res)
    {
        (*p_)(res);
        BOOST_ASSERT(p_);
    }
};

struct default_decorator
{
};

} // detail
} // websocket
} // beast

#endif

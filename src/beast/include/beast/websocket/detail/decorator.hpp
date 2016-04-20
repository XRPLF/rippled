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
        t_(req);
    }

    void
    operator()(response_type& resp) override
    {
        t_(resp);
    }
};

struct default_decorator
{
    static
    char const*
    version()
    {
        return "Beast.WSProto/1.0";
    }

    template<class Body, class Headers>
    void
    operator()(http::message<true, Body, Headers>& req)
    {
        req.headers.replace("User-Agent", version());
    }

    template<class Body, class Headers>
    void
    operator()(http::message<false, Body, Headers>& resp)
    {
        resp.headers.replace("Server", version());
    }
};

using decorator_type =
    std::unique_ptr<abstract_decorator>;

} // detail
} // websocket
} // beast

#endif

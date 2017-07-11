//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_HTTP_BASE_HPP
#define BEAST_EXAMPLE_SERVER_HTTP_BASE_HPP

#include <beast/core/string.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/string_body.hpp>
#include <memory>
#include <utility>
#include <ostream>

namespace framework {

/*  Base class for HTTP PortHandlers

    This holds the server name and has some shared
    routines for building typical HTTP responses.
*/
class http_base
{
    beast::string_view server_name_;

public:
    explicit
    http_base(beast::string_view server_name)
        : server_name_(server_name)
    {
    }

protected:
    // Returns a bad request result response
    //
    template<class Body, class Fields>
    beast::http::response<beast::http::string_body>
    bad_request(beast::http::request<Body, Fields> const& req) const
    {
        beast::http::response<beast::http::string_body> res;

        // Match the version to the request
        res.version = req.version;

        res.result(beast::http::status::bad_request);
        res.set(beast::http::field::server, server_name_);
        res.set(beast::http::field::content_type, "text/html");
        res.body = "Bad request";
        res.prepare_payload();
        return res;
    }

    // Returns a 100 Continue result response
    //
    template<class Body, class Fields>
    beast::http::response<beast::http::empty_body>
    continue_100(beast::http::request<Body, Fields> const& req) const
    {
        beast::http::response<beast::http::empty_body> res;

        // Match the version to the request
        res.version = req.version;

        res.result(beast::http::status::continue_);
        res.set(beast::http::field::server, server_name_);

        return res;
    }
};

} // framework

#endif

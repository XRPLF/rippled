//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_TEST_FAIL_PARSER_HPP
#define BEAST_HTTP_TEST_FAIL_PARSER_HPP

#include <beast/http/basic_parser_v1.hpp>
#include <beast/test/fail_counter.hpp>

namespace beast {
namespace http {

template<bool isRequest>
class fail_parser
    : public basic_parser_v1<isRequest, fail_parser<isRequest>>
{
    test::fail_counter& fc_;
    std::uint64_t content_length_ = no_content_length;
    body_what body_rv_ = body_what::normal;

public:
    std::string body;

    template<class... Args>
    explicit
    fail_parser(test::fail_counter& fc, Args&&... args)
        : fc_(fc)
    {
    }

    void
    on_body_rv(body_what rv)
    {
        body_rv_ = rv;
    }

    // valid on successful parse
    std::uint64_t
    content_length() const
    {
        return content_length_;
    }

    void on_start(error_code& ec)
    {
        fc_.fail(ec);
    }

    void on_method(boost::string_ref const&, error_code& ec)
    {
        fc_.fail(ec);
    }

    void on_uri(boost::string_ref const&, error_code& ec)
    {
        fc_.fail(ec);
    }

    void on_reason(boost::string_ref const&, error_code& ec)
    {
        fc_.fail(ec);
    }

    void on_request(error_code& ec)
    {
        fc_.fail(ec);
    }

    void on_response(error_code& ec)
    {
        fc_.fail(ec);
    }

    void on_field(boost::string_ref const&, error_code& ec)
    {
        fc_.fail(ec);
    }

    void on_value(boost::string_ref const&, error_code& ec)
    {
        fc_.fail(ec);
    }

    void
    on_header(std::uint64_t content_length, error_code& ec)
    {
        if(fc_.fail(ec))
            return;
    }

    body_what
    on_body_what(std::uint64_t content_length, error_code& ec)
    {
        if(fc_.fail(ec))
            return body_what::normal;
        content_length_ = content_length;
        return body_rv_;
    }

    void on_body(boost::string_ref const& s, error_code& ec)
    {
        if(fc_.fail(ec))
            return;
        body.append(s.data(), s.size());
    }

    void on_complete(error_code& ec)
    {
        fc_.fail(ec);
    }
};

} // http
} // beast

#endif

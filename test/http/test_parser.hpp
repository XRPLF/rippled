//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_TEST_PARSER_HPP
#define BEAST_HTTP_TEST_PARSER_HPP

#include <beast/http/basic_parser.hpp>
#include <beast/test/fail_counter.hpp>
#include <string>
#include <unordered_map>

namespace beast {
namespace http {

template<bool isRequest>
class test_parser
    : public basic_parser<isRequest, test_parser<isRequest>>
{
    test::fail_counter* fc_ = nullptr;

public:
    using mutable_buffers_type =
        boost::asio::mutable_buffers_1;

    int status = 0;
    int version = 0;
    std::string method;
    std::string path;
    std::string reason;
    std::string body;
    int got_on_begin       = 0;
    int got_on_field       = 0;
    int got_on_header      = 0;
    int got_on_body        = 0;
    int got_content_length = 0;
    int got_on_chunk       = 0;
    int got_on_complete    = 0;
    std::unordered_map<
        std::string, std::string> fields;

    test_parser() = default;

    explicit
    test_parser(test::fail_counter& fc)
        : fc_(&fc)
    {
    }

    void
    on_request(verb, string_view method_str_,
        string_view path_, int version_, error_code& ec)
    {
        method = std::string(
            method_str_.data(), method_str_.size());
        path = std::string(
            path_.data(), path_.size());
        version = version_;
        ++got_on_begin;
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
    }

    void
    on_response(int code,
        string_view reason_,
            int version_, error_code& ec)
    {
        status = code;
        reason = std::string(
            reason_.data(), reason_.size());
        version = version_;
        ++got_on_begin;
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
    }

    void
    on_field(field, string_view name,
        string_view value, error_code& ec)
    {
        ++got_on_field;
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
        fields[name.to_string()] = value.to_string();
    }

    void
    on_header(error_code& ec)
    {
        ++got_on_header;
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
    }

    void
    on_body(boost::optional<
        std::uint64_t> const& content_length_,
            error_code& ec)
    {
        ++got_on_body;
        got_content_length =
            static_cast<bool>(content_length_);
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
    }

    std::size_t
    on_data(string_view s,
        error_code& ec)
    {
        body.append(s.data(), s.size());
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
        return s.size();
    }

    void
    on_chunk(std::uint64_t,
        string_view, error_code& ec)
    {
        ++got_on_chunk;
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
    }

    void
    on_complete(error_code& ec)
    {
        ++got_on_complete;
        if(fc_)
            fc_->fail(ec);
        else
            ec.assign(0, ec.category());
    }
};

} // http
} // beast

#endif

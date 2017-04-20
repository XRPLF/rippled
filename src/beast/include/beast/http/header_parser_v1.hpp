//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_HEADERS_PARSER_V1_HPP
#define BEAST_HTTP_HEADERS_PARSER_V1_HPP

#include <beast/config.hpp>
#include <beast/http/basic_parser_v1.hpp>
#include <beast/http/concepts.hpp>
#include <beast/http/message.hpp>
#include <beast/core/error.hpp>
#include <boost/assert.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

namespace detail {

struct request_parser_base
{
    std::string method_;
    std::string uri_;
};

struct response_parser_base
{
    std::string reason_;
};

} // detail

/** A parser for a HTTP/1 request or response header.

    This class uses the HTTP/1 wire format parser to
    convert a series of octets into a request or
    response @ref header.

    @note A new instance of the parser is required for each message.
*/
template<bool isRequest, class Fields>
class header_parser_v1
    : public basic_parser_v1<isRequest,
        header_parser_v1<isRequest, Fields>>
    , private std::conditional<isRequest,
        detail::request_parser_base,
            detail::response_parser_base>::type
{
public:
    /// The type of the header this parser produces.
    using header_type = header<isRequest, Fields>;

private:
    // VFALCO Check Fields requirements?

    std::string field_;
    std::string value_;
    header_type h_;
    bool flush_ = false;

public:
    /// Default constructor
    header_parser_v1() = default;

    /// Move constructor
    header_parser_v1(header_parser_v1&&) = default;

    /// Copy constructor (disallowed)
    header_parser_v1(header_parser_v1 const&) = delete;

    /// Move assignment (disallowed)
    header_parser_v1& operator=(header_parser_v1&&) = delete;

    /// Copy assignment (disallowed)
    header_parser_v1& operator=(header_parser_v1 const&) = delete;

    /** Construct the parser.

        @param args Forwarded to the header constructor.
    */
#if GENERATING_DOCS
    template<class... Args>
    explicit
    header_parser_v1(Args&&... args);
#else
    template<class Arg1, class... ArgN,
        class = typename std::enable_if<! std::is_same<
            typename std::decay<Arg1>::type, header_parser_v1>::value>>
    explicit
    header_parser_v1(Arg1&& arg1, ArgN&&... argn)
        : h_(std::forward<Arg1>(arg1),
            std::forward<ArgN>(argn)...)
    {
    }
#endif

    /** Returns the parsed header

        Only valid if @ref complete would return `true`.
    */
    header_type const&
    get() const
    {
        return h_;
    }

    /** Returns the parsed header.

        Only valid if @ref complete would return `true`.
    */
    header_type&
    get()
    {
        return h_;
    }

    /** Returns ownership of the parsed header.

        Ownership is transferred to the caller. Only
        valid if @ref complete would return `true`.

        Requires:
            @ref header_type is @b MoveConstructible
    */
    header_type
    release()
    {
        static_assert(std::is_move_constructible<decltype(h_)>::value,
            "MoveConstructible requirements not met");
        return std::move(h_);
    }

private:
    friend class basic_parser_v1<isRequest, header_parser_v1>;

    void flush()
    {
        if(! flush_)
            return;
        flush_ = false;
        BOOST_ASSERT(! field_.empty());
        h_.fields.insert(field_, value_);
        field_.clear();
        value_.clear();
    }

    void on_start(error_code&)
    {
    }

    void on_method(boost::string_ref const& s, error_code&)
    {
        this->method_.append(s.data(), s.size());
    }

    void on_uri(boost::string_ref const& s, error_code&)
    {
        this->uri_.append(s.data(), s.size());
    }

    void on_reason(boost::string_ref const& s, error_code&)
    {
        this->reason_.append(s.data(), s.size());
    }

    void on_request_or_response(std::true_type)
    {
        h_.method = std::move(this->method_);
        h_.url = std::move(this->uri_);
    }

    void on_request_or_response(std::false_type)
    {
        h_.status = this->status_code();
        h_.reason = std::move(this->reason_);
    }

    void on_request(error_code& ec)
    {
        on_request_or_response(
            std::integral_constant<bool, isRequest>{});
    }

    void on_response(error_code& ec)
    {
        on_request_or_response(
            std::integral_constant<bool, isRequest>{});
    }

    void on_field(boost::string_ref const& s, error_code&)
    {
        flush();
        field_.append(s.data(), s.size());
    }

    void on_value(boost::string_ref const& s, error_code&)
    {
        value_.append(s.data(), s.size());
        flush_ = true;
    }

    void
    on_header(std::uint64_t, error_code&)
    {
        flush();
        h_.version = 10 * this->http_major() + this->http_minor();
    }

    body_what
    on_body_what(std::uint64_t, error_code&)
    {
        return body_what::pause;
    }

    void on_body(boost::string_ref const&, error_code&)
    {
    }

    void on_complete(error_code&)
    {
    }
};

} // http
} // beast

#endif

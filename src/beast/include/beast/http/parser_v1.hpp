//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_PARSER_V1_HPP
#define BEAST_HTTP_PARSER_V1_HPP

#include <beast/config.hpp>
#include <beast/http/concepts.hpp>
#include <beast/http/header_parser_v1.hpp>
#include <beast/http/message.hpp>
#include <beast/core/error.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/assert.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

/** Skip body option.

    The options controls whether or not the parser expects to see a
    HTTP body, regardless of the presence or absence of certain fields
    such as Content-Length.

    Depending on the request, some responses do not carry a body.
    For example, a 200 response to a CONNECT request from a tunneling
    proxy. In these cases, callers use the @ref skip_body option to
    inform the parser that no body is expected. The parser will consider
    the message complete after the header has been received.

    Example:
    @code
        parser_v1<true, empty_body, fields> p;
        p.set_option(skip_body{true});
    @endcode

    @note Objects of this type are passed to @ref parser_v1::set_option.
*/
struct skip_body
{
    bool value;

    explicit
    skip_body(bool v)
        : value(v)
    {
    }
};

/** A parser for producing HTTP/1 messages.

    This class uses the basic HTTP/1 wire format parser to convert
    a series of octets into a `message`.

    @note A new instance of the parser is required for each message.
*/
template<bool isRequest, class Body, class Fields>
class parser_v1
    : public basic_parser_v1<isRequest,
        parser_v1<isRequest, Body, Fields>>
    , private std::conditional<isRequest,
        detail::request_parser_base,
            detail::response_parser_base>::type
{
public:
    /// The type of message this parser produces.
    using message_type =
        message<isRequest, Body, Fields>;

private:
    using reader =
        typename message_type::body_type::reader;

    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_reader<Body>::value,
        "Body has no reader");
    static_assert(is_Reader<reader, message_type>::value,
        "Reader requirements not met");

    std::string field_;
    std::string value_;
    message_type m_;
    boost::optional<reader> r_;
    std::uint8_t skip_body_ = 0;
    bool flush_ = false;

public:
    /// Default constructor
    parser_v1() = default;

    /// Move constructor
    parser_v1(parser_v1&&) = default;

    /// Copy constructor (disallowed)
    parser_v1(parser_v1 const&) = delete;

    /// Move assignment (disallowed)
    parser_v1& operator=(parser_v1&&) = delete;

    /// Copy assignment (disallowed)
    parser_v1& operator=(parser_v1 const&) = delete;

    /** Construct the parser.

        @param args Forwarded to the message constructor.

        @note This function participates in overload resolution only
        if the first argument is not a parser or fields parser.
    */
#if GENERATING_DOCS
    template<class... Args>
    explicit
    parser_v1(Args&&... args);
#else
    template<class Arg1, class... ArgN,
        class = typename std::enable_if<
            ! std::is_same<typename
                std::decay<Arg1>::type,
                    header_parser_v1<isRequest, Fields>>::value &&
            ! std::is_same<typename
                std::decay<Arg1>::type, parser_v1>::value
                    >::type>
    explicit
    parser_v1(Arg1&& arg1, ArgN&&... argn)
        : m_(std::forward<Arg1>(arg1),
            std::forward<ArgN>(argn)...)
    {
    }
#endif

    /** Construct the parser from a fields parser.

        @param parser The fields parser to construct from.
        @param args Forwarded to the message body constructor.
    */
    template<class... Args>
    explicit
    parser_v1(header_parser_v1<isRequest, Fields>& parser,
            Args&&... args)
        : m_(parser.release(), std::forward<Args>(args)...)
    {
        static_cast<basic_parser_v1<
            isRequest, parser_v1<
                isRequest, Body, Fields>>&>(*this) = parser;
    }

    /// Set the skip body option.
    void
    set_option(skip_body const& o)
    {
        skip_body_ = o.value ? 1 : 0;
    }

    /** Returns the parsed message.

        Only valid if @ref complete would return `true`.
    */
    message_type const&
    get() const
    {
        return m_;
    }

    /** Returns the parsed message.

        Only valid if @ref complete would return `true`.
    */
    message_type&
    get()
    {
        return m_;
    }

    /** Returns ownership of the parsed message.

        Ownership is transferred to the caller. Only
        valid if @ref complete would return `true`.

        Requires:
            `message<isRequest, Body, Fields>` is @b MoveConstructible
    */
    message_type
    release()
    {
        static_assert(std::is_move_constructible<decltype(m_)>::value,
            "MoveConstructible requirements not met");
        return std::move(m_);
    }

private:
    friend class basic_parser_v1<isRequest, parser_v1>;

    void flush()
    {
        if(! flush_)
            return;
        flush_ = false;
        BOOST_ASSERT(! field_.empty());
        m_.fields.insert(field_, value_);
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
        m_.method = std::move(this->method_);
        m_.url = std::move(this->uri_);
    }

    void on_request_or_response(std::false_type)
    {
        m_.status = this->status_code();
        m_.reason = std::move(this->reason_);
    }

    void on_request(error_code&)
    {
        on_request_or_response(
            std::integral_constant<bool, isRequest>{});
    }

    void on_response(error_code&)
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
        m_.version = 10 * this->http_major() + this->http_minor();
    }

    body_what
    on_body_what(std::uint64_t, error_code& ec)
    {
        if(skip_body_)
            return body_what::skip;
        r_.emplace(m_);
        r_->init(ec);
        return body_what::normal;
    }

    void on_body(boost::string_ref const& s, error_code& ec)
    {
        r_->write(s.data(), s.size(), ec);
    }

    void on_complete(error_code&)
    {
    }
};

/** Create a new parser from a fields parser.

    Associates a Body type with a fields parser, and returns
    a new parser which parses a complete message object
    containing the original message fields and a new body
    of the specified body type.

    This function allows HTTP messages to be parsed in two stages.
    First, the fields are parsed and control is returned. Then,
    the caller can choose at run-time, the type of Body to
    associate with the message. And finally, complete the parse
    in a second call.

    @param parser The fields parser to construct from. Ownership
    of the message fields in the fields parser is transferred
    as if by call to @ref header_parser_v1::release.

    @param args Forwarded to the body constructor of the message
    in the new parser.

    @return A parser for a message with the specified @b Body type.

    @par Example
    @code
        headers_parser<true, fields> ph;
        ...
        auto p = with_body<string_body>(ph);
        ...
        message<true, string_body, fields> m = p.release();
    @endcode
*/
template<class Body,
    bool isRequest, class Fields, class... Args>
parser_v1<isRequest, Body, Fields>
with_body(header_parser_v1<isRequest, Fields>& parser,
    Args&&... args)
{
    return parser_v1<isRequest, Body, Fields>(
        parser, std::forward<Args>(args)...);
}

} // http
} // beast

#endif

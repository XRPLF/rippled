//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_BASIC_PARSER_v1_HPP
#define BEAST_HTTP_BASIC_PARSER_v1_HPP

#include <beast/http/message.hpp>
#include <beast/http/parse_error.hpp>
#include <beast/http/rfc7230.hpp>
#include <beast/http/detail/basic_parser_v1.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/assert.hpp>
#include <array>
#include <climits>
#include <cstdint>
#include <type_traits>

namespace beast {
namespace http {

/** Parse flags

    The set of parser bit flags are returned by @ref basic_parser_v1::flags.
*/
enum parse_flag
{
    chunked               =   1,
    connection_keep_alive =   2,
    connection_close      =   4,
    connection_upgrade    =   8,
    trailing              =  16,
    upgrade               =  32,
    skipbody              =  64,
    contentlength         = 128,
    paused                = 256
};

/** Body maximum size option.

    Sets the maximum number of cumulative bytes allowed including
    all body octets. Octets in chunk-encoded bodies are counted
    after decoding. A value of zero indicates no limit on
    the number of body octets.

    The default body maximum size for requests is 4MB (four
    megabytes or 4,194,304 bytes) and unlimited for responses.

    @note Objects of this type are used with @ref basic_parser_v1::set_option.
*/
struct body_max_size
{
    std::size_t value;

    explicit
    body_max_size(std::size_t v)
        : value(v)
    {
    }
};

/** Header maximum size option.

    Sets the maximum number of cumulative bytes allowed
    including all header octets. A value of zero indicates
    no limit on the number of header octets.

    The default header maximum size is 16KB (16,384 bytes).

    @note Objects of this type are used with @ref basic_parser_v1::set_option.
*/
struct header_max_size
{
    std::size_t value;

    explicit
    header_max_size(std::size_t v)
        : value(v)
    {
    }
};

/** A value indicating how the parser should treat the body.

    This value is returned from the `on_header` callback in
    the derived class. It controls what the parser does next
    in terms of the message body.
*/
enum class body_what
{
    /** The parser should expect a body, keep reading.
    */
    normal,

    /** Skip parsing of the body.

        When returned by `on_header` this causes parsing to
        complete and control to return to the caller. This
        could be used when sending a response to a HEAD
        request, for example.
    */
    skip,

    /** The message represents an UPGRADE request.

        When returned by `on_body_prepare` this causes parsing
        to complete and control to return to the caller.
    */
    upgrade,

    /** Suspend parsing before reading the body.

        When returned by `on_body_prepare` this causes parsing
        to pause. Control is returned to the caller, and the
        parser state is preserved such that a subsequent call
        to the parser will begin reading the message body.

        This could be used by callers to inspect the HTTP
        header before committing to read the body. For example,
        to choose the body type based on the fields. Or to
        respond to an Expect: 100-continue request.
    */
    pause
};

/// The value returned when no content length is known or applicable.
static std::uint64_t constexpr no_content_length =
    (std::numeric_limits<std::uint64_t>::max)();

/** A parser for decoding HTTP/1 wire format messages.

    This parser is designed to efficiently parse messages in the
    HTTP/1 wire format. It allocates no memory and uses minimal
    state. It will handle chunked encoding and it understands the
    semantics of the Connection and Content-Length header fields.

    The interface uses CRTP (Curiously Recurring Template Pattern).
    To use this class, derive from basic_parser. When bytes are
    presented, the implementation will make a series of zero or
    more calls to derived class members functions (referred to as
    "callbacks" from here on) matching a specific signature.

    Every callback must be provided by the derived class, or else
    a compilation error will be generated. This exemplar shows
    the signature and description of the callbacks required in
    the derived class.

    @code
    template<bool isRequest>
    struct exemplar : basic_parser_v1<isRequest, exemplar>
    {
        // Called when the first valid octet of a new message is received
        //
        void on_start(error_code&);

        // Called for each piece of the Request-Method
        //
        void on_method(boost::string_ref const&, error_code&);

        // Called for each piece of the Request-URI
        //
        void on_uri(boost::string_ref const&, error_code&);

        // Called for each piece of the reason-phrase
        //
        void on_reason(boost::string_ref const&, error_code&);

        // Called after the entire Request-Line has been parsed successfully.
        //
        void on_request(error_code&);

        // Called after the entire Response-Line has been parsed successfully.
        //
        void on_response(error_code&);

        // Called for each piece of the current header field.
        //
        void on_field(boost::string_ref const&, error_code&);

        // Called for each piece of the current header value.
        //
        void on_value(boost::string_ref const&, error_code&)

        // Called when the entire header has been parsed successfully.
        //
        void
        on_header(std::uint64_t content_length, error_code&);

        // Called after on_header, before the body is parsed
        //
        body_what
        on_body_what(std::uint64_t content_length, error_code&);

        // Called for each piece of the body.
        //
        // If the header indicates chunk encoding, the chunk
        // encoding is removed from the buffer before being
        // passed to the callback.
        //
        void on_body(boost::string_ref const&, error_code&);

        // Called when the entire message has been parsed successfully.
        // At this point, @ref complete returns `true`, and the parser
        // is ready to parse another message if `keep_alive` would
        // return `true`.
        //
        void on_complete(error_code&) {}
    };
    @endcode

    The return value of `on_body_what` is special, it controls
    whether or not the parser should expect a body. See @ref body_what
    for choices of the return value.

    If a callback sets an error, parsing stops at the current octet
    and the error is returned to the caller. Callbacks must not throw
    exceptions.

    @tparam isRequest A `bool` indicating whether the parser will be
    presented with request or response message.

    @tparam Derived The derived class type. This is part of the
    Curiously Recurring Template Pattern interface.
*/
template<bool isRequest, class Derived>
class basic_parser_v1 : public detail::parser_base
{
private:
    template<bool, class>
    friend class basic_parser_v1;

    using self = basic_parser_v1;
    typedef void(self::*pmf_t)(error_code&, boost::string_ref const&);

    enum field_state : std::uint8_t
    {
        h_general = 0,
        h_C,
        h_CO,
        h_CON,

        h_matching_connection,
        h_matching_proxy_connection,
        h_matching_content_length,
        h_matching_transfer_encoding,
        h_matching_upgrade,

        h_connection,
        h_content_length0,
        h_content_length,
        h_content_length_ows,
        h_transfer_encoding,
        h_upgrade,

        h_matching_transfer_encoding_chunked,
        h_matching_transfer_encoding_general,
        h_matching_connection_keep_alive,
        h_matching_connection_close,
        h_matching_connection_upgrade,

        h_transfer_encoding_chunked,
        h_transfer_encoding_chunked_ows,

        h_connection_keep_alive,
        h_connection_keep_alive_ows,
        h_connection_close,
        h_connection_close_ows,
        h_connection_upgrade,
        h_connection_upgrade_ows,
        h_connection_token,
        h_connection_token_ows
    };

    std::size_t h_max_;
    std::size_t h_left_;
    std::size_t b_max_;
    std::size_t b_left_;
    std::uint64_t content_length_;
    pmf_t cb_;
    state s_              : 8;
    unsigned fs_          : 8;
    unsigned pos_         : 8; // position in field state
    unsigned http_major_  : 16;
    unsigned http_minor_  : 16;
    unsigned status_code_ : 16;
    unsigned flags_       : 9;
    bool upgrade_         : 1; // true if parser exited for upgrade

public:
    /// Default constructor
    basic_parser_v1();

    /// Copy constructor.
    template<class OtherDerived>
    basic_parser_v1(basic_parser_v1<
        isRequest, OtherDerived> const& other);

    /// Copy assignment.
    template<class OtherDerived>
    basic_parser_v1& operator=(basic_parser_v1<
        isRequest, OtherDerived> const& other);

    /** Set options on the parser.

        @param args One or more parser options to set.
    */
#if GENERATING_DOCS
    template<class... Args>
    void
    set_option(Args&&... args)
#else
    template<class A1, class A2, class... An>
    void
    set_option(A1&& a1, A2&& a2, An&&... an)
#endif
    {
        set_option(std::forward<A1>(a1));
        set_option(std::forward<A2>(a2),
            std::forward<An>(an)...);
    }

    /// Set the header maximum size option
    void
    set_option(header_max_size const& o)
    {
        h_max_ = o.value;
        h_left_ = h_max_;
    }

    /// Set the body maximum size option
    void
    set_option(body_max_size const& o)
    {
        b_max_ = o.value;
        b_left_ = b_max_;
    }

    /// Returns internal flags associated with the parser.
    unsigned
    flags() const
    {
        return flags_;
    }

    /** Returns `true` if the message end is indicated by eof.

        This function returns true if the semantics of the message require
        that the end of the message is signaled by an end of file. For
        example, if the message is a HTTP/1.0 message and the Content-Length
        is unspecified, the end of the message is indicated by an end of file.

        @return `true` if write_eof must be used to indicate the message end.
    */
    bool
    needs_eof() const
    {
        return needs_eof(
            std::integral_constant<bool, isRequest>{});
    }

    /** Returns the major HTTP version number.

        Examples:
            * Returns 1 for HTTP/1.1
            * Returns 1 for HTTP/1.0

        @return The HTTP major version number.
    */
    unsigned
    http_major() const
    {
        return http_major_;
    }

    /** Returns the minor HTTP version number.

        Examples:
            * Returns 1 for HTTP/1.1
            * Returns 0 for HTTP/1.0

        @return The HTTP minor version number.
    */
    unsigned
    http_minor() const
    {
        return http_minor_;
    }

    /** Returns `true` if the message is an upgrade message.

        A value of `true` indicates that the parser has successfully
        completed parsing a HTTP upgrade message.

        @return `true` if the message is an upgrade message.
    */
    bool
    upgrade() const
    {
        return upgrade_;
    }

    /** Returns the numeric HTTP Status-Code of a response.

        @return The Status-Code.
    */
    unsigned
    status_code() const
    {
        return status_code_;
    }

    /** Returns `true` if the connection should be kept open.

        @note This function is only valid to call when the parser
        is complete.
    */
    bool
    keep_alive() const;

    /** Returns `true` if the parse has completed succesfully.

        When the parse has completed successfully, and the semantics
        of the parsed message indicate that the connection is still
        active, a subsequent call to `write` will begin parsing a
        new message.

        @return `true` If the parsing has completed successfully.
    */
    bool
    complete() const
    {
        return
            s_ == s_restart ||
            s_ == s_closed_complete ||
            (flags_ & parse_flag::paused);
    }

    /** Write a sequence of buffers to the parser.

        @param buffers An object meeting the requirements of
        ConstBufferSequence that represents the input sequence.

        @param ec Set to the error, if any error occurred.

        @return The number of bytes consumed in the input sequence.
    */
    template<class ConstBufferSequence>
#if GENERATING_DOCS
    std::size_t
#else
    typename std::enable_if<
        ! std::is_convertible<ConstBufferSequence,
            boost::asio::const_buffer>::value,
                std::size_t>::type
#endif
    write(ConstBufferSequence const& buffers, error_code& ec);

    /** Write a single buffer of data to the parser.

        @param buffer The buffer to write.
        @param ec Set to the error, if any error occurred.

        @return The number of bytes consumed in the buffer.
    */
    std::size_t
    write(boost::asio::const_buffer const& buffer, error_code& ec);

    /** Called to indicate the end of file.

        HTTP needs to know where the end of the stream is. For example,
        sometimes servers send responses without Content-Length and
        expect the client to consume input (for the body) until EOF.
        Callbacks and errors will still be processed as usual.

        @note This is typically called when a socket read returns eof.
    */
    void
    write_eof(error_code& ec);

protected:
    /** Reset the parsing state.

        The state of the parser is reset to expect the beginning of
        a new request or response. The old state is discarded.
    */
    void
    reset();

private:
    Derived&
    impl()
    {
        return *static_cast<Derived*>(this);
    }

    void
    reset(std::true_type)
    {
        s_ = s_req_start;
    }

    void
    reset(std::false_type)
    {
        s_ = s_res_start;
    }

    void
    init(std::true_type)
    {
        // Request: 16KB max header, 4MB max body
        h_max_ = 16 * 1024;
        b_max_ = 4 * 1024 * 1024;
    }

    void
    init(std::false_type)
    {
        // Response: 16KB max header, unlimited body
        h_max_ = 16 * 1024;
        b_max_ = 0;
    }

    void
    init()
    {
        init(std::integral_constant<bool, isRequest>{});
        reset();
    }

    bool
    needs_eof(std::true_type) const;

    bool
    needs_eof(std::false_type) const;

    template<class T, class = beast::detail::void_t<>>
    struct check_on_start : std::false_type {};

    template<class T>
    struct check_on_start<T, beast::detail::void_t<decltype(
        std::declval<T>().on_start(
            std::declval<error_code&>())
                )>> : std::true_type { };

    template<class T, class = beast::detail::void_t<>>
    struct check_on_method : std::false_type {};

    template<class T>
    struct check_on_method<T, beast::detail::void_t<decltype(
        std::declval<T>().on_method(
            std::declval<boost::string_ref>(),
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_uri : std::false_type {};

    template<class T>
    struct check_on_uri<T, beast::detail::void_t<decltype(
        std::declval<T>().on_uri(
            std::declval<boost::string_ref>(),
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_reason : std::false_type {};

    template<class T>
    struct check_on_reason<T, beast::detail::void_t<decltype(
        std::declval<T>().on_reason(
            std::declval<boost::string_ref>(),
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_request : std::false_type {};

    template<class T>
    struct check_on_request<T, beast::detail::void_t<decltype(
        std::declval<T>().on_request(
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_response : std::false_type {};

    template<class T>
    struct check_on_response<T, beast::detail::void_t<decltype(
        std::declval<T>().on_response(
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_field : std::false_type {};

    template<class T>
    struct check_on_field<T, beast::detail::void_t<decltype(
        std::declval<T>().on_field(
            std::declval<boost::string_ref>(),
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_value : std::false_type {};

    template<class T>
    struct check_on_value<T, beast::detail::void_t<decltype(
        std::declval<T>().on_value(
            std::declval<boost::string_ref>(),
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_headers : std::false_type {};

    template<class T>
    struct check_on_headers<T, beast::detail::void_t<decltype(
        std::declval<T>().on_header(
            std::declval<std::uint64_t>(),
            std::declval<error_code&>())
                )>> : std::true_type {};

    // VFALCO Can we use std::is_detected? Is C++11 capable?
    template<class C>
    class check_on_body_what_t
    {
        template<class T, class R = std::is_convertible<decltype(
            std::declval<T>().on_body_what(
                std::declval<std::uint64_t>(),
                std::declval<error_code&>())),
                    body_what>>
        static R check(int);
        template<class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using check_on_body_what =
        std::integral_constant<bool, check_on_body_what_t<C>::value>;

    template<class T, class = beast::detail::void_t<>>
    struct check_on_body : std::false_type {};

    template<class T>
    struct check_on_body<T, beast::detail::void_t<decltype(
        std::declval<T>().on_body(
            std::declval<boost::string_ref>(),
            std::declval<error_code&>())
                )>> : std::true_type {};

    template<class T, class = beast::detail::void_t<>>
    struct check_on_complete : std::false_type {};

    template<class T>
    struct check_on_complete<T, beast::detail::void_t<decltype(
        std::declval<T>().on_complete(
            std::declval<error_code&>())
                )>> : std::true_type {};

    void call_on_start(error_code& ec)
    {
        static_assert(check_on_start<Derived>::value,
            "on_start requirements not met");
        impl().on_start(ec);
    }

    void call_on_method(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        static_assert(check_on_method<Derived>::value,
            "on_method requirements not met");
        if(h_max_ && s.size() > h_left_)
        {
            ec = parse_error::header_too_big;
            return;
        }
        h_left_ -= s.size();
        impl().on_method(s, ec);
    }

    void call_on_method(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_method(error_code& ec,
        boost::string_ref const& s)
    {
        call_on_method(ec, s,
            std::integral_constant<bool, isRequest>{});
    }

    void call_on_uri(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        static_assert(check_on_uri<Derived>::value,
            "on_uri requirements not met");
        if(h_max_ && s.size() > h_left_)
        {
            ec = parse_error::header_too_big;
            return;
        }
        h_left_ -= s.size();
        impl().on_uri(s, ec);
    }

    void call_on_uri(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_uri(error_code& ec,
        boost::string_ref const& s)
    {
        call_on_uri(ec, s,
            std::integral_constant<bool, isRequest>{});
    }

    void call_on_reason(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        static_assert(check_on_reason<Derived>::value,
            "on_reason requirements not met");
        if(h_max_ && s.size() > h_left_)
        {
            ec = parse_error::header_too_big;
            return;
        }
        h_left_ -= s.size();
        impl().on_reason(s, ec);
    }

    void call_on_reason(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_reason(error_code& ec, boost::string_ref const& s)
    {
        call_on_reason(ec, s,
            std::integral_constant<bool, ! isRequest>{});
    }

    void call_on_request(error_code& ec, std::true_type)
    {
        static_assert(check_on_request<Derived>::value,
            "on_request requirements not met");
        impl().on_request(ec);
    }

    void call_on_request(error_code&, std::false_type)
    {
    }

    void call_on_request(error_code& ec)
    {
        call_on_request(ec,
            std::integral_constant<bool, isRequest>{});
    }

    void call_on_response(error_code& ec, std::true_type)
    {
        static_assert(check_on_response<Derived>::value,
            "on_response requirements not met");
        impl().on_response(ec);
    }

    void call_on_response(error_code&, std::false_type)
    {
    }

    void call_on_response(error_code& ec)
    {
        call_on_response(ec,
            std::integral_constant<bool, ! isRequest>{});
    }

    void call_on_field(error_code& ec,
        boost::string_ref const& s)
    {
        static_assert(check_on_field<Derived>::value,
            "on_field requirements not met");
        if(h_max_ && s.size() > h_left_)
        {
            ec = parse_error::header_too_big;
            return;
        }
        h_left_ -= s.size();
        impl().on_field(s, ec);
    }

    void call_on_value(error_code& ec,
        boost::string_ref const& s)
    {
        static_assert(check_on_value<Derived>::value,
            "on_value requirements not met");
        if(h_max_ && s.size() > h_left_)
        {
            ec = parse_error::header_too_big;
            return;
        }
        h_left_ -= s.size();
        impl().on_value(s, ec);
    }

    void
    call_on_headers(error_code& ec)
    {
        static_assert(check_on_headers<Derived>::value,
            "on_header requirements not met");
        impl().on_header(content_length_, ec);
    }

    body_what
    call_on_body_what(error_code& ec)
    {
        static_assert(check_on_body_what<Derived>::value,
            "on_body_what requirements not met");
        return impl().on_body_what(content_length_, ec);
    }

    void call_on_body(error_code& ec,
        boost::string_ref const& s)
    {
        static_assert(check_on_body<Derived>::value,
            "on_body requirements not met");
        if(b_max_ && s.size() > b_left_)
        {
            ec = parse_error::body_too_big;
            return;
        }
        b_left_ -= s.size();
        impl().on_body(s, ec);
    }

    void call_on_complete(error_code& ec)
    {
        static_assert(check_on_complete<Derived>::value,
            "on_complete requirements not met");
        impl().on_complete(ec);
    }
};

} // http
} // beast

#include <beast/http/impl/basic_parser_v1.ipp>

#endif

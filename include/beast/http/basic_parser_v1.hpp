//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
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
#include <beast/type_check.hpp>
#include <boost/asio/buffer.hpp>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <type_traits>

namespace beast {
namespace http {

namespace parse_flag {
enum values
{
    chunked               = 1 << 0,
    connection_keep_alive = 1 << 1,
    connection_close      = 1 << 2,
    connection_upgrade    = 1 << 3,
    trailing              = 1 << 4,
    upgrade               = 1 << 5,
    skipbody              = 1 << 6,
    contentlength         = 1 << 7
};
} // parse_flag

/** Base class for parsing HTTP/1 requests and responses.

    During parsing, callbacks will be made to the derived class
    if those members are present (detected through SFINAE). The
    signatures which can be present in the derived class are:<br>

    @li `void on_method(boost::string_ref const&, error_code& ec)`

        Called for each piece of the Request-Method

    @li `void on_uri(boost::string_ref const&, error_code& ec)`

        Called for each piece of the Request-URI

    @li `void on_reason(boost::string_ref const&, error_code& ec)`

        Called for each piece of the reason-phrase

    @li `void on_request(error_code& ec)`

        Called after the entire Request-Line has been parsed successfully.

    @li `void on_response(error_code& ec)`

        Called after the entire Response-Line has been parsed successfully.

    @li `void on_field(boost::string_ref const&, error_code& ec)`

        Called for each piece of the current header field.

    @li `void on_value(boost::string_ref const&, error_code& ec)`

        Called for each piece of the current header value.

    @li `int on_headers(error_code& ec)`

        Called when all the headers have been parsed successfully.

    @li `void on_body(boost::string_ref const&, error_code& ec)`

        Called for each piece of the body. If the headers indicated
        chunked encoding, the chunk encoding is removed from the
        buffer before being passed to the callback.

    @li `void on_complete(error_code& ec)`

        Called when the entire message has been parsed successfully.
        At this point, basic_parser_v1::complete() returns `true`, and
        the parser is ready to parse another message if keep_alive()
        would return `true`.

    The return value of `on_headers` is special, it controls whether
    or not the parser should expect a body. These are the return values:

    @li *0* The parser should expect a body

    @li *1* The parser should skip the body. For example, this is
        used when sending a response to a HEAD request.

    @li *2* The parser should skip ths body, this is an
        upgrade to a different protocol.

    The parser uses traits to determine if the callback is possible.
    If the Derived type omits one or more callbacks, they are simply
    skipped with no compilation error. The default behavior of on_body
    when the derived class does not provide the member, is to specify that
    the body should not be skipped.

    If a callback sets an error, parsing stops at the current octet
    and the error is returned to the caller.
*/
template<bool isRequest, class Derived>
class basic_parser_v1
{
private:
    using self = basic_parser_v1;
    typedef void(self::*pmf_t)(error_code&, boost::string_ref const&);

    static std::uint64_t constexpr no_content_length =
        std::numeric_limits<std::uint64_t>::max();

    enum state : std::uint8_t
    {
        s_closed = 1,

        s_req_start,
        s_req_method_start,
        s_req_method,
        s_req_space_before_url,
        s_req_url_start,
        s_req_url,
        s_req_http_start,
        s_req_http_H,
        s_req_http_HT,
        s_req_http_HTT,
        s_req_http_HTTP,
        s_req_major_start,
        s_req_major,
        s_req_minor_start,
        s_req_minor,
        s_req_line_end,

        s_res_start,
        s_res_H,
        s_res_HT,
        s_res_HTT,
        s_res_HTTP,
        s_res_major_start,
        s_res_major,
        s_res_minor_start,
        s_res_minor,
        s_res_status_code_start,
        s_res_status_code,
        s_res_status_start,
        s_res_status,
        s_res_line_almost_done,
        s_res_line_done,

        s_header_field_start,
        s_header_field,
        s_header_value_start,
        s_header_value_discard_lWs0,
        s_header_value_discard_ws0,
        s_header_value_almost_done0,
        s_header_value_text_start,
        s_header_value_discard_lWs,
        s_header_value_discard_ws,
        s_header_value_text,
        s_header_value_almost_done,

        s_headers_almost_done,
        s_headers_done,

        s_chunk_size_start,
        s_chunk_size,
        s_chunk_parameters,
        s_chunk_size_almost_done,

        // states below do not count towards
        // the limit on the size of the message

        s_body_identity0,
        s_body_identity,
        s_body_identity_eof0,
        s_body_identity_eof,

        s_chunk_data_start,
        s_chunk_data,
        s_chunk_data_almost_done,
        s_chunk_data_done,

        s_complete,
        s_restart
    };

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
        h_content_length,
        h_transfer_encoding,
        h_upgrade,

        h_matching_transfer_encoding_chunked,
        h_matching_connection_token_start,
        h_matching_connection_keep_alive,
        h_matching_connection_close,
        h_matching_connection_upgrade,
        h_matching_connection_token,

        h_transfer_encoding_chunked,
        h_connection_keep_alive,
        h_connection_close,
        h_connection_upgrade,
    };

    std::uint64_t content_length_;
    std::uint64_t nread_;
    pmf_t cb_;
    state s_              : 8;
    unsigned flags_       : 8;
    unsigned fs_          : 8;
    unsigned pos_         : 8; // position in field state
    unsigned http_major_  : 16;
    unsigned http_minor_  : 16;
    unsigned status_code_ : 16;
    bool upgrade_         : 1; // true if parser exited for upgrade

public:
    /// Copy constructor.
    basic_parser_v1(basic_parser_v1 const&) = default;

    /// Copy assignment.
    basic_parser_v1& operator=(basic_parser_v1 const&) = default;

    /// Constructor
    basic_parser_v1()
    {
        init(std::integral_constant<bool, isRequest>{});
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
        return s_ == s_restart;
    }

    /** Write a sequence of buffers to the parser.

        @param buffers An object meeting the requirements of
        ConstBufferSequence that represents the input sequence.

        @param ec Set to the error, if any error occurred.

        @return The number of bytes consumed in the input sequence.
    */
    template<class ConstBufferSequence,
        class = typename std::enable_if<
            ! std::is_convertible<ConstBufferSequence,
                boost::asio::const_buffer>::value>::type
    >
    std::size_t
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

        @throws boost::system::system_error Thrown on failure.
    */
    void
    write_eof(error_code& ec);

private:
    Derived&
    impl()
    {
        return *static_cast<Derived*>(this);
    }

    void
    init(std::true_type)
    {
        s_ = s_req_start;
    }

    void
    init(std::false_type)
    {
        s_ = s_res_start;
    }

    bool
    needs_eof(std::true_type) const;

    bool
    needs_eof(std::false_type) const;

    template<class C>
    class has_on_method_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_method(
                std::declval<boost::string_ref const&>(),
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_method =
        std::integral_constant<bool, has_on_method_t<C>::value>;

    template<class C>
    class has_on_uri_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_uri(
                std::declval<boost::string_ref const&>(),
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_uri =
        std::integral_constant<bool, has_on_uri_t<C>::value>;

    template<class C>
    class has_on_reason_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_reason(
                std::declval<boost::string_ref const&>(),
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_reason =
        std::integral_constant<bool, has_on_reason_t<C>::value>;

    template<class C>
    class has_on_request_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_request(
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_request =
        std::integral_constant<bool, has_on_request_t<C>::value>;

    template<class C>
    class has_on_response_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_response(
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_response =
        std::integral_constant<bool, has_on_response_t<C>::value>;

    template<class C>
    class has_on_field_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_uri(
                std::declval<boost::string_ref const&>(),
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_field =
        std::integral_constant<bool, has_on_field_t<C>::value>;

    template<class C>
    class has_on_value_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_uri(
                std::declval<boost::string_ref const&>(),
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_value =
        std::integral_constant<bool, has_on_value_t<C>::value>;

    template<class C>
    class has_on_headers_t
    {
        template<class T, class R = std::is_same<int,
            decltype(std::declval<T>().on_headers(
                std::declval<error_code&>()))>>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_headers =
        std::integral_constant<bool, has_on_headers_t<C>::value>;

    template<class C>
    class has_on_body_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_body(
                std::declval<boost::string_ref const&>(),
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_body =
        std::integral_constant<bool, has_on_body_t<C>::value>;

    template<class C>
    class has_on_complete_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_complete(
                std::declval<error_code&>()),
                    std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_complete =
        std::integral_constant<bool, has_on_complete_t<C>::value>;

    void call_on_method(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        impl().on_method(s, ec);
    }

    void call_on_method(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_method(error_code& ec,
        boost::string_ref const& s)
    {
        call_on_method(ec, s, std::integral_constant<bool,
            isRequest && has_on_method<Derived>::value>{});
    }

    void call_on_uri(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        impl().on_uri(s, ec);
    }

    void call_on_uri(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_uri(error_code& ec, boost::string_ref const& s)
    {
        call_on_uri(ec, s, std::integral_constant<bool,
            isRequest && has_on_uri<Derived>::value>{});
    }

    void call_on_reason(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        impl().on_reason(s, ec);
    }

    void call_on_reason(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_reason(error_code& ec, boost::string_ref const& s)
    {
        call_on_reason(ec, s, std::integral_constant<bool,
            ! isRequest && has_on_reason<Derived>::value>{});
    }

    void call_on_request(error_code& ec, std::true_type)
    {
        impl().on_request(ec);
    }

    void call_on_request(error_code&, std::false_type)
    {
    }

    void call_on_request(error_code& ec)
    {
        call_on_request(ec, std::integral_constant<bool,
            isRequest && has_on_request<Derived>::value>{});
    }

    void call_on_response(error_code& ec, std::true_type)
    {
        impl().on_response(ec);
    }

    void call_on_response(error_code&, std::false_type)
    {
    }

    void call_on_response(error_code& ec)
    {
        call_on_response(ec, std::integral_constant<bool,
            ! isRequest && has_on_response<Derived>::value>{});
    }

    void call_on_field(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        impl().on_field(s, ec);
    }

    void call_on_field(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_field(error_code& ec, boost::string_ref const& s)
    {
        call_on_field(ec, s, has_on_field<Derived>{});
    }

    void call_on_value(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        impl().on_value(s, ec);
    }

    void call_on_value(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_value(error_code& ec, boost::string_ref const& s)
    {
        call_on_value(ec, s, has_on_value<Derived>{});
    }

    int call_on_headers(error_code& ec, std::true_type)
    {
        return impl().on_headers(ec);
    }

    int call_on_headers(error_code& ec, std::false_type)
    {
        return 0;
    }

    int call_on_headers(error_code& ec)
    {
        return call_on_headers(ec, has_on_headers<Derived>{});
    }

    void call_on_body(error_code& ec,
        boost::string_ref const& s, std::true_type)
    {
        impl().on_body(s, ec);
    }

    void call_on_body(error_code&,
        boost::string_ref const&, std::false_type)
    {
    }

    void call_on_body(error_code& ec, boost::string_ref const& s)
    {
        call_on_body(ec, s, has_on_body<Derived>{});
    }

    void call_on_complete(error_code& ec, std::true_type)
    {
        impl().on_complete(ec);
    }

    void call_on_complete(error_code&, std::false_type)
    {
    }

    void call_on_complete(error_code& ec)
    {
        call_on_complete(ec, has_on_complete<Derived>{});
    }
};

} // http
} // beast

#include <beast/http/impl/basic_parser_v1.ipp>

#endif

//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_BASIC_PARSER_HPP
#define BEAST_HTTP_BASIC_PARSER_HPP

#include <beast/http/method.hpp>
#include <beast/http/impl/http_parser.h>
#include <beast/type_check.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <string>
#include <type_traits>

namespace beast {
namespace http {

/** Parser for producing HTTP requests and responses.

    Callbacks:

    If a is an object of type Derived, and the call expression is
    valid then the stated effects will take place:

    a.on_start()

        Called once when a new message begins.

    a.on_field(std::string field, std::string value)

        Called for each field

    a.on_headers_complete(error_code&)

        Called when all the header fields have been received, but
        before any part of the body if any is received.

    a.on_request(method_t method, std::string url,
        int major, int minor, bool keep_alive, bool upgrade)

        Called for requests when all the headers have been received.
        This will precede any content body.

        When keep_alive is false:
            * Server roles respond with a "Connection: close" header.
            * Client roles close the connection.

    a.on_response(int status, std::string text,
        int major, int minor, bool keep_alive,
            bool upgrade)

        Called for responses when all the headers have been received.
        This will precede any content body.

        When keep_alive is `false`:
            * Client roles close the connection.
            * Server roles respond with a "Connection: close" header.

        This function should return `true` if upgrade is false and
        a content body is expected. When upgrade is true, no
        content-body is expected, and the return value is ignored.

    a.on_body(void const* data, std::size_t bytes, error_code&)

        Called zero or more times for the content body. Any transfer
        encoding is already decoded in the memory pointed to by data.

    a.on_complete()

        Called when parsing completes successfully.

    The parser uses traits to determine if the callback is possible.
    If the Derived type omits the callbacks, they are simply skipped
    with no compilation error.
*/
/*
    VFALCO TODO is_call_possible, enable_if_t on Derived calls
                use boost::string_ref instead of std::string
*/
template<class Derived>
class basic_parser
{
    http_parser state_;
    boost::system::error_code* ec_;
    bool complete_ = false;
    std::string url_;
    std::string status_;
    std::string field_;
    std::string value_;

public:
    using error_code = boost::system::error_code;

    /** Move constructor.

        The state of the moved-from object is undefined,
        but safe to destroy.
    */
    basic_parser(basic_parser&& other);

    /** Move assignment.

        The state of the moved-from object is undefined,
        but safe to destroy.
    */
    basic_parser&
    operator=(basic_parser&& other);

    /** Copy constructor. */
    basic_parser(basic_parser const& other);

    /** Copy assignment. */
    basic_parser& operator=(basic_parser const& other);

    /** Construct the parser.

        @param request If `true`, the parser is setup for a request.
    */
    explicit
    basic_parser(bool request) noexcept;

    /** Returns `true` if parsing is complete.

        This is only defined when no errors have been returned.
    */
    bool
    complete() const noexcept
    {
        return complete_;
    }

    /** Write data to the parser.

        @param data A pointer to a buffer representing the input sequence.
        @param size The number of bytes in the buffer pointed to by data.

        @throws boost::system::system_error Thrown on failure.

        @return The number of bytes consumed in the input sequence.
    */
    std::size_t
    write(void const* data, std::size_t size)
    {
        error_code ec;
        auto const used = write(data, size, ec);
        if(ec)
            throw boost::system::system_error{ec};
        return used;
    }

    /** Write data to the parser.

        @param data A pointer to a buffer representing the input sequence.
        @param size The number of bytes in the buffer pointed to by data.
        @param ec Set to the error, if any error occurred.

        @return The number of bytes consumed in the input sequence.
    */
    std::size_t
    write(void const* data, std::size_t size,
        error_code& ec);

    /** Write data to the parser.

        @param buffers An object meeting the requirements of
        ConstBufferSequence that represents the input sequence.

        @throws boost::system::system_error Thrown on failure.

        @return The number of bytes consumed in the input sequence.
    */
    template<class ConstBufferSequence>
    std::size_t
    write(ConstBufferSequence const& buffers)
    {
        error_code ec;
        auto const used = write(buffers, ec);
        if(ec)
            throw boost::system::system_error{ec};
        return used;
    }

    /** Write data to the parser.

        @param buffers An object meeting the requirements of
        ConstBufferSequence that represents the input sequence.
        @param ec Set to the error, if any error occurred.

        @return The number of bytes consumed in the input sequence.
    */
    template<class ConstBufferSequence>
    std::size_t
    write(ConstBufferSequence const& buffers,
        error_code& ec);

    /** Called to indicate the end of file.

        HTTP needs to know where the end of the stream is. For example,
        sometimes servers send responses without Content-Length and
        expect the client to consume input (for the body) until EOF.
        Callbacks and errors will still be processed as usual.

        @note This is typically called when a socket read returns eof.

        @throws boost::system::system_error Thrown on failure.
    */
    void
    write_eof()
    {
        error_code ec;
        write_eof(ec);
        if(ec)
            throw boost::system::system_error{ec};
    }

    /** Called to indicate the end of file.

        HTTP needs to know where the end of the stream is. For example,
        sometimes servers send responses without Content-Length and
        expect the client to consume input (for the body) until EOF.
        Callbacks and errors will still be processed as usual.

        @note This is typically called when a socket read returns eof.

        @param ec Set to the error, if any error occurred.
    */
    void
    write_eof(error_code& ec);

private:
    Derived&
    impl()
    {
        return *static_cast<Derived*>(this);
    }

    template<class C>
    class has_on_start_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_start(), std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_start =
        std::integral_constant<bool, has_on_start_t<C>::value>;

    void
    call_on_start(std::true_type)
    {
        impl().on_start();
    }

    void
    call_on_start(std::false_type)
    {
    }

    template<class C>
    class has_on_field_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_field(
                std::declval<std::string const&>(),
                    std::declval<std::string const&>()),
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

    void
    call_on_field(std::string const& field,
        std::string const& value, std::true_type)
    {
        impl().on_field(field, value);
    }

    void
    call_on_field(std::string const&, std::string const&,
        std::false_type)
    {
    }

    template<class C>
    class has_on_headers_complete_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_headers_complete(
                std::declval<error_code&>()), std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_headers_complete =
        std::integral_constant<bool, has_on_headers_complete_t<C>::value>;

    void
    call_on_headers_complete(error_code& ec, std::true_type)
    {
        impl().on_headers_complete(ec);
    }

    void
    call_on_headers_complete(error_code&, std::false_type)
    {
    }

    template<class C>
    class has_on_request_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_request(
                std::declval<method_t>(), std::declval<std::string>(),
                    std::declval<int>(), std::declval<int>(),
                        std::declval<bool>(), std::declval<bool>()),
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

    void
    call_on_request(method_t method, std::string url,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        impl().on_request(
            method, url, major, minor, keep_alive, upgrade);
    }

    void
    call_on_request(method_t, std::string, int, int, bool, bool,
        std::false_type)
    {
    }

    template<class C>
    class has_on_response_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_response(
                std::declval<int>(), std::declval<std::string>,
                    std::declval<int>(), std::declval<int>(),
                        std::declval<bool>(), std::declval<bool>()),
                            std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
#if 0
        using type = decltype(check<C>(0));
#else
        // VFALCO Trait seems broken for http::parser
        using type = std::true_type;
#endif
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_response =
        std::integral_constant<bool, has_on_response_t<C>::value>;

    bool
    call_on_response(int status, std::string text,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        return impl().on_response(
            status, text, major, minor, keep_alive, upgrade);
    }

    bool
    call_on_response(int, std::string, int, int, bool, bool,
        std::false_type)
    {
        // VFALCO Certainly incorrect
        return true;
    }

    template<class C>
    class has_on_body_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_body(
                std::declval<void const*>(), std::declval<std::size_t>(),
                    std::declval<error_code&>()), std::true_type{})>
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

    void
    call_on_body(void const* data, std::size_t bytes,
        error_code& ec, std::true_type)
    {
        impl().on_body(data, bytes, ec);
    }

    void
    call_on_body(void const*, std::size_t,
        error_code&, std::false_type)
    {
    }

    template<class C>
    class has_on_complete_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_complete(), std::true_type{})>
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

    void
    call_on_complete(std::true_type)
    {
        impl().on_complete();
    }

    void
    call_on_complete(std::false_type)
    {
    }

    void
    check_header();

    static int cb_message_start(http_parser*);
    static int cb_url(http_parser*, char const*, std::size_t);
    static int cb_status(http_parser*, char const*, std::size_t);
    static int cb_header_field(http_parser*, char const*, std::size_t);
    static int cb_header_value(http_parser*, char const*, std::size_t);
    static int cb_headers_complete(http_parser*);
    static int cb_body(http_parser*, char const*, std::size_t);
    static int cb_message_complete(http_parser*);
    static int cb_chunk_header(http_parser*);
    static int cb_chunk_complete(http_parser*);

    struct hooks_t : http_parser_settings
    {
        hooks_t()
        {
            http_parser_settings_init(this);
            on_message_begin    = &basic_parser::cb_message_start;
            on_url              = &basic_parser::cb_url;
            on_status           = &basic_parser::cb_status;
            on_header_field     = &basic_parser::cb_header_field;
            on_header_value     = &basic_parser::cb_header_value;
            on_headers_complete = &basic_parser::cb_headers_complete;
            on_body             = &basic_parser::cb_body;
            on_message_complete = &basic_parser::cb_message_complete;
            on_chunk_header     = &basic_parser::cb_chunk_header;
            on_chunk_complete   = &basic_parser::cb_chunk_complete;
        }
    };

    static
    http_parser_settings const*
    hooks();
};

template<class Derived>
template<class ConstBufferSequence>
std::size_t
basic_parser<Derived>::write(
    ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    std::size_t bytes_used = 0;
    for (auto const& buffer : buffers)
    {
        auto const n = write(
            buffer_cast<void const*>(buffer),
                buffer_size(buffer), ec);
        if(ec)
            return 0;
        bytes_used += n;
        if(complete())
            break;
    }
    return bytes_used;
}

template<class Derived>
http_parser_settings const*
basic_parser<Derived>::hooks()
{
    static hooks_t const h;
    return &h;
}

} // http
} // beast

#include <beast/http/impl/basic_parser.ipp>

#endif

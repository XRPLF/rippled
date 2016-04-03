//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_HTTP_BASIC_PARSER_H_INCLUDED
#define BEAST_HTTP_BASIC_PARSER_H_INCLUDED

#include <beast/http/method.h>
#include <beast/http/impl/nodejs_parser.h>
#include <beast/asio/type_check.h>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <string>

namespace beast {
namespace http {

/** Parser for producing HTTP requests and responses.

    Derived requirements:

    If a is an object of type Derived, these expressions
    must be valid and have the stated effects:
 
    a.on_start()
    
        Called once when a new message begins.

    a.on_field(std::string field, std::string value)

        Called for each field
   
    a.on_request(method_t method, std::string url,
        int major, int minor, bool keep_alive, bool upgrade)

        Called for requests when all the headers have been received.
        This will precede any content body.

        When keep_alive is false:
            * Server roles respond with a "Connection: close" header.
            * Client roles close the connection.

        This function should return `true` if upgrade is false and
        a content body is expected. When upgrade is true, no
        content-body is expected, and the return value is ignored. 

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

    a.on_body(void const* data, std::size_t bytes)

        Called zero or more times for the content body. Any transfer
        encoding is already decoded in the memory pointed to by data.

    a.on_complete()

        Called when parsing completes successfully.
*/
template<class Derived>
class basic_parser
{
    nodejs::http_parser state_;
    bool complete_ = false;
    std::string url_;
    std::string status_;
    std::string field_;
    std::string value_;

public:
    using error_code = boost::system::error_code;

    /** Construct the parser.

        @param request If `true`, the parser is setup for a request.
    */
    explicit
    basic_parser(bool request) noexcept;

    /** Move construct a parser.

        The state of the moved-from object is undefined,
        but safe to destroy.
    */
    basic_parser&
    operator=(basic_parser&& other);

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

    void check_header();

    static int cb_message_start(nodejs::http_parser*);
    static int cb_url(nodejs::http_parser*, char const*, std::size_t);
    static int cb_status(nodejs::http_parser*, char const*, std::size_t);
    static int cb_header_field(nodejs::http_parser*, char const*, std::size_t);
    static int cb_header_value(nodejs::http_parser*, char const*, std::size_t);
    static int cb_headers_complete(nodejs::http_parser*);
    static int cb_body(nodejs::http_parser*, char const*, std::size_t);
    static int cb_message_complete(nodejs::http_parser*);
    static int cb_chunk_header(nodejs::http_parser*);
    static int cb_chunk_complete(nodejs::http_parser*);

    struct hooks_t : nodejs::http_parser_settings
    {
        hooks_t()
        {
            nodejs::http_parser_settings_init(this);
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
    nodejs::http_parser_settings const*
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
nodejs::http_parser_settings const*
basic_parser<Derived>::hooks()
{
    static hooks_t const h;
    return &h;
}

} // http
} // beast

#include <beast/http/impl/basic_parser.ipp>

#endif

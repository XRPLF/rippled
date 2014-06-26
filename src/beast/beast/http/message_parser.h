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

#ifndef BEAST_HTTP_MESSAGE_PARSER_H_INCLUDED
#define BEAST_HTTP_MESSAGE_PARSER_H_INCLUDED

#include <beast/http/method.h>
#include <boost/system/error_code.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace beast {

namespace joyent {
struct http_parser;
};

namespace http {

class message_parser
{
public:
    typedef boost::system::error_code error_code;

private:
    // These structures must exactly match the
    // declarations in joyent http_parser.h include
    //
    struct state_t
    {
      unsigned int type  : 2;
      unsigned int flags : 6;
      unsigned int state : 8;
      unsigned int header_state : 8;
      unsigned int index : 8;
      std::uint32_t nread;
      std::uint64_t content_length;
      unsigned short http_major;
      unsigned short http_minor;
      unsigned int status_code : 16;
      unsigned int method : 8;
      unsigned int http_errno : 7;
      unsigned int upgrade : 1;
      void *data;
    };

    typedef int (*data_cb_t) (
        state_t*, const char *at, size_t length);
    typedef int (*cb_t) (state_t*);

    struct hooks_t
    {
      cb_t      on_message_begin;
      data_cb_t on_url;
      data_cb_t on_status;
      data_cb_t on_header_field;
      data_cb_t on_header_value;
      cb_t      on_headers_complete;
      data_cb_t on_body;
      cb_t      on_message_complete;
    };

    error_code ec_;
    char state_ [sizeof(state_t)];
    char hooks_ [sizeof(hooks_t)];

    bool complete_;
    std::string url_;
    bool checked_url_;
    std::string field_;
    std::string value_;

protected:
    /** Construct the parser.
        If `request` is `true` this sets up the parser to
        process an HTTP request.
    */
    explicit
    message_parser (bool request);

public:
    /** Returns `true` if parsing is complete.
        This is only defined when no errors have been returned.
    */
    bool
    complete() const
    {
        return complete_;
    }

    /** Write data to the parser.
        The return value includes the error code if any,
        and the number of bytes consumed in the input sequence.
    */
    std::pair <error_code, std::size_t>
    write_one (void const* in, std::size_t bytes);

    template <class ConstBuffer>
    std::pair <error_code, std::size_t>
    write_one (ConstBuffer const& buffer)
    {
        return write_one (boost::asio::buffer_cast <void const*> (buffer),
            boost::asio::buffer_size (buffer));
    }

    template <class ConstBufferSequence>
    std::pair <error_code, std::size_t>
    write (ConstBufferSequence const& buffers)
    {
        std::pair <error_code, std::size_t> result (error_code(), 0);
        for (auto const& buffer : buffers)
        {
            std::size_t bytes_consumed;
            std::tie (result.first, bytes_consumed) = write_one (buffer);
            if (result.first)
                break;
            result.second += bytes_consumed;
        }
        return result;
    }

protected:
    virtual
    error_code
    on_request (method_t method, int http_major,
        int http_minor, std::string const& url) = 0;

    virtual
    error_code
    on_field (std::string const& field, std::string const& value) = 0;

private:
    int check_url();

    int do_message_start ();
    int do_url (char const* in, std::size_t bytes);
    int do_status (char const* in, std::size_t bytes);
    int do_header_field (char const* in, std::size_t bytes);
    int do_header_value (char const* in, std::size_t bytes);
    int do_headers_done ();
    int do_body (char const* in, std::size_t bytes);
    int do_message_complete ();

    static int cb_message_start (joyent::http_parser*);
    static int cb_url (joyent::http_parser*, char const*, std::size_t);
    static int cb_status (joyent::http_parser*, char const*, std::size_t);
    static int cb_header_field (joyent::http_parser*, char const*, std::size_t);
    static int cb_header_value (joyent::http_parser*, char const*, std::size_t);
    static int cb_headers_done (joyent::http_parser*);
    static int cb_body (joyent::http_parser*, char const*, std::size_t);
    static int cb_message_complete (joyent::http_parser*);
};

} // http
} // beast

#endif

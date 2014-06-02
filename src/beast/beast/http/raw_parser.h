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

#ifndef BEAST_HTTP_RAW_PARSER_H_INCLUDED
#define BEAST_HTTP_RAW_PARSER_H_INCLUDED

#include <beast/utility/empty_base_optimization.h>

#include <boost/system/error_code.hpp> // change to <system_error> soon

#include <array>
#include <cstdint>
#include <memory>

namespace beast {

namespace joyent {
struct http_parser;
};

namespace http {

/** Raw HTTP message parser.
    This is implemented using a zero-allocation state machine. The caller
    is responsible for all buffer management.
*/
class raw_parser
{
private:
    typedef boost::system::error_code error_code;

public:
    enum message_type
    {
        request,
        response
    };

    struct callback
    {
        /** Called when the first byte of an HTTP request is received. */
        virtual
        error_code
        on_request ();

        /** Called when the first byte of an HTTP response is received. */
        virtual
        error_code
        on_response ();

        /** Called repeatedly to provide parts of the URL.
            This is only for requests.
        */
        virtual
        error_code
        on_url (
            void const* in, std::size_t bytes);

        /** Called when the status is received.
            This is only for responses.
        */
        virtual
        error_code
        on_status (int status_code,
            void const* in, std::size_t bytes);

        /** Called repeatedly to provide parts of a field. */
        virtual
        error_code
        on_header_field (
            void const* in, std::size_t bytes);

        /** Called repeatedly to provide parts of a value. */
        virtual
        error_code
        on_header_value (
            void const* in, std::size_t bytes);

        /** Called when there are no more bytes of headers remaining. */
        virtual
        error_code
        on_headers_done (
            bool keep_alive);

        /** Called repeatedly to provide parts of the body. */
        virtual
        error_code
        on_body (bool is_final,
            void const* in, std::size_t bytes);

        /** Called when there are no more bytes of body remaining. */
        virtual
        error_code on_message_complete (
            bool keep_alive);
    };

    explicit raw_parser (callback& cb);

    ~raw_parser();

    /** Prepare to parse a new message.
        The previous state information, if any, is discarded.
    */
    void
    reset (message_type type);

    /** Processs message data.
        The return value includes the error code if any,
        and the number of bytes consumed in the input sequence.
    */
    std::pair <error_code, std::size_t>
    process_data (void const* in, std::size_t bytes);

    /** Notify the parser the end of the data is reached.
        Normally this will be called in response to the remote
        end closing down its half of the connection.
    */
    error_code
    process_eof ();

private:
    int do_message_start ();
    int do_url (char const* in, std::size_t bytes);
    int do_status (char const* in, std::size_t bytes);
    int do_header_field (char const* in, std::size_t bytes);
    int do_header_value (char const* in, std::size_t bytes);
    int do_headers_done ();
    int do_body (char const* in, std::size_t bytes);
    int do_message_complete ();

    static int on_message_start (joyent::http_parser*);
    static int on_url (joyent::http_parser*, char const*, std::size_t);
    static int on_status (joyent::http_parser*, char const*, std::size_t);
    static int on_header_field (joyent::http_parser*, char const*, std::size_t);
    static int on_header_value (joyent::http_parser*, char const*, std::size_t);
    static int on_headers_done (joyent::http_parser*);
    static int on_body (joyent::http_parser*, char const*, std::size_t);
    static int on_message_complete (joyent::http_parser*);

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

    std::reference_wrapper <callback> m_cb;
    error_code m_ec;
    char m_state [sizeof(state_t)];
    char m_hooks [sizeof(hooks_t)];
};

}
}

#endif

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

#ifndef BEAST_MODULE_ASIO_HTTPPARSERIMPL_H_INCLUDED
#define BEAST_MODULE_ASIO_HTTPPARSERIMPL_H_INCLUDED

#include <beast/http/impl/joyent_parser.h>
#include <boost/asio/buffer.hpp>

namespace beast {

class HTTPParserImpl
{
public:
    enum
    {
        stringReservation = 256
    };

    explicit HTTPParserImpl (enum joyent::http_parser_type type)
        : m_finished (false)
        , m_was_value (false)
        , m_headersComplete (false)
    {
        m_settings.on_message_begin     = &HTTPParserImpl::on_message_begin;
        m_settings.on_url               = &HTTPParserImpl::on_url;
        m_settings.on_status            = &HTTPParserImpl::on_status;
        m_settings.on_header_field      = &HTTPParserImpl::on_header_field;
        m_settings.on_header_value      = &HTTPParserImpl::on_header_value;
        m_settings.on_headers_complete  = &HTTPParserImpl::on_headers_complete;
        m_settings.on_body              = &HTTPParserImpl::on_body;
        m_settings.on_message_complete  = &HTTPParserImpl::on_message_complete;

        m_field.reserve (stringReservation);
        m_value.reserve (stringReservation);

        joyent::http_parser_init (&m_parser, type);
        m_parser.data = this;
    }

    ~HTTPParserImpl ()
    {
    }

    unsigned char error () const
    {
        return m_parser.http_errno;
    }

    String message () const
    {
        return String (joyent::http_errno_name (static_cast <
            enum joyent::http_errno> (m_parser.http_errno)));
    }

    std::size_t process (void const* buf, std::size_t bytes)
    {
        return joyent::http_parser_execute (&m_parser,
            &m_settings, static_cast <char const*> (buf), bytes);
    }

    void process_eof ()
    {
        joyent::http_parser_execute (&m_parser, &m_settings, nullptr, 0);
    }

    bool finished () const
    {
        return m_finished;
    }

    HTTPVersion version () const
    {
        return HTTPVersion (
            m_parser.http_major, m_parser.http_minor);
    }

    // Only for HTTPResponse!
    unsigned short status_code () const
    {
        return m_parser.status_code;
    }

    // Only for HTTPRequest!
    unsigned char method () const
    {
        return m_parser.method;
    }

    unsigned char http_errno () const
    {
        return m_parser.http_errno;
    }

    String http_errno_message () const
    {
        return String (joyent::http_errno_name (
            static_cast <enum joyent::http_errno> (
                m_parser.http_errno)));
    }

    bool upgrade () const
    {
        return m_parser.upgrade != 0;
    }

    StringPairArray& fields ()
    {
        return m_fields;
    }

    bool headers_complete () const
    {
        return m_headersComplete;
    }

    DynamicBuffer& body ()
    {
        return m_body;
    }

private:
    void addFieldValue ()
    {
        if (m_field.size () > 0 && m_value.size () > 0)
            m_fields.set (m_field, m_value);
        m_field.resize (0);
        m_value.resize (0);
    }

    int onMessageBegin ()
    {
        int ec (0);
        return ec;
    }

    int onUrl (char const*, std::size_t)
    {
        int ec (0);
        // This is for HTTP Request
        return ec;
    }

    int onStatus ()
    {
        int ec (0);
        return ec;
    }

    int onHeaderField (char const* at, std::size_t length)
    {
        int ec (0);
        if (m_was_value)
        {
            addFieldValue ();
            m_was_value = false;
        }
        m_field.append (at, length);
        return ec;
    }

    int onHeaderValue (char const* at, std::size_t length)
    {
        int ec (0);
        m_value.append (at, length);
        m_was_value = true;
        return ec;
    }

    int onHeadersComplete ()
    {
        m_headersComplete = true;
        int ec (0);
        addFieldValue ();
        return ec;
    }

    int onBody (char const* at, std::size_t length)
    {
        m_body.commit (boost::asio::buffer_copy (
            m_body.prepare <boost::asio::mutable_buffer> (length),
                boost::asio::buffer (at, length)));
        return 0;
    }

    int onMessageComplete ()
    {
        int ec (0);
        m_finished = true;
        return ec;
    }

private:
    static int on_message_begin (joyent::http_parser* parser)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onMessageBegin ();
    }

    static int on_url (joyent::http_parser* parser, const char *at, size_t length)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onUrl (at, length);
    }

    static int on_status (joyent::http_parser* parser,
        char const* /*at*/, size_t /*length*/)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onStatus ();
    }

    static int on_header_field (joyent::http_parser* parser,
        const char *at, size_t length)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onHeaderField (at, length);
    }

    static int on_header_value (joyent::http_parser* parser,
        const char *at, size_t length)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onHeaderValue (at, length);
    }

    static int on_headers_complete (joyent::http_parser* parser)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onHeadersComplete ();
    }

    static int on_body (joyent::http_parser* parser,
        const char *at, size_t length)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onBody (at, length);
    }

    static int on_message_complete (joyent::http_parser* parser)
    {
        return static_cast <HTTPParserImpl*> (parser->data)->
            onMessageComplete ();
    }

private:
    bool m_finished;
    joyent::http_parser_settings m_settings;
    joyent::http_parser m_parser;
    StringPairArray m_fields;
    bool m_was_value;
    std::string m_field;
    std::string m_value;
    bool m_headersComplete;
    DynamicBuffer m_body;
};

}

#endif

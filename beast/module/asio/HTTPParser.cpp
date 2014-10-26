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

#include <beast/module/asio/HTTPParser.h>
#include <beast/module/asio/HTTPParserImpl.h>

namespace beast {

HTTPParser::HTTPParser (Type type)
    : m_type (type)
    , m_impl (new HTTPParserImpl (
        (type == typeResponse) ? joyent::HTTP_RESPONSE : joyent::HTTP_REQUEST))
{
}

HTTPParser::~HTTPParser ()
{
}

unsigned char HTTPParser::error () const
{
    return m_impl->http_errno ();
}

String HTTPParser::message () const
{
    return m_impl->http_errno_message ();
}

std::size_t HTTPParser::process (void const* buf, std::size_t bytes)
{
    std::size_t const bytes_used (m_impl->process (buf, bytes));

    if (m_impl->finished ())
    {
        if (m_type == typeRequest)
        {
            m_request = new HTTPRequest (
                m_impl->version (),
                m_impl->fields (),
                m_impl->body (),
                m_impl->method ());
        }
        else if (m_type == typeResponse)
        {
            m_response = new HTTPResponse (
                m_impl->version (),
                m_impl->fields (),
                m_impl->body (),
                m_impl->status_code ());
        }
        else
        {
            bassertfalse;
        }
    }

    return bytes_used;
}

void HTTPParser::process_eof ()
{
    m_impl->process_eof ();
}

bool HTTPParser::finished () const
{
    return m_impl->finished();
}

StringPairArray const& HTTPParser::fields () const
{
    return m_impl->fields();
}

bool HTTPParser::headersComplete () const
{
    return m_impl->headers_complete();
}

SharedPtr <HTTPRequest> const& HTTPParser::request ()
{
    bassert (m_type == typeRequest);

    return m_request;
}

SharedPtr <HTTPResponse> const& HTTPParser::response ()
{
    bassert (m_type == typeResponse);

    return m_response;
}

}

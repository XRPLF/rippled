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

#ifndef BEAST_MODULE_ASIO_HTTPMESSAGE_H_INCLUDED
#define BEAST_MODULE_ASIO_HTTPMESSAGE_H_INCLUDED

#include <beast/module/asio/HTTPHeaders.h>
#include <beast/module/asio/HTTPVersion.h>

#include <beast/smart_ptr/SharedObject.h>
#include <beast/net/DynamicBuffer.h>
#include <beast/module/core/text/StringPairArray.h>

namespace beast {

/** A complete HTTP message.

    This provides the information common to all HTTP messages, including
    the version, content body, and headers.
    Derived classes provide the request or response specific data.

    Because a single HTTP message can be a fairly expensive object to
    make copies of, this is a SharedObject.

    @see HTTPRequest, HTTPResponse
*/
class HTTPMessage : public SharedObject
{
public:
    /** Construct the common HTTP message parts from values.
        Ownership of the fields and body parameters are
        transferred from the caller.
    */
    HTTPMessage (HTTPVersion const& version_,
                 StringPairArray& fields,
                 DynamicBuffer& body);

    /** Returns the HTTP version of this message. */
    HTTPVersion const& version () const;

    /** Returns the set of HTTP headers associated with this message. */
    HTTPHeaders const& headers () const;

    /** Returns the content-body. */
    DynamicBuffer const& body () const;

    /** Outputs all the HTTPMessage data excluding the body into a string. */
    String toString () const;
        
private:
    HTTPVersion m_version;
    HTTPHeaders m_headers;
    DynamicBuffer m_body;
};

}

#endif

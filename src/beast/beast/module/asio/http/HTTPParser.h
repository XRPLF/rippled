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

#ifndef BEAST_ASIO_HTTPPARSER_H_INCLUDED
#define BEAST_ASIO_HTTPPARSER_H_INCLUDED

#include <beast/module/asio/http/HTTPRequest.h>
#include <beast/module/asio/http/HTTPResponse.h>

namespace beast {

class HTTPParserImpl;

/** A parser for HTTPRequest and HTTPResponse objects. */
class HTTPParser
{
public:
    enum Type
    {
        typeRequest,
        typeResponse
    };

    /** Construct a new parser for the specified HTTPMessage type. */
    explicit HTTPParser (Type type);

    /** Destroy the parser. */
    ~HTTPParser ();

    /** Returns a non zero error code if parsing fails. */
    unsigned char error () const;

    /** Returns the error message text when error is non zero. */
    String message () const;

    /** Parse the buffer and return the amount used.
        Typically it is an error when this returns less than
        the amount passed in.
    */
    std::size_t process (void const* buf, std::size_t bytes);

    /** Notify the parser that eof was received.
    */
    void process_eof ();

    /** Returns `true` when parsing is successful and complete. */
    bool finished () const;

    /** Peek at the header fields as they are being built.
        Only complete pairs will show up, never partial strings.
    */
    StringPairArray const& fields () const;

    /** Returns `true` if all the HTTP headers have been received. */
    bool headersComplete () const;

    /** Return the HTTPRequest object produced from the parsiing.
        Only valid after finished returns `true`.
    */
    SharedPtr <HTTPRequest> const& request ();

    /** Return the HTTPResponse object produced from the parsing.
        Only valid after finished returns `true`.
    */
    SharedPtr <HTTPResponse> const& response ();

protected:
    Type m_type;
    std::unique_ptr <HTTPParserImpl> m_impl;
    SharedPtr <HTTPRequest> m_request;
    SharedPtr <HTTPResponse> m_response;
};

}

#endif

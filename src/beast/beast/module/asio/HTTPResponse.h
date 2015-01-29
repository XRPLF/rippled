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

#ifndef BEAST_MODULE_ASIO_HTTPRESPONSE_H_INCLUDED
#define BEAST_MODULE_ASIO_HTTPRESPONSE_H_INCLUDED

#include <beast/module/asio/HTTPMessage.h>

namespace beast {

class HTTPResponse : public HTTPMessage
{
public:
    /** Construct a complete response from values.
        Ownership of the fields and body parameters are
        transferred from the caller.
    */
    HTTPResponse (
        HTTPVersion const& version_,
        StringPairArray& fields,
        DynamicBuffer& body,
        unsigned short status_);

    unsigned short status () const;

    /** Convert the response into a string, excluding the body. */
    String toString () const;

private:
    unsigned short m_status;
};

}

#endif

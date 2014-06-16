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

#ifndef BEAST_ASIO_HTTPCLIENTTYPE_H_INCLUDED
#define BEAST_ASIO_HTTPCLIENTTYPE_H_INCLUDED

#include <beast/asio/shared_handler.h>

#include <utility>

namespace beast {
namespace asio {

class HTTPClientBase
{
public:
    typedef boost::system::error_code           error_type;
    typedef SharedPtr <HTTPResponse>            value_type;
    typedef std::pair <error_type, value_type>  result_type;

    static HTTPClientBase* New (
        Journal journal = Journal(),
        double timeoutSeconds = 30,
        std::size_t messageLimitBytes = 256 * 1024,
        std::size_t bufferSize = 16 * 1024);

    /** Destroy the client.
        This will cancel any pending i/o and block until all completion
        handlers have been called.
    */
    virtual ~HTTPClientBase () { }

    virtual result_type get (URL const& url) = 0;

    /** Perform an asynchronous get on the specified URL.
        Handler will be called with this signature:
            void (result_type)
    */
    virtual void async_get (boost::asio::io_service& io_service,
        URL const& url, asio::shared_handler <void (result_type)> handler) = 0;

    /** Cancel all pending asynchronous operations. */
    virtual void cancel() = 0;

    /** Block until all asynchronous i/o completes. */
    virtual void wait() = 0;
};

}
}

#endif

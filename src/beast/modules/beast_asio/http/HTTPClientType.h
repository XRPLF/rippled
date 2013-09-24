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

class HTTPClientBase
{
public:
    struct Result
    {
        boost::system::error_code error;
        SharedPtr <HTTPResponse> response;
    };

    class Listener
    {
    public:
        virtual void onHTTPRequestComplete (
            HTTPClientBase const& client,
                Result const& result) = 0;
    };

    static HTTPClientBase* New (
        double timeoutSeconds = 30,
        std::size_t messageLimitBytes = 256 * 1024,
        std::size_t bufferSize = 16 * 1024);

    virtual ~HTTPClientBase () { }

    virtual Result const& result () const = 0;

    virtual Result const& get (
        URL const& url) = 0;

    virtual void async_get (boost::asio::io_service& io_service,
                            Listener* listener,
                            URL const& url) = 0;

    /** Cancel any pending asynchronous operations.
        This must be called before destroying the container if there are
        any pending asynchronous operations. This routine does nothing if
        there are no pending operations. The call will block until all
        pending i/o is canceled.
    */
    virtual void cancel () = 0;
};

#endif

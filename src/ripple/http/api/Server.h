//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_HTTP_SERVER_H_INCLUDED
#define RIPPLE_HTTP_SERVER_H_INCLUDED

#include <memory>
#include <ostream>

namespace ripple {

namespace HTTP {

class ServerImpl;

/** Multi-threaded, asynchronous HTTP server. */
class Server
{
public:
    /** Create the server using the specified handler. */
    Server (Handler& handler, beast::Journal journal);

    /** Destroy the server.
        This blocks until the server stops.
    */
    virtual ~Server ();

    /** Returns the Journal associated with the server. */
    beast::Journal const& journal () const;

    /** Returns the listening ports settings.
        Thread safety:
            Safe to call from any thread.
            Cannot be called concurrently with setPorts.
    */
    Ports const& getPorts () const;

    /** Set the listening ports settings.
        These take effect immediately. Any current ports that are not in the
        new set will be closed. Established connections will not be disturbed.
        Thread safety:
            Cannot be called concurrently.
    */
    void setPorts (Ports const& ports);

    /** Notify the server to stop, without blocking.
        Thread safety:
            Safe to call concurrently from any thread.
    */
    void stopAsync ();

    /** Notify the server to stop, and block until the stop is complete.
        The handler's onStopped method will be called when the stop completes.
        Thread safety:
            Cannot be called concurrently.
            Cannot be called from the thread of execution of any Handler functions.
    */
    void stop ();

private:
    std::unique_ptr <ServerImpl> m_impl;
};

}  // namespace HTTP
}  // namespace ripple

#endif

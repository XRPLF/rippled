//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_SERVER_H_INCLUDED
#define RIPPLE_HTTP_SERVER_H_INCLUDED

#include <ostream>

namespace ripple {
namespace HTTP {

using namespace beast;

class ServerImpl;

/** Multi-threaded, asynchronous HTTP server. */
class Server
{
public:
    /** Create the server using the specified handler. */
    Server (Handler& handler, Journal journal);

    /** Destroy the server.
        This blocks until the server stops.
    */
    virtual ~Server ();

    /** Returns the Journal associated with the server. */
    Journal const& journal () const;

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
    ScopedPointer <ServerImpl> m_impl;
};

}
}

#endif

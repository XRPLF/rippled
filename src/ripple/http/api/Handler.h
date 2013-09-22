//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_HANDLER_H_INCLUDED
#define RIPPLE_HTTP_HANDLER_H_INCLUDED

namespace ripple {
namespace HTTP {

using namespace beast;

class Server;
class Session;

/** Processes all sessions.
    Thread safety:
        Must be safe to call concurrently from any number of foreign threads.
*/
struct Handler
{
    /** Called when the connection is accepted and we know remoteAddress. */
    virtual void onAccept (Session& session) = 0;

    /** Called repeatedly as new HTTP headers are received.
        Guaranteed to be called at least once.
    */
    virtual void onHeaders (Session& session) = 0;

    /** Called when we have the full Content-Body. */
    virtual void onRequest (Session& session) = 0;

    /** Called when the session ends.
        Guaranteed to be called once.
    */
    virtual void onClose (Session& session) = 0;

    /** Called when the server has finished its stop. */
    virtual void onStopped (Server& server) = 0;
};

}
}

#endif

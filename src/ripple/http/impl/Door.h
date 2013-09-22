//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_DOOR_H_INCLUDED
#define RIPPLE_HTTP_DOOR_H_INCLUDED

namespace ripple {
namespace HTTP {

/** A listening socket. */
class Door
    : public SharedObject
    , public AsyncObject <Door>
    , public List <Door>::Node
    , public LeakChecked <Door>
{
public:
    typedef SharedPtr <Door> Ptr;

    ServerImpl& m_impl;
    acceptor m_acceptor;
    Port m_port;

    Door (ServerImpl& impl, Port const& port);
    ~Door ();
    Port const& port () const;
    void cancel ();
    void failed (error_code ec);
    void asyncHandlersComplete ();
    void async_accept ();
    void handle_accept (error_code ec, Peer::Ptr peer, CompletionCounter);
};

}
}

#endif

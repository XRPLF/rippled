//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_SERVERIMPL_H_INCLUDED
#define RIPPLE_HTTP_SERVERIMPL_H_INCLUDED

namespace ripple {
namespace HTTP {

class Door;
class Peer;

class ServerImpl : public Thread
{
public:
    struct State
    {
        // Attributes for our listening ports
        Ports ports;

        // All allocated Peer objects
        List <Peer> peers;

        // All allocated Door objects
        List <Door> doors;
    };

    typedef SharedData <State> SharedState;
    typedef std::vector <SharedPtr <Door> > Doors;

    Server& m_server;
    Handler& m_handler;
    Journal m_journal;
    boost::asio::io_service m_io_service;
    boost::asio::io_service::strand m_strand;
    boost::optional <boost::asio::io_service::work> m_work;
    WaitableEvent m_stopped;
    SharedState m_state;
    Doors m_doors;

    ServerImpl (Server& server, Handler& handler, Journal journal);
    ~ServerImpl ();
    Journal const& journal() const;
    Ports const& getPorts () const;
    void setPorts (Ports const& ports);
    bool stopping () const;
    void stop (bool wait);

    Handler& handler();
    boost::asio::io_service& get_io_service();
    void add (Peer& peer);
    void add (Door& door);
    void remove (Peer& peer);
    void remove (Door& door);

    void handle_update ();
    void update ();
    void run ();
};

}
}

#endif

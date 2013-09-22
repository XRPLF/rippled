//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HTTP_SERVERIMPL_H_INCLUDED
#define RIPPLE_HTTP_SERVERIMPL_H_INCLUDED

namespace ripple {
namespace HTTP {

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
    typedef std::vector <Door::Ptr> Doors;

    Server& m_server;
    Handler& m_handler;
    Journal m_journal;
    boost::asio::io_service m_io_service;
    boost::asio::io_service::strand m_strand;
    boost::optional <boost::asio::io_service::work> m_work;
    WaitableEvent m_stopped;
    SharedState m_state;
    Doors m_doors;

    //--------------------------------------------------------------------------

    ServerImpl (Server& server, Handler& handler, Journal journal)
        : Thread ("HTTP::Server")
        , m_server (server)
        , m_handler (handler)
        , m_journal (journal)
        , m_strand (m_io_service)
        , m_work (boost::in_place (boost::ref (m_io_service)))
        , m_stopped (true)
    {
        startThread ();
    }

    ~ServerImpl ()
    {
        stopThread ();
    }

    Journal const& journal() const
    {
        return m_journal;
    }

    Ports const& getPorts () const
    {
        SharedState::UnlockedAccess state (m_state);
        return state->ports;
    }

    void setPorts (Ports const& ports)
    {
        SharedState::Access state (m_state);
        state->ports = ports;
        update();
    }

    bool stopping () const
    {
        return ! m_work;
    }

    void stop (bool wait)
    {
        if (! stopping())
        {
            m_work = boost::none;
            update();
        }
        
        if (wait)
            m_stopped.wait();
    }

    //--------------------------------------------------------------------------
    //
    // Server
    //

    Handler& handler()
    {
        return m_handler;
    }

    boost::asio::io_service& get_io_service()
    {
        return m_io_service;
    }

    // Inserts the peer into our list of peers. We only remove it
    // from the list inside the destructor of the Peer object. This
    // way, the Peer can never outlive the server.
    //
    void add (Peer& peer)
    {
        SharedState::Access state (m_state);
        state->peers.push_back (peer);
    }

    void add (Door& door)
    {
        SharedState::Access state (m_state);
        state->doors.push_back (door);
    }

    // Removes the peer from our list of peers. This is only called from
    // the destructor of Peer. Essentially, the item in the list functions
    // as a weak_ptr.
    //
    void remove (Peer& peer)
    {
        SharedState::Access state (m_state);
        state->peers.erase (state->peers.iterator_to (peer));
    }

    void remove (Door& door)
    {
        SharedState::Access state (m_state);
        state->doors.push_back (door);
    }

    //--------------------------------------------------------------------------
    //
    // Thread
    //

    // Updates our Door list based on settings.
    //
    void handle_update ()
    {
        if (! stopping())
        {
            // Make a local copy to shorten the lock
            //
            Ports ports;
            {
                SharedState::ConstAccess state (m_state);
                ports = state->ports;
            }

            std::sort (ports.begin(), ports.end());

            // Walk the Door list and the Port list simultaneously and
            // build a replacement Door vector which we will then swap in.
            //
            Doors doors;
            Doors::iterator door (m_doors.begin());
            for (Ports::const_iterator port (ports.begin());
                port != ports.end(); ++port)
            {
                int comp;

                while (door != m_doors.end() && 
                       ((comp = compare (*port, (*door)->port())) > 0))
                {
                    (*door)->cancel();
                    ++door;
                }

                if (door != m_doors.end())
                {
                    if (comp < 0)
                    {
                        doors.push_back (new Door (*this, *port));
                    }
                    else
                    {
                        // old Port and new Port are the same
                        doors.push_back (*door);
                        ++door;
                    }
                }
                else
                {
                    doors.push_back (new Door (*this, *port));
                }
            }

            // Any remaining Door objects are not in the new set, so cancel them.
            //
            for (;door != m_doors.end();)
                (*door)->cancel();

            m_doors.swap (doors);
        }
        else
        {
            // Cancel pending I/O on all doors.
            //
            for (Doors::iterator iter (m_doors.begin());
                iter != m_doors.end(); ++iter)
            {
                (*iter)->cancel();
            }

            // Remove our references to the old doors.
            //
            m_doors.resize (0);
        }
    }

    // Causes handle_update to run on the io_service
    //
    void update ()
    {
        m_io_service.post (m_strand.wrap (boost::bind (
            &ServerImpl::handle_update, this)));
    }

    // The main i/o processing loop.
    //
    void run ()
    {
        m_io_service.run ();

        m_stopped.signal();
        m_handler.onStopped (m_server);
    }
};

}
}

#endif

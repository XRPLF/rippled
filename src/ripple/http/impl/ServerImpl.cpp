//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

ServerImpl::ServerImpl (Server& server, Handler& handler, Journal journal)
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

ServerImpl::~ServerImpl ()
{
    stopThread ();
}

Journal const& ServerImpl::journal() const
{
    return m_journal;
}

Ports const& ServerImpl::getPorts () const
{
    SharedState::UnlockedAccess state (m_state);
    return state->ports;
}

void ServerImpl::setPorts (Ports const& ports)
{
    SharedState::Access state (m_state);
    state->ports = ports;
    update();
}

bool ServerImpl::stopping () const
{
    return ! m_work;
}

void ServerImpl::stop (bool wait)
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

Handler& ServerImpl::handler()
{
    return m_handler;
}

boost::asio::io_service& ServerImpl::get_io_service()
{
    return m_io_service;
}

// Inserts the peer into our list of peers. We only remove it
// from the list inside the destructor of the Peer object. This
// way, the Peer can never outlive the server.
//
void ServerImpl::add (Peer& peer)
{
    SharedState::Access state (m_state);
    state->peers.push_back (peer);
}

void ServerImpl::add (Door& door)
{
    SharedState::Access state (m_state);
    state->doors.push_back (door);
}

// Removes the peer from our list of peers. This is only called from
// the destructor of Peer. Essentially, the item in the list functions
// as a weak_ptr.
//
void ServerImpl::remove (Peer& peer)
{
    SharedState::Access state (m_state);
    state->peers.erase (state->peers.iterator_to (peer));
}

void ServerImpl::remove (Door& door)
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
void ServerImpl::handle_update ()
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
void ServerImpl::update ()
{
    m_io_service.post (m_strand.wrap (boost::bind (
        &ServerImpl::handle_update, this)));
}

// The main i/o processing loop.
//
void ServerImpl::run ()
{
    m_io_service.run ();

    m_stopped.signal();
    m_handler.onStopped (m_server);
}

}
}

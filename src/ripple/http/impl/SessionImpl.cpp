//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

SessionImpl::SessionImpl (Peer& peer)
    : m_peer (peer)
{
}

SessionImpl::~SessionImpl ()
{
}

void SessionImpl::write (void const* buffer, std::size_t bytes)
{
    m_peer.write (buffer, bytes);
}

// Called from an io_service thread
void SessionImpl::handle_close()
{
    m_peer_ref = nullptr;
}

void SessionImpl::close()
{
    m_peer.close();
}

void SessionImpl::detach()
{
    if (m_detached.compareAndSetBool (1, 0))
    {
        bassert (! m_work);
        bassert (m_peer_ref.empty());
        m_peer_ref = &m_peer;
        m_work = boost::in_place (boost::ref (
            m_peer.m_impl.get_io_service()));
    }
}

}
}

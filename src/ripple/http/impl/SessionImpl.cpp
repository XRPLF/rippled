//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

SessionImpl::SessionImpl (Peer& peer)
    : m_peer (peer)
    , m_closed (false)
{
}

SessionImpl::~SessionImpl ()
{
}

bool SessionImpl::closed() const
{
    return m_closed;
}

void SessionImpl::write (void const* buffer, std::size_t bytes)
{
    m_peer.write (buffer, bytes);
}

void SessionImpl::close()
{
    m_closed = true;
}

void SessionImpl::detach()
{
    if (! m_work)
        m_work = boost::in_place (boost::ref (
            m_peer.m_impl.get_io_service()));
}

}
}

//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

Server::Server (Handler& handler, Journal journal)
    : m_impl (new ServerImpl (*this, handler, journal))
{
}

Server::~Server ()
{
    stop();
}

Journal const& Server::journal () const
{
    return m_impl->journal();
}

Ports const& Server::getPorts () const
{
    return m_impl->getPorts();
}

void Server::setPorts (Ports const& ports)
{
    m_impl->setPorts (ports);
}

void Server::stopAsync ()
{
    m_impl->stop(false);
}

void Server::stop ()
{
    m_impl->stop(true);
}

}
}

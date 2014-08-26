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

#include <ripple/http/Server.h>
#include <ripple/http/impl/ServerImpl.h>
#include <beast/cxx14/memory.h> // <memory>

namespace ripple {
namespace HTTP {

Server::Server (Handler& handler, beast::Journal journal)
    : m_impl (std::make_unique <ServerImpl> (*this, handler, journal))
{
}

Server::~Server ()
{
    stop();
}

beast::Journal
Server::journal () const
{
    return m_impl->journal();
}

Ports const&
Server::getPorts () const
{
    return m_impl->getPorts();
}

void
Server::setPorts (Ports const& ports)
{
    m_impl->setPorts (ports);
}

void
Server::stopAsync ()
{
    m_impl->stop(false);
}

void
Server::stop ()
{
    m_impl->stop(true);
}

void
Server::onWrite (beast::PropertyStream::Map& map)
{
    m_impl->onWrite (map);
}

}
}

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

#include <beast/threads/Thread.h>

namespace ripple {
namespace Resource {

class ManagerImp
    : public Manager
    , public beast::Thread
{
public:
    beast::Journal m_journal;
    Logic m_logic;

    ManagerImp (beast::insight::Collector::ptr const& collector,
        beast::Journal journal)
        : Thread ("Resource::Manager")
        , m_journal (journal)
        , m_logic (collector, get_seconds_clock (), journal)
    {
        startThread ();
    }

    ~ManagerImp ()
    {
        stopThread ();
    }

    Consumer newInboundEndpoint (beast::IP::Endpoint const& address)
    {
        return m_logic.newInboundEndpoint (address);
    }

    Consumer newOutboundEndpoint (beast::IP::Endpoint const& address)
    {
        return m_logic.newOutboundEndpoint (address);
    }

    Consumer newAdminEndpoint (std::string const& name)
    {
        return m_logic.newAdminEndpoint (name);
    }

    Gossip exportConsumers ()
    {
        return m_logic.exportConsumers();
    }
    
    void importConsumers (std::string const& origin, Gossip const& gossip)
    {
        m_logic.importConsumers (origin, gossip);
    }

    //--------------------------------------------------------------------------

    Json::Value getJson ()
    {
        return m_logic.getJson ();
    }

    Json::Value getJson (int threshold)
    {
        return m_logic.getJson (threshold);
    }

    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map)
    {
        m_logic.onWrite (map);
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        do
        {
            m_logic.periodicActivity();
            wait (1000);
        }
        while (! threadShouldExit ());
    }
};

//------------------------------------------------------------------------------

Manager::Manager ()
    : beast::PropertyStream::Source ("resource")
{
}

Manager::~Manager ()
{
}

//------------------------------------------------------------------------------

std::unique_ptr <Manager> make_Manager (
    beast::insight::Collector::ptr const& collector,
        beast::Journal journal)
{
    return std::make_unique <ManagerImp> (collector, journal);
}

}
}

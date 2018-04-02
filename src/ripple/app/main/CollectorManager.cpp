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

#include <ripple/app/main/CollectorManager.h>
#include <memory>

namespace ripple {

class CollectorManagerImp
    : public CollectorManager
{
public:
    beast::Journal m_journal;
    beast::insight::Collector::ptr m_collector;
    std::unique_ptr <beast::insight::Groups> m_groups;

    CollectorManagerImp (Section const& params,
        beast::Journal journal)
        : m_journal (journal)
    {
        std::string const& server  = get<std::string> (params, "server");

        if (server == "statsd")
        {
            beast::IP::Endpoint const address (beast::IP::Endpoint::from_string (
                get<std::string> (params, "address")));
            std::string const& prefix (get<std::string> (params, "prefix"));

            m_collector = beast::insight::StatsDCollector::New (address, prefix, journal);
        }
        else
        {
            m_collector = beast::insight::NullCollector::New ();
        }

        m_groups = beast::insight::make_Groups (m_collector);
    }

    ~CollectorManagerImp ()
    {
    }

    beast::insight::Collector::ptr const& collector () override
    {
        return m_collector;
    }

    beast::insight::Group::ptr const& group (std::string const& name) override
    {
        return m_groups->get (name);
    }
};

//------------------------------------------------------------------------------

CollectorManager::~CollectorManager ()
{
}

std::unique_ptr<CollectorManager> CollectorManager::New(Section const& params,
    beast::Journal journal)
{
    return std::make_unique<CollectorManagerImp>(params, journal);
}

}

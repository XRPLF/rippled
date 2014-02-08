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

namespace ripple {

class CollectorManagerImp
    : public CollectorManager
{
public:
    Journal m_journal;
    insight::Collector::ptr m_collector;
    std::unique_ptr <insight::Groups> m_groups;

    CollectorManagerImp (StringPairArray const& params,
        Journal journal)
        : m_journal (journal)
    {
        std::string const& server (params ["server"].toStdString());

        if (server == "statsd")
        {
            IPAddress const address (IPAddress::from_string (
                params ["address"].toStdString ()));
            std::string const& prefix (params ["prefix"].toStdString ());

            m_collector = insight::StatsDCollector::New (address, prefix, journal);
        }
        else
        {
            m_collector = insight::NullCollector::New ();
        }

        m_groups = insight::make_Groups (m_collector);
    }

    ~CollectorManagerImp ()
    {
    }

    insight::Collector::ptr const& collector ()
    {
        return m_collector;
    }

    insight::Group::ptr const& group (std::string const& name)
    {
        return m_groups->get (name);
    }
};

//------------------------------------------------------------------------------

CollectorManager::~CollectorManager ()
{
}

CollectorManager* CollectorManager::New (StringPairArray const& params,
    Journal journal)
{
    return new CollectorManagerImp (params, journal);
}

}

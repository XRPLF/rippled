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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/ClusterNode.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/tokens.h>
#include <boost/regex.hpp>
#include <memory.h>

namespace ripple {

Cluster::Cluster (beast::Journal j)
    : j_ (j)
{
}

boost::optional<std::string>
Cluster::member (RippleAddress const& identity) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto iter = nodes_.find (identity);
    if (iter == nodes_.end ())
        return boost::none;
    return iter->name ();
}

std::size_t
Cluster::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

bool
Cluster::update (
    RippleAddress const& identity,
    std::string name,
    std::uint32_t loadFee,
    std::uint32_t reportTime)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // We can't use auto here yet due to the libstdc++ issue
    // described at https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68190
    std::set<ClusterNode, Comparator>::iterator iter =
        nodes_.find (identity);

    if (iter != nodes_.end ())
    {
        if (reportTime <= iter->getReportTime())
            return false;

        if (name.empty())
            name = iter->name();

        iter = nodes_.erase (iter);
    }

    nodes_.emplace_hint (iter, identity, name, loadFee, reportTime);
    return true;
}

void
Cluster::for_each (
    std::function<void(ClusterNode const&)> func) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto const& ni : nodes_)
        func (ni);
}

std::unique_ptr<Cluster>
make_Cluster (Config const& config, beast::Journal j)
{
    static boost::regex const re (
        "^"                         // start of line
        "(?:\\s*)"                  // whitespace (optional)
        "([a-zA-Z0-9]*)"            // Node identity
        "(?:\\s*)"                  // whitespace (optional)
        "(.*\\S*)"                  // <value>
        "(?:\\s*)"                  // whitespace (optional)
    );

    auto cluster = std::make_unique<Cluster> (j);

    for (auto const& n : config.CLUSTER_NODES)
    {
        boost::smatch match;

        if (!boost::regex_match (n, match, re))
        {
            JLOG (j.error) <<
                "Malformed entry: '" << n << "'";
            continue;
        }

        auto const nid = RippleAddress::createNodePublic (match[1]);

        if (!nid.isValid())
        {
            JLOG (j.error) <<
                "Invalid node identity: " << match[1];
            continue;
        }

        if (cluster->member (nid))
        {
            JLOG (j.warning) <<
                "Duplicate node identity: " << match[1];
            continue;
        }

        cluster->update(nid, match[2]);
    }

    return cluster;
}

} // ripple

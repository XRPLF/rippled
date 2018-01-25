//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

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
#include <test/csf/ledgers.h>

#include <sstream>

namespace ripple {
namespace test {
namespace csf {

Ledger::Instance const Ledger::genesis;

Json::Value
Ledger::getJson() const
{
    Json::Value res(Json::objectValue);
    res["id"] = static_cast<ID::value_type>(id());
    res["seq"] = static_cast<Seq::value_type>(seq());
    return res;
}

LedgerOracle::LedgerOracle()
{
    instances_.insert(InstanceEntry{Ledger::genesis, nextID()});
}

Ledger::ID
LedgerOracle::nextID() const
{
    return Ledger::ID{static_cast<Ledger::ID::value_type>(instances_.size())};
}

Ledger
LedgerOracle::accept(
    Ledger const& parent,
    TxSetType const& txs,
    NetClock::duration closeTimeResolution,
    NetClock::time_point const& consensusCloseTime)
{
    Ledger::Instance next(*parent.instance_);
    next.txs.insert(txs.begin(), txs.end());
    next.seq = parent.seq() + Ledger::Seq{1};
    next.closeTimeResolution = closeTimeResolution;
    next.closeTimeAgree = consensusCloseTime != NetClock::time_point{};
    if(next.closeTimeAgree)
        next.closeTime = effCloseTime(
            consensusCloseTime, closeTimeResolution, parent.closeTime());
    else
        next.closeTime = parent.closeTime() + 1s;

    next.parentCloseTime = parent.closeTime();
    next.parentID = parent.id();
    auto it = instances_.left.find(next);
    if (it == instances_.left.end())
    {
        using Entry = InstanceMap::left_value_type;
        it = instances_.left.insert(Entry{next, nextID()}).first;
    }
    return Ledger(it->second, &(it->first));
}

boost::optional<Ledger>
LedgerOracle::lookup(Ledger::ID const & id) const
{
    auto const it = instances_.right.find(id);
    if(it != instances_.right.end())
    {
        return Ledger(it->first, &(it->second));
    }
    return boost::none;
}


bool
LedgerOracle::isAncestor(Ledger const & ancestor, Ledger const& descendant) const
{
    // The ancestor must have an earlier sequence number than the descendent
    if(ancestor.seq() >= descendant.seq())
        return false;

    boost::optional<Ledger> current{descendant};
    while(current && current->seq() > ancestor.seq())
        current = lookup(current->parentID());
    return current && (current->id() == ancestor.id());
}

std::size_t
LedgerOracle::branches(std::set<Ledger> const & ledgers) const
{
    // Tips always maintains the Ledgers with largest sequence number
    // along all known chains.
    std::vector<Ledger> tips;
    tips.reserve(ledgers.size());

    for (Ledger const & ledger : ledgers)
    {
        // Three options,
        //  1. ledger is on a new branch
        //  2. ledger is on a branch that we have seen tip for
        //  3. ledger is the new tip for a branch
        bool found = false;
        for (auto idx = 0; idx < tips.size() && !found; ++idx)
        {
            bool const idxEarlier = tips[idx].seq() < ledger.seq();
            Ledger const & earlier = idxEarlier ? tips[idx] : ledger;
            Ledger const & later = idxEarlier ? ledger : tips[idx] ;
            if (isAncestor(earlier, later))
            {
                tips[idx] = later;
                found = true;
            }
        }

        if(!found)
            tips.push_back(ledger);

    }
    // The size of tips is the number of branches
    return tips.size();
}
}  // namespace csf
}  // namespace test
}  // namespace ripple

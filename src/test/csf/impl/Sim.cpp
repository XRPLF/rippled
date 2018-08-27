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
#include <test/csf/Sim.h>

namespace ripple {
namespace test {
namespace csf {

void
Sim::run(int ledgers)
{
    for (auto& p : peers)
    {
        p.targetLedgers = p.completedLedgers + ledgers;
        p.start();
    }
    scheduler.step();
}

void
Sim::run(SimDuration const & dur)
{
    for (auto& p : peers)
    {
        p.targetLedgers = std::numeric_limits<decltype(p.targetLedgers)>::max();
        p.start();
    }
    scheduler.step_for(dur);
}

bool
Sim::synchronized() const
{
    return synchronized(allPeers);
}

bool
Sim::synchronized(PeerGroup const & g) const
{
    if (g.size() < 1)
        return true;
    Peer const * ref = g[0];
    return std::all_of(g.begin(), g.end(), [&ref](Peer const* p) {
        return p->lastClosedLedger.id() ==
            ref->lastClosedLedger.id() &&
            p->fullyValidatedLedger.id() ==
            ref->fullyValidatedLedger.id();
    });
}

std::size_t
Sim::branches() const
{
    return branches(allPeers);
}
std::size_t
Sim::branches(PeerGroup const & g) const
{
    if(g.size() < 1)
        return 0;
    std::set<Ledger> ledgers;
    for(auto const & peer : g)
        ledgers.insert(peer->fullyValidatedLedger);

    return oracle.branches(ledgers);
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

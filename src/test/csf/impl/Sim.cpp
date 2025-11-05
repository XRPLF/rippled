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
Sim::run(SimDuration const& dur)
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
Sim::synchronized(PeerGroup const& g) const
{
    if (g.size() < 1)
        return true;
    Peer const* ref = g[0];
    return std::all_of(g.begin(), g.end(), [&ref](Peer const* p) {
        return p->lastClosedLedger.id() == ref->lastClosedLedger.id() &&
            p->fullyValidatedLedger.id() == ref->fullyValidatedLedger.id();
    });
}

std::size_t
Sim::branches() const
{
    return branches(allPeers);
}
std::size_t
Sim::branches(PeerGroup const& g) const
{
    if (g.size() < 1)
        return 0;
    std::set<Ledger> ledgers;
    for (auto const& peer : g)
        ledgers.insert(peer->fullyValidatedLedger);

    return oracle.branches(ledgers);
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

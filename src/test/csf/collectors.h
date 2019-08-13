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
#ifndef RIPPLE_TEST_CSF_COLLECTORS_H_INCLUDED
#define RIPPLE_TEST_CSF_COLLECTORS_H_INCLUDED

#include <ripple/basics/UnorderedContainers.h>
#include <boost/optional.hpp>
#include <chrono>
#include <ostream>
#include <test/csf/Histogram.h>
#include <test/csf/SimTime.h>
#include <test/csf/events.h>
#include <tuple>

namespace ripple {
namespace test {
namespace csf {

//  A collector is any class that implements
//
//      on(NodeID, SimTime, Event)
//
//  for all events emitted by a Peer.
//
//  This file contains helper functions for composing different collectors
//  and also defines several standard collectors available for simulations.


/** Group of collectors.

    Presents a group of collectors as a single collector which process an event
    by calling each collector sequentially. This is analagous to CollectorRefs
    in CollectorRef.h, but does *not* erase the type information of the combined
    collectors.
 */
template <class... Cs>
class Collectors
{
    std::tuple<Cs&...> cs;

    template <class C, class E>
    static void
    apply(C& c, PeerID who, SimTime when, E e)
    {
        c.on(who, when, e);
    }

    template <std::size_t... Is, class E>
    static void
    apply(
        std::tuple<Cs&...>& cs,
        PeerID who,
        SimTime when,
        E e,
        std::index_sequence<Is...>)
    {
        // Sean Parent for_each_argument trick (C++ fold expressions would be
        // nice here)
        (void)std::array<int, sizeof...(Cs)>{
            {((apply(std::get<Is>(cs), who, when, e)), 0)...}};
    }

public:
    /** Constructor

        @param cs References to the collectors to call together
    */
    Collectors(Cs&... cs_) : cs(std::tie(cs_...))
    {
    }

    template <class E>
    void
    on(PeerID who, SimTime when, E e)
    {
        apply(cs, who, when, e, std::index_sequence_for<Cs...>{});
    }
};

/** Create an instance of Collectors<Cs...> */
template <class... Cs>
Collectors<Cs...>
makeCollectors(Cs&... cs)
{
    return Collectors<Cs...>(cs...);
}

/** Maintain an instance of a Collector per peer

    For each peer that emits events, this class maintains a corresponding
    instance of CollectorType, only forwarding events emitted by the peer to
    the related instance.

    CollectorType should be default constructible.
*/
template <class CollectorType>
struct CollectByNode
{
    std::map<PeerID, CollectorType> byNode;

    CollectorType&
    operator[](PeerID who)
    {
        return byNode[who];
    }

    CollectorType const&
    operator[](PeerID who) const
    {
        return byNode[who];
    }
    template <class E>
    void
    on(PeerID who, SimTime when, E const& e)
    {
        byNode[who].on(who, when, e);
    }

};

/** Collector which ignores all events */
struct NullCollector
{
    template <class E>
    void
    on(PeerID, SimTime, E const& e)
    {
    }
};

/** Tracks the overall duration of a simulation */
struct SimDurationCollector
{
    bool init = false;
    SimTime start;
    SimTime stop;

    template <class E>
    void
    on(PeerID, SimTime when, E const& e)
    {
        if (!init)
        {
            start = when;
            init = true;
        }
        else
            stop = when;
    }
};

/** Tracks the submission -> accepted -> validated evolution of transactions.

    This collector tracks transactions through the network by monitoring the
    *first* time the transaction is seen by any node in the network, or
    seen by any node's accepted or fully validated ledger.

    If transactions submitted to the network do not have unique IDs, this
    collector will not track subsequent submissions.
*/
struct TxCollector
{
    // Counts
    std::size_t submitted{0};
    std::size_t accepted{0};
    std::size_t validated{0};

    struct Tracker
    {
        Tx tx;
        SimTime submitted;
        boost::optional<SimTime> accepted;
        boost::optional<SimTime> validated;

        Tracker(Tx tx_, SimTime submitted_) : tx{tx_}, submitted{submitted_}
        {
        }
    };

    hash_map<Tx::ID, Tracker> txs;

    using Hist = Histogram<SimTime::duration>;
    Hist submitToAccept;
    Hist submitToValidate;

    // Ignore most events by default
    template <class E>
    void
    on(PeerID, SimTime when, E const& e)
    {
    }

    void
    on(PeerID who, SimTime when, SubmitTx const& e)
    {

        // save first time it was seen
        if (txs.emplace(e.tx.id(), Tracker{e.tx, when}).second)
        {
            submitted++;
        }
    }

    void
    on(PeerID who, SimTime when, AcceptLedger const& e)
    {
        for (auto const& tx : e.ledger.txs())
        {
            auto it = txs.find(tx.id());
            if (it != txs.end() && !it->second.accepted)
            {
                Tracker& tracker = it->second;
                tracker.accepted = when;
                accepted++;

                submitToAccept.insert(*tracker.accepted - tracker.submitted);
            }
        }
    }

    void
    on(PeerID who, SimTime when, FullyValidateLedger const& e)
    {
        for (auto const& tx : e.ledger.txs())
        {
            auto it = txs.find(tx.id());
            if (it != txs.end() && !it->second.validated)
            {
                Tracker& tracker = it->second;
                // Should only validated a previously accepted Tx
                assert(tracker.accepted);

                tracker.validated = when;
                validated++;
                submitToValidate.insert(*tracker.validated - tracker.submitted);
            }
        }
    }

    // Returns the number of txs which were never accepted
    std::size_t
    orphaned() const
    {
        return std::count_if(txs.begin(), txs.end(), [](auto const& it) {
            return !it.second.accepted;
        });
    }

    // Returns the number of txs which were never validated
    std::size_t
    unvalidated() const
    {
        return std::count_if(txs.begin(), txs.end(), [](auto const& it) {
            return !it.second.validated;
        });
    }

    template <class T>
    void
    report(SimDuration simDuration, T& log, bool printBreakline = false)
    {
        using namespace std::chrono;
        auto perSec = [&simDuration](std::size_t count)
        {
            return double(count)/duration_cast<seconds>(simDuration).count();
        };

        auto fmtS = [](SimDuration dur)
        {
            return duration_cast<duration<float>>(dur).count();
        };

        if (printBreakline)
        {
            log << std::setw(11) << std::setfill('-') << "-" <<  "-"
                << std::setw(7) << std::setfill('-') << "-" <<  "-"
                << std::setw(7) << std::setfill('-') << "-" <<  "-"
                << std::setw(36) << std::setfill('-') << "-"
                << std::endl;
            log << std::setfill(' ');
        }

        log << std::left
            << std::setw(11) << "TxStats" <<  "|"
            << std::setw(7) << "Count" <<  "|"
            << std::setw(7) << "Per Sec" <<  "|"
            << std::setw(15) << "Latency (sec)"
            << std::right
            << std::setw(7) << "10-ile"
            << std::setw(7) << "50-ile"
            << std::setw(7) << "90-ile"
            << std::left
            << std::endl;

        log << std::setw(11) << std::setfill('-') << "-" <<  "|"
            << std::setw(7) << std::setfill('-') << "-" <<  "|"
            << std::setw(7) << std::setfill('-') << "-" <<  "|"
            << std::setw(36) << std::setfill('-') << "-"
            << std::endl;
        log << std::setfill(' ');

        log << std::left <<
            std::setw(11) << "Submit " << "|"
            << std::right
            << std::setw(7) << submitted << "|"
            << std::setw(7) << std::setprecision(2) << perSec(submitted) << "|"
            << std::setw(36) << "" << std::endl;

        log << std::left
            << std::setw(11) << "Accept " << "|"
            << std::right
            << std::setw(7) << accepted << "|"
            << std::setw(7) << std::setprecision(2) << perSec(accepted) << "|"
            << std::setw(15) << std::left << "From Submit" << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(submitToAccept.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(submitToAccept.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(submitToAccept.percentile(0.9f))
            << std::endl;

        log << std::left
            << std::setw(11) << "Validate " << "|"
            << std::right
            << std::setw(7) << validated << "|"
            << std::setw(7) << std::setprecision(2) << perSec(validated) << "|"
            << std::setw(15) << std::left << "From Submit" << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(submitToValidate.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(submitToValidate.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(submitToValidate.percentile(0.9f))
            << std::endl;

        log << std::left
            << std::setw(11) << "Orphan" << "|"
            << std::right
            << std::setw(7) << orphaned() << "|"
            << std::setw(7) << "" << "|"
            << std::setw(36) << std::endl;

        log << std::left
            << std::setw(11) << "Unvalidated" << "|"
            << std::right
            << std::setw(7) << unvalidated() << "|"
            << std::setw(7) << "" << "|"
            << std::setw(43) << std::endl;

        log << std::setw(11) << std::setfill('-') << "-" <<  "-"
            << std::setw(7) << std::setfill('-') << "-" <<  "-"
            << std::setw(7) << std::setfill('-') << "-" <<  "-"
            << std::setw(36) << std::setfill('-') << "-"
            << std::endl;
        log << std::setfill(' ');
    }

    template <class T, class Tag>
    void
    csv(SimDuration simDuration, T& log, Tag const& tag, bool printHeaders = false)
    {
        using namespace std::chrono;
        auto perSec = [&simDuration](std::size_t count)
        {
            return double(count)/duration_cast<seconds>(simDuration).count();
        };

        auto fmtS = [](SimDuration dur)
        {
            return duration_cast<duration<float>>(dur).count();
        };

        if(printHeaders)
        {
            log << "tag" << ","
                << "txNumSubmitted" << ","
                << "txNumAccepted" << ","
                << "txNumValidated" << ","
                << "txNumOrphaned" << ","
                << "txUnvalidated" << ","
                << "txRateSumbitted" << ","
                << "txRateAccepted" << ","
                << "txRateValidated" << ","
                << "txLatencySubmitToAccept10Pctl" << ","
                << "txLatencySubmitToAccept50Pctl" << ","
                << "txLatencySubmitToAccept90Pctl" << ","
                << "txLatencySubmitToValidatet10Pctl" << ","
                << "txLatencySubmitToValidatet50Pctl" << ","
                << "txLatencySubmitToValidatet90Pctl"
                << std::endl;
        }


        log << tag << ","
            // txNumSubmitted
            << submitted << ","
            // txNumAccepted
            << accepted << ","
            // txNumValidated
            << validated << ","
            // txNumOrphaned
            << orphaned() << ","
            // txNumUnvalidated
            << unvalidated() << ","
            // txRateSubmitted
            << std::setprecision(2) << perSec(submitted) << ","
            // txRateAccepted
            << std::setprecision(2) << perSec(accepted) << ","
            // txRateValidated
            << std::setprecision(2) << perSec(validated) << ","
            // txLatencySubmitToAccept10Pctl
            << std::setprecision(2) << fmtS(submitToAccept.percentile(0.1f)) << ","
            // txLatencySubmitToAccept50Pctl
            << std::setprecision(2) << fmtS(submitToAccept.percentile(0.5f)) << ","
            // txLatencySubmitToAccept90Pctl
            << std::setprecision(2) << fmtS(submitToAccept.percentile(0.9f)) << ","
            // txLatencySubmitToValidate10Pctl
            << std::setprecision(2) << fmtS(submitToValidate.percentile(0.1f)) << ","
            // txLatencySubmitToValidate50Pctl
            << std::setprecision(2) << fmtS(submitToValidate.percentile(0.5f)) << ","
            // txLatencySubmitToValidate90Pctl
            << std::setprecision(2) << fmtS(submitToValidate.percentile(0.9f)) << ","
            << std::endl;
    }
};

/** Tracks the accepted -> validated evolution of ledgers.

    This collector tracks ledgers through the network by monitoring the
    *first* time the ledger is accepted or fully validated by ANY node.

*/
struct LedgerCollector
{
    std::size_t accepted{0};
    std::size_t fullyValidated{0};

    struct Tracker
    {
        SimTime accepted;
        boost::optional<SimTime> fullyValidated;

        Tracker(SimTime accepted_) : accepted{accepted_}
        {
        }
    };

    hash_map<Ledger::ID, Tracker> ledgers_;

    using Hist = Histogram<SimTime::duration>;
    Hist acceptToFullyValid;
    Hist acceptToAccept;
    Hist fullyValidToFullyValid;

    // Ignore most events by default
    template <class E>
    void
    on(PeerID, SimTime, E const& e)
    {
    }

    void
    on(PeerID who, SimTime when, AcceptLedger const& e)
    {
        // First time this ledger accepted
        if (ledgers_.emplace(e.ledger.id(), Tracker{when}).second)
        {
            ++accepted;
            // ignore jumps?
            if (e.prior.id() == e.ledger.parentID())
            {
                auto const it = ledgers_.find(e.ledger.parentID());
                if (it != ledgers_.end())
                {
                    acceptToAccept.insert(when - it->second.accepted);
                }
            }
        }
    }

    void
    on(PeerID who, SimTime when, FullyValidateLedger const& e)
    {
        // ignore jumps
        if (e.prior.id() == e.ledger.parentID())
        {
            auto const it = ledgers_.find(e.ledger.id());
            assert(it != ledgers_.end());
            auto& tracker = it->second;
            // first time fully validated
            if (!tracker.fullyValidated)
            {
                ++fullyValidated;
                tracker.fullyValidated = when;
                acceptToFullyValid.insert(when - tracker.accepted);

                auto const parentIt = ledgers_.find(e.ledger.parentID());
                if (parentIt != ledgers_.end())
                {
                    auto& parentTracker = parentIt->second;
                    if (parentTracker.fullyValidated)
                    {
                        fullyValidToFullyValid.insert(
                            when - *parentTracker.fullyValidated);
                    }
                }
            }
        }
    }

    std::size_t
    unvalidated() const
    {
        return std::count_if(
            ledgers_.begin(), ledgers_.end(), [](auto const& it) {
                return !it.second.fullyValidated;
            });
    }

    template <class T>
    void
    report(SimDuration simDuration, T& log, bool printBreakline = false)
    {
        using namespace std::chrono;
        auto perSec = [&simDuration](std::size_t count)
        {
            return double(count)/duration_cast<seconds>(simDuration).count();
        };

        auto fmtS = [](SimDuration dur)
        {
            return duration_cast<duration<float>>(dur).count();
        };

        if (printBreakline)
        {
            log << std::setw(11) << std::setfill('-') << "-" <<  "-"
                << std::setw(7) << std::setfill('-') << "-" <<  "-"
                << std::setw(7) << std::setfill('-') << "-" <<  "-"
                << std::setw(36) << std::setfill('-') << "-"
                << std::endl;
            log << std::setfill(' ');
        }

        log << std::left
            << std::setw(11) << "LedgerStats" <<  "|"
            << std::setw(7)  << "Count" <<  "|"
            << std::setw(7)  << "Per Sec" <<  "|"
            << std::setw(15) << "Latency (sec)"
            << std::right
            << std::setw(7) << "10-ile"
            << std::setw(7) << "50-ile"
            << std::setw(7) << "90-ile"
            << std::left
            << std::endl;

        log << std::setw(11) << std::setfill('-') << "-" <<  "|"
            << std::setw(7) << std::setfill('-') << "-" <<  "|"
            << std::setw(7) << std::setfill('-') << "-" <<  "|"
            << std::setw(36) << std::setfill('-') << "-"
            << std::endl;
        log << std::setfill(' ');

         log << std::left
            << std::setw(11) << "Accept " << "|"
            << std::right
            << std::setw(7) << accepted << "|"
            << std::setw(7) << std::setprecision(2) << perSec(accepted) << "|"
            << std::setw(15) << std::left << "From Accept" << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(acceptToAccept.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(acceptToAccept.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(acceptToAccept.percentile(0.9f))
            << std::endl;

        log << std::left
            << std::setw(11) << "Validate " << "|"
            << std::right
            << std::setw(7) << fullyValidated << "|"
            << std::setw(7) << std::setprecision(2) << perSec(fullyValidated) << "|"
            << std::setw(15) << std::left << "From Validate " << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(fullyValidToFullyValid.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(fullyValidToFullyValid.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(fullyValidToFullyValid.percentile(0.9f))
            << std::endl;

        log << std::setw(11) << std::setfill('-') << "-" <<  "-"
            << std::setw(7) << std::setfill('-') << "-" <<  "-"
            << std::setw(7) << std::setfill('-') << "-" <<  "-"
            << std::setw(36) << std::setfill('-') << "-"
            << std::endl;
        log << std::setfill(' ');
    }

    template <class T, class Tag>
    void
    csv(SimDuration simDuration, T& log, Tag const& tag, bool printHeaders = false)
    {
        using namespace std::chrono;
        auto perSec = [&simDuration](std::size_t count)
        {
            return double(count)/duration_cast<seconds>(simDuration).count();
        };

        auto fmtS = [](SimDuration dur)
        {
            return duration_cast<duration<float>>(dur).count();
        };

        if(printHeaders)
        {
            log << "tag" << ","
                << "ledgerNumAccepted" << ","
                << "ledgerNumFullyValidated" << ","
                << "ledgerRateAccepted" << ","
                << "ledgerRateFullyValidated" << ","
                << "ledgerLatencyAcceptToAccept10Pctl" << ","
                << "ledgerLatencyAcceptToAccept50Pctl" << ","
                << "ledgerLatencyAcceptToAccept90Pctl" << ","
                << "ledgerLatencyFullyValidToFullyValid10Pctl" << ","
                << "ledgerLatencyFullyValidToFullyValid50Pctl" << ","
                << "ledgerLatencyFullyValidToFullyValid90Pctl"
                << std::endl;
        }

        log << tag << ","
            // ledgerNumAccepted
            << accepted << ","
            // ledgerNumFullyValidated
            << fullyValidated << ","
            // ledgerRateAccepted
            << std::setprecision(2) << perSec(accepted) << ","
            // ledgerRateFullyValidated
            << std::setprecision(2) << perSec(fullyValidated) << ","
            // ledgerLatencyAcceptToAccept10Pctl
            << std::setprecision(2) << fmtS(acceptToAccept.percentile(0.1f)) << ","
            // ledgerLatencyAcceptToAccept50Pctl
            << std::setprecision(2) << fmtS(acceptToAccept.percentile(0.5f)) << ","
            // ledgerLatencyAcceptToAccept90Pctl
            << std::setprecision(2) << fmtS(acceptToAccept.percentile(0.9f)) << ","
            // ledgerLatencyFullyValidToFullyValid10Pctl
            << std::setprecision(2) << fmtS(fullyValidToFullyValid.percentile(0.1f)) << ","
            // ledgerLatencyFullyValidToFullyValid50Pctl
            << std::setprecision(2) << fmtS(fullyValidToFullyValid.percentile(0.5f)) << ","
            // ledgerLatencyFullyValidToFullyValid90Pctl
            << std::setprecision(2) << fmtS(fullyValidToFullyValid.percentile(0.9f))
            << std::endl;
    }
};

/** Write out stream of ledger activity

    Writes information about every accepted and fully-validated ledger to a
    provided std::ostream.
*/
struct StreamCollector
{
    std::ostream& out;

    // Ignore most events by default
    template <class E>
    void
    on(PeerID, SimTime, E const& e)
    {
    }

    void
    on(PeerID who, SimTime when, AcceptLedger const& e)
    {
        out << when.time_since_epoch().count() << ": Node " << who << " accepted "
            << "L" << e.ledger.id() << " " << e.ledger.txs() << "\n";
    }

    void
    on(PeerID who, SimTime when, FullyValidateLedger const& e)
    {
        out << when.time_since_epoch().count() << ": Node " << who
            << " fully-validated " << "L"<< e.ledger.id() << " " << e.ledger.txs()
            << "\n";
    }
};

/** Saves information about Jumps for closed and fully validated ledgers. A
    jump occurs when a node closes/fully validates a new ledger that is not the
    immediate child of the prior closed/fully validated ledgers. This includes
    jumps across branches and jumps ahead in the same branch of ledger history.
*/
struct JumpCollector
{
    struct Jump
    {
        PeerID id;
        SimTime when;
        Ledger from;
        Ledger to;
    };

    std::vector<Jump> closeJumps;
    std::vector<Jump> fullyValidatedJumps;

    // Ignore most events by default
    template <class E>
    void
    on(PeerID, SimTime, E const& e)
    {
    }

    void
    on(PeerID who, SimTime when, AcceptLedger const& e)
    {
        // Not a direct child -> parent switch
        if(e.ledger.parentID() != e.prior.id())
            closeJumps.emplace_back(Jump{who, when, e.prior, e.ledger});
    }

    void
    on(PeerID who, SimTime when, FullyValidateLedger const& e)
    {
        // Not a direct child -> parent switch
        if (e.ledger.parentID() != e.prior.id())
            fullyValidatedJumps.emplace_back(
                Jump{who, when, e.prior, e.ledger});
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif

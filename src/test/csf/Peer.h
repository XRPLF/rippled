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
#ifndef RIPPLE_TEST_CSF_PEER_H_INCLUDED
#define RIPPLE_TEST_CSF_PEER_H_INCLUDED

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>

#include <test/csf/Ledger.h>
#include <test/csf/Tx.h>
#include <test/csf/UNL.h>

namespace ripple {
namespace test {
namespace csf {

/** Store validations reached by peers */
struct Validation
{
    PeerID id;
    Ledger::ID ledger;
    Ledger::ID prevLedger;
};

namespace bc = boost::container;

class Validations
{
    //< Ledgers seen by peers, saved in order received (which should be order
    //<  created)
    bc::flat_map<Ledger::ID, bc::flat_set<PeerID>> nodesFromLedger;
    bc::flat_map<Ledger::ID, bc::flat_set<PeerID>> nodesFromPrevLedger;
    bc::flat_map<Ledger::ID, bc::flat_map<Ledger::ID, std::size_t>>
        childLedgers;

public:
    void
    update(Validation const& v)
    {
        nodesFromLedger[v.ledger].insert(v.id);
        if (v.ledger.seq > 0)
        {
            nodesFromPrevLedger[v.prevLedger].insert(v.id);
            childLedgers[v.prevLedger][v.ledger]++;
        }
    }

    //< The number of peers who have validated this ledger
    std::size_t
    proposersValidated(Ledger::ID const& prevLedger) const
    {
        auto it = nodesFromLedger.find(prevLedger);
        if (it != nodesFromLedger.end())
            return it->second.size();
        return 0;
    }

    /** The number of peers that are past this ledger, i.e.
        they have a newer most recent ledger, but have this ledger
        as an ancestor.
    */
    std::size_t
    proposersFinished(Ledger::ID const& prevLedger) const
    {
        auto it = nodesFromPrevLedger.find(prevLedger);
        if (it != nodesFromPrevLedger.end())
            return it->second.size();
        return 0;
    }

    /** Returns the ledger starting from prevLedger with the most validations.
     */
    Ledger::ID
    getBestLCL(Ledger::ID const& currLedger, Ledger::ID const& prevLedger) const
    {
        auto it = childLedgers.find(prevLedger);
        if (it != childLedgers.end() && !it->second.empty())
        {
            std::size_t bestCount = 0;
            Ledger::ID bestLedger;

            for (auto const& b : it->second)
            {
                auto currCount = b.second;
                if (currLedger == b.first)
                    currCount++;
                if (currCount > bestCount)
                    bestLedger = b.first;
                if (currCount == bestCount && currLedger == b.first)
                    bestLedger = b.first;
            }
            return bestLedger;
        }
        return currLedger;
    }
};

/** Proposal is a position taken in the consensus process and is represented
    directly from the generic types.
*/
using Proposal = ConsensusProposal<PeerID, Ledger::ID, TxSetType>;

struct Traits
{
    using Ledger_t = Ledger;
    using NodeID_t = PeerID;
    using TxSet_t = TxSet;
};

/** Represents a single node participating in the consensus process.
    It implements the Callbacks required by Consensus.
*/
struct Peer : public Consensus<Peer, Traits>
{
    using Base = Consensus<Peer, Traits>;

    //! Our unique ID
    PeerID id;

    //! openTxs that haven't been closed in a ledger yet
    TxSetType openTxs;

    //! last ledger this peer closed
    Ledger lastClosedLedger;

    //! Handle to network for sending messages
    BasicNetwork<Peer*>& net;

    //! UNL of trusted peers
    UNL unl;

    //! Most recent ledger completed by peers
    Validations peerValidations;

    // The ledgers, proposals, TxSets and Txs this peer has seen
    bc::flat_map<Ledger::ID, Ledger> ledgers;

    //! Map from Ledger::ID to vector of Positions with that ledger
    //! as the prior ledger
    bc::flat_map<Ledger::ID, std::vector<Proposal>> peerPositions_;
    bc::flat_map<TxSet::ID, TxSet> txSets;

    int completedLedgers = 0;
    int targetLedgers = std::numeric_limits<int>::max();

    //! Skew samples from the network clock; to be refactored into a
    //! clock time once it is provided separately from the network.
    std::chrono::seconds clockSkew{0};

    //! Delay in processing validations from remote peers
    std::chrono::milliseconds validationDelay{0};

    //! Delay in acquiring missing ledger from the network
    std::chrono::milliseconds missingLedgerDelay{0};

    bool validating_ = true;
    bool proposing_ = true;

    //! All peers start from the default constructed ledger
    Peer(PeerID i, BasicNetwork<Peer*>& n, UNL const& u)
        : Consensus<Peer, Traits>(n.clock(), beast::Journal{})
        , id{i}
        , net{n}
        , unl(u)
    {
        ledgers[lastClosedLedger.id()] = lastClosedLedger;
    }

    Ledger const*
    acquireLedger(Ledger::ID const& ledgerHash)
    {
        auto it = ledgers.find(ledgerHash);
        if (it != ledgers.end())
            return &(it->second);

        // TODO Get from network/oracle properly!

        for (auto const& link : net.links(this))
        {
            auto const& p = *link.to;
            auto it = p.ledgers.find(ledgerHash);
            if (it != p.ledgers.end())
            {
                schedule(
                    missingLedgerDelay,
                    [ this, ledgerHash, ledger = it->second ]() {
                        ledgers.emplace(ledgerHash, ledger);
                    });
                if (missingLedgerDelay == 0ms)
                    return &ledgers[ledgerHash];
                break;
            }
        }
        return nullptr;
    }

    auto const&
    proposals(Ledger::ID const& ledgerHash)
    {
        return peerPositions_[ledgerHash];
    }

    TxSet const*
    acquireTxSet(TxSet::ID const& setId)
    {
        auto it = txSets.find(setId);
        if (it != txSets.end())
            return &(it->second);
        // TODO Get from network/oracle instead!
        return nullptr;
    }

    bool
    hasOpenTransactions() const
    {
        return !openTxs.empty();
    }

    std::size_t
    proposersValidated(Ledger::ID const& prevLedger)
    {
        return peerValidations.proposersValidated(prevLedger);
    }

    std::size_t
    proposersFinished(Ledger::ID const& prevLedger)
    {
        return peerValidations.proposersFinished(prevLedger);
    }

    Result
    onClose(Ledger const& prevLedger, NetClock::time_point closeTime, Mode mode)
    {
        TxSet res{openTxs};

        return Result{TxSet{openTxs},
                      Proposal{prevLedger.id(),
                               Proposal::seqJoin,
                               res.id(),
                               closeTime,
                               now(),
                               id}};
    }

    void
    onForceAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        CloseTimes const& rawCloseTimes,
        Mode const& mode)
    {
        onAccept(result, prevLedger, closeResolution, rawCloseTimes, mode);
    }

    void
    onAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        CloseTimes const& rawCloseTimes,
        Mode const& mode)
    {
        auto newLedger = prevLedger.close(
            result.set.txs_,
            closeResolution,
            rawCloseTimes.self,
            result.position.closeTime() != NetClock::time_point{});
        ledgers[newLedger.id()] = newLedger;

        lastClosedLedger = newLedger;

        auto it =
            std::remove_if(openTxs.begin(), openTxs.end(), [&](Tx const& tx) {
                return result.set.exists(tx.id());
            });
        openTxs.erase(it, openTxs.end());

        if (validating_)
            relay(Validation{id, newLedger.id(), newLedger.parentID()});

        // kick off the next round...
        // in the actual implementation, this passes back through
        // network ops
        ++completedLedgers;
        // startRound sets the LCL state, so we need to call it once after
        // the last requested round completes
        // TODO: reconsider this and instead just save LCL generated here?
        if (completedLedgers <= targetLedgers)
        {
            startRound(
                now(), lastClosedLedger.id(), lastClosedLedger, proposing_);
        }
    }

    Ledger::ID
    getPrevLedger(Ledger::ID const& ledgerID, Ledger const& ledger, Mode mode)
    {
        // TODO: Use generic validation code
        if (mode != Mode::wrongLedger && ledgerID.seq > 0 &&
            ledger.id().seq > 0)
            return peerValidations.getBestLCL(ledgerID, ledger.parentID());
        return ledgerID;
    }

    void
    propose(Proposal const& pos)
    {
        if (proposing_)
            relay(pos);
    }

    //-------------------------------------------------------------------------
    // non-callback helpers
    void
    receive(Proposal const& p)
    {
        if (unl.find(p.nodeID()) == unl.end())
            return;

        // TODO: Be sure this is a new proposal!!!!!
        auto& dest = peerPositions_[p.prevLedger()];
        if (std::find(dest.begin(), dest.end(), p) != dest.end())
            return;

        dest.push_back(p);
        peerProposal(now(), p);
    }

    void
    receive(TxSet const& txs)
    {
        // save and map complete?
        auto it = txSets.insert(std::make_pair(txs.id(), txs));
        if (it.second)
            gotTxSet(now(), txs);
    }

    void
    receive(Tx const& tx)
    {
        if (openTxs.find(tx.id()) == openTxs.end())
        {
            openTxs.insert(tx);
            // relay to peers???
            relay(tx);
        }
    }

    void
    receive(Validation const& v)
    {
        if (unl.find(v.id) != unl.end())
        {
            schedule(validationDelay, [&, v]() { peerValidations.update(v); });
        }
    }

    template <class T>
    void
    relay(T const& t)
    {
        for (auto const& link : net.links(this))
            net.send(
                this, link.to, [ msg = t, to = link.to ] { to->receive(msg); });
    }

    // Receive and relay locally submitted transaction
    void
    submit(Tx const& tx)
    {
        receive(tx);
        relay(tx);
    }

    void
    timerEntry()
    {
        Base::timerEntry(now());
        // only reschedule if not completed
        if (completedLedgers < targetLedgers)
            net.timer(LEDGER_GRANULARITY, [&]() { timerEntry(); });
    }
    void
    start()
    {
        net.timer(LEDGER_GRANULARITY, [&]() { timerEntry(); });
        // The ID is the one we have seen the most validations for
        // In practice, we might not actually have that ledger itself yet,
        // so there is no gaurantee that bestLCL == lastClosedLedger.id()
        auto bestLCL = peerValidations.getBestLCL(
            lastClosedLedger.id(), lastClosedLedger.parentID());
        startRound(now(), bestLCL, lastClosedLedger, proposing_);
    }

    NetClock::time_point
    now() const
    {
        // We don't care about the actual epochs, but do want the
        // generated NetClock time to be well past its epoch to ensure
        // any subtractions of two NetClock::time_point in the consensu
        // code are positive. (e.g. PROPOSE_FRESHNESS)
        using namespace std::chrono;
        return NetClock::time_point(duration_cast<NetClock::duration>(
            net.now().time_since_epoch() + 86400s + clockSkew));
    }

    // Schedule the provided callback in `when` duration, but if
    // `when` is 0, call immediately
    template <class T>
    void
    schedule(std::chrono::nanoseconds when, T&& what)
    {
        if (when == 0ns)
            what();
        else
            net.timer(when, std::forward<T>(what));
    }
};

}  // csf
}  // test
}  // ripple
#endif

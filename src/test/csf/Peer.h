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

#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/consensus/Validations.h>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <algorithm>
#include <test/csf/CollectorRef.h>
#include <test/csf/Scheduler.h>
#include <test/csf/TrustGraph.h>
#include <test/csf/Tx.h>
#include <test/csf/Validation.h>
#include <test/csf/events.h>
#include <test/csf/ledgers.h>

namespace ripple {
namespace test {
namespace csf {

namespace bc = boost::container;

/** A single peer in the simulation.

    This is the main work-horse of the consensus simulation framework and is
    where many other components are integrated. The peer

     - Implements the Callbacks required by Consensus
     - Manages trust & network connections with other peers
     - Issues events back to the simulation based on its actions for analysis
       by Collectors
     - Exposes most internal state for forcibly simulating arbitrary scenarios
*/
struct Peer
{
    /** Basic wrapper of a proposed position taken by a peer.

        For real consensus, this would add additional data for serialization
        and signing. For simulation, nothing extra is needed.
    */
    class Position
    {
    public:
        Position(Proposal const& p) : proposal_(p)
        {
        }

        Proposal const&
        proposal() const
        {
            return proposal_;
        }

        Json::Value
        getJson() const
        {
            return proposal_.getJson();
        }

    private:
        Proposal proposal_;
    };


    /** Simulated delays in internal peer processing.
     */
    struct ProcessingDelays
    {
        //! Delay in consensus calling doAccept to accepting and issuing
        //! validation
        //! TODO: This should be a function of the number of transactions
        std::chrono::milliseconds ledgerAccept{0};

        //! Delay in processing validations from remote peers
        std::chrono::milliseconds recvValidation{0};

        // Return the receive delay for message type M, default is no delay
        // Received delay is the time from receiving the message to actually
        // handling it.
        template <class M>
        SimDuration
        onReceive(M const&) const
        {
            return SimDuration{};
        }

        SimDuration
        onReceive(Validation const&) const
        {
            return recvValidation;
        }
    };

    /** Generic Validations adaptor that simply ignores recently stale validations
    */
    class ValAdaptor
    {
        Peer& p_;

    public:
        struct Mutex
        {
            void
            lock()
            {
            }

            void
            unlock()
            {
            }
        };

        using Validation = csf::Validation;
        using Ledger = csf::Ledger;

        ValAdaptor(Peer& p) : p_{p}
        {
        }

        NetClock::time_point
        now() const
        {
            return p_.now();
        }

        void
        onStale(Validation&& v)
        {
        }

        void
        flush(hash_map<PeerID, Validation>&& remaining)
        {
        }

        boost::optional<Ledger>
        acquire(Ledger::ID const & id)
        {
            if(Ledger const * ledger = p_.acquireLedger(id))
                return *ledger;
            return boost::none;
        }
    };

    //! Type definitions for generic consensus
    using Ledger_t = Ledger;
    using NodeID_t = PeerID;
    using TxSet_t = TxSet;
    using PeerPosition_t = Position;
    using Result = ConsensusResult<Peer>;

    //! Logging support that prefixes messages with the peer ID
    beast::WrappedSink sink;
    beast::Journal j;

    //! Generic consensus
    Consensus<Peer> consensus;

    //! Our unique ID
    PeerID id;

    //! Current signing key
    PeerKey key;

    //! The oracle that manages unique ledgers
    LedgerOracle& oracle;

    //! Scheduler of events
    Scheduler& scheduler;

    //! Handle to network for sending messages
    BasicNetwork<Peer*>& net;

    //! Handle to Trust graph of network
    TrustGraph<Peer*>& trustGraph;

    //! openTxs that haven't been closed in a ledger yet
    TxSetType openTxs;

    //! The last ledger closed by this node
    Ledger lastClosedLedger;

    //! Ledgers this node has closed or loaded from the network
    hash_map<Ledger::ID, Ledger> ledgers;

    //! Validations from trusted nodes
    Validations<ValAdaptor> validations;

    //! The most recent ledger that has been fully validated by the network from
    //! the perspective of this Peer
    Ledger fullyValidatedLedger;

    //-------------------------------------------------------------------------
    // Store most network messages; these could be purged if memory use ever
    // becomes problematic

    //! Map from Ledger::ID to vector of Positions with that ledger
    //! as the prior ledger
    bc::flat_map<Ledger::ID, std::vector<Proposal>> peerPositions;
    //! TxSet associated with a TxSet::ID
    bc::flat_map<TxSet::ID, TxSet> txSets;

    // Ledgers/TxSets we are acquiring and when that request times out
    bc::flat_map<Ledger::ID,SimTime> acquiringLedgers;
    bc::flat_map<TxSet::ID,SimTime> acquiringTxSets;

    //! The number of ledgers this peer has completed
    int completedLedgers = 0;

    //! The number of ledgers this peer should complete before stopping to run
    int targetLedgers = std::numeric_limits<int>::max();

    //! Skew of time relative to the common scheduler clock
    std::chrono::seconds clockSkew{0};

    //! Simulated delays to use for internal processing
    ProcessingDelays delays;

    //! Whether to simulate running as validator or a tracking node
    bool runAsValidator = true;

    //! Enforce invariants on validation sequence numbers
    SeqEnforcer<Ledger::Seq> seqEnforcer;

    //TODO: Consider removing these two, they are only a convenience for tests
    // Number of proposers in the prior round
    std::size_t prevProposers = 0;
    // Duration of prior round
    std::chrono::milliseconds prevRoundTime;

    // Quorum of validations needed for a ledger to be fully validated
    // TODO: Use the logic in ValidatorList to set this dynamically
    std::size_t quorum = 0;

    // Simulation parameters
    ConsensusParms consensusParms;

    //! The collectors to report events to
    CollectorRefs & collectors;

    /** Constructor

        @param i Unique PeerID
        @param s Simulation Scheduler
        @param o Simulation Oracle
        @param n Simulation network
        @param tg Simulation trust graph
        @param c Simulation collectors
        @param jIn Simulation journal

    */
    Peer(
        PeerID i,
        Scheduler& s,
        LedgerOracle& o,
        BasicNetwork<Peer*>& n,
        TrustGraph<Peer*>& tg,
        CollectorRefs& c,
        beast::Journal jIn)
        : sink(jIn, "Peer " + to_string(i) + ": ")
        , j(sink)
        , consensus(s.clock(), *this, j)
        , id{i}
        , key{id, 0}
        , oracle{o}
        , scheduler{s}
        , net{n}
        , trustGraph(tg)
        , lastClosedLedger{Ledger::MakeGenesis{}}
        , validations{ValidationParms{}, s.clock(), *this}
        , fullyValidatedLedger{Ledger::MakeGenesis{}}
        , collectors{c}
    {
        // All peers start from the default constructed genesis ledger
        ledgers[lastClosedLedger.id()] = lastClosedLedger;

        // nodes always trust themselves . . SHOULD THEY?
        trustGraph.trust(this, this);
    }

    /**  Schedule the provided callback in `when` duration, but if
        `when` is 0, call immediately
    */
    template <class T>
    void
    schedule(std::chrono::nanoseconds when, T&& what)
    {
        using namespace std::chrono_literals;

        if (when == 0ns)
            what();
        else
            scheduler.in(when, std::forward<T>(what));
    }

    // Issue a new event to the collectors
    template <class E>
    void
    issue(E const & event)
    {
        // Use the scheduler time and not the peer's (skewed) local time
        collectors.on(id, scheduler.now(), event);
    }

    //--------------------------------------------------------------------------
    // Trust and Network members
    // Methods for modifying and querying the network and trust graphs from
    // the perspective of this Peer

    //< Extend trust to a peer
    void
    trust(Peer & o)
    {
        trustGraph.trust(this, &o);
    }

    //< Revoke trust from a peer
    void
    untrust(Peer & o)
    {
        trustGraph.untrust(this, &o);
    }

    //< Check whether we trust a peer
    bool
    trusts(Peer & o)
    {
        return trustGraph.trusts(this, &o);
    }

    //< Check whether we trust a peer based on its ID
    bool
    trusts(PeerID const & oId)
    {
        for(auto const & p : trustGraph.trustedPeers(this))
            if(p->id == oId)
                return true;
        return false;
    }

    /** Create network connection

        Creates a new outbound connection to another Peer if none exists

        @param o The peer with the inbound connection
        @param dur The fixed delay for messages between the two Peers
        @return Whether the connection was created.
    */

    bool
    connect(Peer & o, SimDuration dur)
    {
        return net.connect(this, &o, dur);
    }

    /** Remove a network connection

        Removes a connection between peers if one exists

        @param o The peer we disconnect from
        @return Whether the connection was removed
    */
    bool
    disconnect(Peer & o)
    {
        return net.disconnect(this, &o);
    }

    //--------------------------------------------------------------------------
    // Generic Consensus members

    // Attempt to acquire the Ledger associated with the given ID
    Ledger const*
    acquireLedger(Ledger::ID const& ledgerID)
    {
        using namespace std::chrono;

        auto it = ledgers.find(ledgerID);
        if (it != ledgers.end())
            return &(it->second);

        // No peers
        if(net.links(this).empty())
            return nullptr;

        // Don't retry if we already are acquiring it and haven't timed out
        auto aIt = acquiringLedgers.find(ledgerID);
        if(aIt!= acquiringLedgers.end())
        {
            if(scheduler.now() < aIt->second)
                return nullptr;
        }


        SimDuration minDuration{10s};
        for (auto const& link : net.links(this))
        {
            minDuration = std::min(minDuration, link.data.delay);

            // Send a messsage to neighbors to find the ledger
            net.send(
                this, link.target, [ to = link.target, from = this, ledgerID ]() {
                    auto it = to->ledgers.find(ledgerID);
                    if (it != to->ledgers.end())
                    {
                        // if the ledger is found, send it back to the original
                        // requesting peer where it is added to the available
                        // ledgers
                        to->net.send(to, from, [ from, ledger = it->second ]() {
                            from->acquiringLedgers.erase(ledger.id());
                            from->ledgers.emplace(ledger.id(), ledger);
                        });
                    }
                });
        }
        acquiringLedgers[ledgerID] = scheduler.now() + 2 * minDuration;
        return nullptr;
    }

    // Attempt to acquire the TxSet associated with the given ID
    TxSet const*
    acquireTxSet(TxSet::ID const& setId)
    {
        auto it = txSets.find(setId);
        if (it != txSets.end())
            return &(it->second);

        // No peers
        if(net.links(this).empty())
            return nullptr;

        // Don't retry if we already are acquiring it and haven't timed out
        auto aIt = acquiringTxSets.find(setId);
        if(aIt!= acquiringTxSets.end())
        {
            if(scheduler.now() < aIt->second)
                return nullptr;
        }

        SimDuration minDuration{10s};
        for (auto const& link : net.links(this))
        {
            minDuration = std::min(minDuration, link.data.delay);
            // Send a message to neighbors to find the tx set
            net.send(
                this, link.target, [ to = link.target, from = this, setId ]() {
                    auto it = to->txSets.find(setId);
                    if (it != to->txSets.end())
                    {
                        // If the txSet is found, send it back to the original
                        // requesting peer, where it is handled like a TxSet
                        // that was broadcast over the network
                        to->net.send(to, from, [ from, txSet = it->second ]() {
                            from->acquiringTxSets.erase(txSet.id());
                            from->handle(txSet);
                        });
                    }
                });
        }
        acquiringTxSets[setId] = scheduler.now() + 2 * minDuration;
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
        return validations.numTrustedForLedger(prevLedger);
    }

    std::size_t
    proposersFinished(Ledger const & prevLedger, Ledger::ID const& prevLedgerID)
    {
        return validations.getNodesAfter(prevLedger, prevLedgerID);
    }

    Result
    onClose(
        Ledger const& prevLedger,
        NetClock::time_point closeTime,
        ConsensusMode mode)
    {
        issue(CloseLedger{prevLedger, openTxs});

        return Result(
            TxSet{openTxs},
            Proposal(
                prevLedger.id(),
                Proposal::seqJoin,
                TxSet::calcID(openTxs),
                closeTime,
                now(),
                id));
    }

    void
    onForceAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        ConsensusCloseTimes const& rawCloseTimes,
        ConsensusMode const& mode,
        Json::Value&& consensusJson)
    {
        onAccept(
            result,
            prevLedger,
            closeResolution,
            rawCloseTimes,
            mode,
            std::move(consensusJson));
    }

    void
    onAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        ConsensusCloseTimes const& rawCloseTimes,
        ConsensusMode const& mode,
        Json::Value&& consensusJson)
    {
        schedule(delays.ledgerAccept, [=]() {
            const bool proposing = mode == ConsensusMode::proposing;
            const bool consensusFail = result.state == ConsensusState::MovedOn;

            TxSet const acceptedTxs = injectTxs(prevLedger, result.txns);
            Ledger const newLedger = oracle.accept(
                prevLedger,
                acceptedTxs.txs(),
                closeResolution,
                result.position.closeTime());
            ledgers[newLedger.id()] = newLedger;

            issue(AcceptLedger{newLedger, lastClosedLedger});
            prevProposers = result.proposers;
            prevRoundTime = result.roundTime.read();
            lastClosedLedger = newLedger;

            auto const it = std::remove_if(
                openTxs.begin(), openTxs.end(), [&](Tx const& tx) {
                    return acceptedTxs.exists(tx.id());
                });
            openTxs.erase(it, openTxs.end());

            // Only send validation if the new ledger is compatible with our
            // fully validated ledger
            bool const isCompatible =
                newLedger.isAncestor(fullyValidatedLedger);

            // Can only send one validated ledger per seq
            if (runAsValidator && isCompatible && !consensusFail &&
                seqEnforcer(
                    scheduler.now(), newLedger.seq(), validations.parms()))
            {
                bool isFull = proposing;

                Validation v{newLedger.id(),
                             newLedger.seq(),
                             now(),
                             now(),
                             key,
                             id,
                             isFull};
                // share the new validation; it is trusted by the receiver
                share(v);
                // we trust ourselves
                addTrustedValidation(v);
            }

            checkFullyValidated(newLedger);

            // kick off the next round...
            // in the actual implementation, this passes back through
            // network ops
            ++completedLedgers;
            // startRound sets the LCL state, so we need to call it once after
            // the last requested round completes
            if (completedLedgers <= targetLedgers)
            {
                startRound();
            }
        });
    }

    // Earliest allowed sequence number when checking for ledgers with more
    // validations than our current ledger
    Ledger::Seq
    earliestAllowedSeq() const
    {
        return fullyValidatedLedger.seq();
    }

    Ledger::ID
    getPrevLedger(
        Ledger::ID const& ledgerID,
        Ledger const& ledger,
        ConsensusMode mode)
    {
        // only do if we are past the genesis ledger
        if (ledger.seq() == Ledger::Seq{0})
            return ledgerID;

        Ledger::ID const netLgr =
            validations.getPreferred(ledger, earliestAllowedSeq());

        if (netLgr != ledgerID)
        {
            JLOG(j.trace()) << Json::Compact(validations.getJsonTrie());
            issue(WrongPrevLedger{ledgerID, netLgr});
        }

        return netLgr;
    }

    void
    propose(Proposal const& pos)
    {
        share(pos);
    }

    ConsensusParms const&
    parms() const
    {
        return consensusParms;
    }

    // Not interested in tracking consensus mode changes for now
    void
    onModeChange(ConsensusMode, ConsensusMode)
    {
    }

    // Share a message by broadcasting to all connected peers
    template <class M>
    void
    share(M const& m)
    {
        issue(Share<M>{m});
        send(BroadcastMesg<M>{m,router.nextSeq++, this->id}, this->id);
    }

    // Unwrap the Position and share the raw proposal
    void
    share(Position const & p)
    {
        share(p.proposal());
    }

    //--------------------------------------------------------------------------
    // Validation members

    /** Add a trusted validation and return true if it is worth forwarding */
    bool
    addTrustedValidation(Validation v)
    {
        v.setTrusted();
        v.setSeen(now());
        ValStatus const res = validations.add(v.nodeID(), v);

        if(res == ValStatus::stale)
            return false;

        // Acquire will try to get from network if not already local
        if (Ledger const* lgr = acquireLedger(v.ledgerID()))
            checkFullyValidated(*lgr);
        return true;
    }

    /** Check if a new ledger can be deemed fully validated */
    void
    checkFullyValidated(Ledger const& ledger)
    {
        // Only consider ledgers newer than our last fully validated ledger
        if (ledger.seq() <= fullyValidatedLedger.seq())
            return;

        std::size_t const count = validations.numTrustedForLedger(ledger.id());
        std::size_t const numTrustedPeers = trustGraph.graph().outDegree(this);
        quorum = static_cast<std::size_t>(std::ceil(numTrustedPeers * 0.8));
        if (count >= quorum && ledger.isAncestor(fullyValidatedLedger))
        {
            issue(FullyValidateLedger{ledger, fullyValidatedLedger});
            fullyValidatedLedger = ledger;
        }
    }

    //-------------------------------------------------------------------------
    // Peer messaging members

    // Basic Sequence number router
    //   A message that will be flooded across the network is tagged with a
    //   seqeuence number by the origin node in a BroadcastMesg. Receivers will
    //   ignore a message as stale if they've already processed a newer sequence
    //   number, or will process and potentially relay the message along.
    //
    //  The various bool handle(MessageType) members do the actual processing
    //  and should return true if the message should continue to be sent to peers.
    //
    //  WARN: This assumes messages are received and processed in the order they
    //        are sent, so that a peer receives a message with seq 1 from node 0
    //        before seq 2 from node 0, etc.
    //  TODO: Break this out into a class and identify type interface to allow
    //        alternate routing strategies
    template <class M>
    struct BroadcastMesg
    {
        M mesg;
        std::size_t seq;
        PeerID origin;
    };

    struct Router
    {
        std::size_t nextSeq = 1;
        bc::flat_map<PeerID, std::size_t> lastObservedSeq;
    };

    Router router;

    // Send a broadcast message to all peers
    template <class M>
    void
    send(BroadcastMesg<M> const& bm, PeerID from)
    {
        for (auto const& link : net.links(this))
        {
            if (link.target->id != from && link.target->id != bm.origin)
            {
                // cheat and don't bother sending if we know it has already been
                // used on the other end
                if (link.target->router.lastObservedSeq[bm.origin] < bm.seq)
                {
                    issue(Relay<M>{link.target->id, bm.mesg});
                    net.send(
                        this, link.target, [to = link.target, bm, id = this->id ] {
                            to->receive(bm, id);
                        });
                }
            }
        }
    }

    // Receive a shared message, process it and consider continuing to relay it
    template <class M>
    void
    receive(BroadcastMesg<M> const& bm, PeerID from)
    {
        issue(Receive<M>{from, bm.mesg});
        if (router.lastObservedSeq[bm.origin] < bm.seq)
        {
            router.lastObservedSeq[bm.origin] = bm.seq;
            schedule(delays.onReceive(bm.mesg), [this, bm, from]
            {
                if (handle(bm.mesg))
                    send(bm, from);
            });
        }
    }

    // Type specific receive handlers, return true if the message should
    // continue to be broadcast to peers
    bool
    handle(Proposal const& p)
    {
        // Only relay untrusted proposals on the same ledger
        if(!trusts(p.nodeID()))
            return p.prevLedger() == lastClosedLedger.id();

        // TODO: This always suppresses relay of peer positions already seen
        // Should it allow forwarding if for a recent ledger ?
        auto& dest = peerPositions[p.prevLedger()];
        if (std::find(dest.begin(), dest.end(), p) != dest.end())
            return false;

        dest.push_back(p);

        // Rely on consensus to decide whether to relay
        return consensus.peerProposal(now(), Position{p});
    }

    bool
    handle(TxSet const& txs)
    {
        auto const it = txSets.insert(std::make_pair(txs.id(), txs));
        if (it.second)
            consensus.gotTxSet(now(), txs);
        // relay only if new
        return it.second;
    }

    bool
    handle(Tx const& tx)
    {
        // Ignore and suppress relay of transactions already in last ledger
        TxSetType const& lastClosedTxs = lastClosedLedger.txs();
        if (lastClosedTxs.find(tx) != lastClosedTxs.end())
            return false;

        // only relay if it was new to our open ledger
        return openTxs.insert(tx).second;

    }

    bool
    handle(Validation const& v)
    {
        // TODO: This is not relaying untrusted validations
        if (!trusts(v.nodeID()))
            return false;

        // Will only relay if current
        return addTrustedValidation(v);
    }

    //--------------------------------------------------------------------------
    //  A locally submitted transaction
    void
    submit(Tx const& tx)
    {
        issue(SubmitTx{tx});
        if(handle(tx))
            share(tx);
    }

    //--------------------------------------------------------------------------
    // Simulation "driver" members

    //! Heartbeat timer call
    void
    timerEntry()
    {
        consensus.timerEntry(now());
        // only reschedule if not completed
        if (completedLedgers < targetLedgers)
            scheduler.in(parms().ledgerGranularity, [this]() { timerEntry(); });
    }

    // Called to begin the next round
    void
    startRound()
    {
        // Between rounds, we take the majority ledger
        // In the future, consider taking peer dominant ledger if no validations
        // yet
        Ledger::ID bestLCL =
            validations.getPreferred(lastClosedLedger, earliestAllowedSeq());
        if(bestLCL == Ledger::ID{0})
            bestLCL = lastClosedLedger.id();

        issue(StartRound{bestLCL, lastClosedLedger});

        // Not yet modeling dynamic UNL.
        hash_set<PeerID> nowUntrusted;
        consensus.startRound(
            now(), bestLCL, lastClosedLedger, nowUntrusted, runAsValidator);
    }

    // Start the consensus process assuming it is not yet running
    // This runs forever unless targetLedgers is specified
    void
    start()
    {
        // TODO: Expire validations less frequently?
        validations.expire();
        scheduler.in(parms().ledgerGranularity, [&]() { timerEntry(); });
        startRound();
    }

    NetClock::time_point
    now() const
    {
        // We don't care about the actual epochs, but do want the
        // generated NetClock time to be well past its epoch to ensure
        // any subtractions of two NetClock::time_point in the consensus
        // code are positive. (e.g. proposeFRESHNESS)
        using namespace std::chrono;
        using namespace std::chrono_literals;
        return NetClock::time_point(duration_cast<NetClock::duration>(
            scheduler.now().time_since_epoch() + 86400s + clockSkew));
    }


    Ledger::ID
    prevLedgerID() const
    {
        return consensus.prevLedgerID();
    }

    //-------------------------------------------------------------------------
    // Injects a specific transaction when generating the ledger following
    // the provided sequence.  This allows simulating a byzantine failure in
    // which a node generates the wrong ledger, even when consensus worked
    // properly.
    // TODO: Make this more robust
    hash_map<Ledger::Seq, Tx> txInjections;

    /** Inject non-consensus Tx

        Injects a transactionsinto the ledger following prevLedger's sequence
        number.

        @param prevLedger The ledger we are building the new ledger on top of
        @param src The Consensus TxSet
        @return Consensus TxSet with inject transactions added if prevLedger.seq
                matches a previously registered Tx.
    */
    TxSet
    injectTxs(Ledger prevLedger, TxSet const & src)
    {
        auto const it = txInjections.find(prevLedger.seq());

        if(it == txInjections.end())
            return src;
        TxSetType res{src.txs()};
        res.insert(it->second);

        return TxSet{res};

    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple
#endif


//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#ifndef RIPPLE_CONSENSUS_CONSENSUS_H_INCLUDED
#define RIPPLE_CONSENSUS_CONSENSUS_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/consensus/ConsensusProposal.h>
#include <ripple/consensus/ConsensusParms.h>
#include <ripple/consensus/ConsensusTypes.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/consensus/DisputedTx.h>
#include <ripple/json/json_writer.h>
#include <boost/logic/tribool.hpp>
#include <sstream>

namespace ripple {


/** Determines whether the current ledger should close at this time.

    This function should be called when a ledger is open and there is no close
    in progress, or when a transaction is received and no close is in progress.

    @param anyTransactions indicates whether any transactions have been received
    @param prevProposers proposers in the last closing
    @param proposersClosed proposers who have currently closed this ledger
    @param proposersValidated proposers who have validated the last closed
                              ledger
    @param prevRoundTime time for the previous ledger to reach consensus
    @param timeSincePrevClose  time since the previous ledger's (possibly rounded)
                        close time
    @param openTime     duration this ledger has been open
    @param idleInterval the network's desired idle interval
    @param parms        Consensus constant parameters
    @param j            journal for logging
*/
bool
shouldCloseLedger(
    bool anyTransactions,
    std::size_t prevProposers,
    std::size_t proposersClosed,
    std::size_t proposersValidated,
    std::chrono::milliseconds prevRoundTime,
    std::chrono::milliseconds timeSincePrevClose,
    std::chrono::milliseconds openTime,
    std::chrono::milliseconds idleInterval,
    ConsensusParms const & parms,
    beast::Journal j);

/** Determine whether the network reached consensus and whether we joined.

    @param prevProposers proposers in the last closing (not including us)
    @param currentProposers proposers in this closing so far (not including us)
    @param currentAgree proposers who agree with us
    @param currentFinished proposers who have validated a ledger after this one
    @param previousAgreeTime how long, in milliseconds, it took to agree on the
                             last ledger
    @param currentAgreeTime how long, in milliseconds, we've been trying to
                            agree
    @param parms            Consensus constant parameters
    @param proposing        whether we should count ourselves
    @param j                journal for logging
*/
ConsensusState
checkConsensus(
    std::size_t prevProposers,
    std::size_t currentProposers,
    std::size_t currentAgree,
    std::size_t currentFinished,
    std::chrono::milliseconds previousAgreeTime,
    std::chrono::milliseconds currentAgreeTime,
    ConsensusParms const & parms,
    bool proposing,
    beast::Journal j);

/** Generic implementation of consensus algorithm.

  Achieves consensus on the next ledger.

  Two things need consensus:

    1.  The set of transactions included in the ledger.
    2.  The close time for the ledger.

  The basic flow:

    1. A call to `startRound` places the node in the `Open` phase.  In this
       phase, the node is waiting for transactions to include in its open
       ledger.
    2. Successive calls to `timerEntry` check if the node can close the ledger.
       Once the node `Close`s the open ledger, it transitions to the
       `Establish` phase.  In this phase, the node shares/receives peer
       proposals on which transactions should be accepted in the closed ledger.
    3. During a subsequent call to `timerEntry`, the node determines it has
       reached consensus with its peers on which transactions to include. It
       transitions to the `Accept` phase. In this phase, the node works on
       applying the transactions to the prior ledger to generate a new closed
       ledger. Once the new ledger is completed, the node shares the validated
       ledger with the network, does some book-keeping, then makes a call to
       `startRound` to start the cycle again.

  This class uses a generic interface to allow adapting Consensus for specific
  applications. The Adaptor template implements a set of helper functions that
  plug the consensus algorithm into a specific application.  It also identifies
  the types that play important roles in Consensus (transactions, ledgers, ...).
  The code stubs below outline the interface and type requirements.  The traits
  types must be copy constructible and assignable.

  @warning The generic implementation is not thread safe and the public methods
  are not intended to be run concurrently.  When in a concurrent environment,
  the application is responsible for ensuring thread-safety.  Simply locking
  whenever touching the Consensus instance is one option.

  @code
  // A single transaction
  struct Tx
  {
    // Unique identifier of transaction
    using ID = ...;

    ID id() const;

  };

  // A set of transactions
  struct TxSet
  {
    // Unique ID of TxSet (not of Tx)
    using ID = ...;
    // Type of individual transaction comprising the TxSet
    using Tx = Tx;

    bool exists(Tx::ID const &) const;
    // Return value should have semantics like Tx const *
    Tx const * find(Tx::ID const &) const ;
    ID const & id() const;

    // Return set of transactions that are not common to this set or other
    // boolean indicates which set it was in
    std::map<Tx::ID, bool> compare(TxSet const & other) const;

    // A mutable view of transactions
    struct MutableTxSet
    {
        MutableTxSet(TxSet const &);
        bool insert(Tx const &);
        bool erase(Tx::ID const &);
    };

    // Construct from a mutable view.
    TxSet(MutableTxSet const &);

    // Alternatively, if the TxSet is itself mutable
    // just alias MutableTxSet = TxSet

  };

  // Agreed upon state that consensus transactions will modify
  struct Ledger
  {
    using ID = ...;
    using Seq = ...;

    // Unique identifier of ledger
    ID const id() const;
    Seq seq() const;
    auto closeTimeResolution() const;
    auto closeAgree() const;
    auto closeTime() const;
    auto parentCloseTime() const;
    Json::Value getJson() const;
  };

  // Wraps a peer's ConsensusProposal
  struct PeerPosition
  {
    ConsensusProposal<
        std::uint32_t, //NodeID,
        typename Ledger::ID,
        typename TxSet::ID> const &
    proposal() const;

  };


  class Adaptor
  {
  public:
      //-----------------------------------------------------------------------
      // Define consensus types
      using Ledger_t = Ledger;
      using NodeID_t = std::uint32_t;
      using TxSet_t = TxSet;
      using PeerPosition_t = PeerPosition;

      //-----------------------------------------------------------------------
      //
      // Attempt to acquire a specific ledger.
      boost::optional<Ledger> acquireLedger(Ledger::ID const & ledgerID);

      // Acquire the transaction set associated with a proposed position.
      boost::optional<TxSet> acquireTxSet(TxSet::ID const & setID);

      // Whether any transactions are in the open ledger
      bool hasOpenTransactions() const;

      // Number of proposers that have validated the given ledger
      std::size_t proposersValidated(Ledger::ID const & prevLedger) const;

      // Number of proposers that have validated a ledger descended from the
      // given ledger; if prevLedger.id() != prevLedgerID, use prevLedgerID
      // for the determination
      std::size_t proposersFinished(Ledger const & prevLedger,
                                    Ledger::ID const & prevLedger) const;

      // Return the ID of the last closed (and validated) ledger that the
      // application thinks consensus should use as the prior ledger.
      Ledger::ID getPrevLedger(Ledger::ID const & prevLedgerID,
                      Ledger const & prevLedger,
                      Mode mode);

      // Called whenever consensus operating mode changes
      void onModeChange(ConsensusMode before, ConsensusMode after);

      // Called when ledger closes
      Result onClose(Ledger const &, Ledger const & prev, Mode mode);

      // Called when ledger is accepted by consensus
      void onAccept(Result const & result,
        RCLCxLedger const & prevLedger,
        NetClock::duration closeResolution,
        CloseTimes const & rawCloseTimes,
        Mode const & mode);

      // Called when ledger was forcibly accepted by consensus via the simulate
      // function.
      void onForceAccept(Result const & result,
        RCLCxLedger const & prevLedger,
        NetClock::duration closeResolution,
        CloseTimes const & rawCloseTimes,
        Mode const & mode);

      // Propose the position to peers.
      void propose(ConsensusProposal<...> const & pos);

      // Share a received peer proposal with other peer's.
      void share(PeerPosition_t const & prop);

      // Share a disputed transaction with peers
      void share(Txn const & tx);

      // Share given transaction set with peers
      void share(TxSet const &s);

      // Consensus timing parameters and constants
      ConsensusParms const &
      parms() const;
  };
  @endcode

  @tparam Adaptor Defines types and provides helper functions needed to adapt
                  Consensus to the larger application.
*/
template <class Adaptor>
class Consensus
{
    using Ledger_t = typename Adaptor::Ledger_t;
    using TxSet_t = typename Adaptor::TxSet_t;
    using NodeID_t = typename Adaptor::NodeID_t;
    using Tx_t = typename TxSet_t::Tx;
    using PeerPosition_t = typename Adaptor::PeerPosition_t;
    using Proposal_t = ConsensusProposal<
        NodeID_t,
        typename Ledger_t::ID,
        typename TxSet_t::ID>;

    using Result = ConsensusResult<Adaptor>;

    // Helper class to ensure adaptor is notified whenver the ConsensusMode
    // changes
    class MonitoredMode
    {
        ConsensusMode mode_;

    public:
        MonitoredMode(ConsensusMode m) : mode_{m}
        {
        }
        ConsensusMode
        get() const
        {
            return mode_;
        }

        void
        set(ConsensusMode mode, Adaptor& a)
        {
            a.onModeChange(mode_, mode);
            mode_ = mode;
        }
    };
public:
    //! Clock type for measuring time within the consensus code
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

    Consensus(Consensus&&) noexcept = default;

    /** Constructor.

        @param clock The clock used to internally sample consensus progress
        @param adaptor The instance of the adaptor class
        @param j The journal to log debug output
    */
    Consensus(clock_type const& clock, Adaptor& adaptor, beast::Journal j);

    /** Kick-off the next round of consensus.

        Called by the client code to start each round of consensus.

        @param now The network adjusted time
        @param prevLedgerID the ID of the last ledger
        @param prevLedger The last ledger
        @param nowUntrusted ID of nodes that are newly untrusted this round
        @param proposing Whether we want to send proposals to peers this round.

        @note @b prevLedgerID is not required to the ID of @b prevLedger since
        the ID may be known locally before the contents of the ledger arrive
    */
    void
    startRound(
        NetClock::time_point const& now,
        typename Ledger_t::ID const& prevLedgerID,
        Ledger_t prevLedger,
        hash_set<NodeID_t> const & nowUntrusted,
        bool proposing);

    /** A peer has proposed a new position, adjust our tracking.

        @param now The network adjusted time
        @param newProposal The new proposal from a peer
        @return Whether we should do delayed relay of this proposal.
    */
    bool
    peerProposal(
        NetClock::time_point const& now,
        PeerPosition_t const& newProposal);

    /** Call periodically to drive consensus forward.

        @param now The network adjusted time
    */
    void
    timerEntry(NetClock::time_point const& now);

    /** Process a transaction set acquired from the network

        @param now The network adjusted time
        @param txSet the transaction set
    */
    void
    gotTxSet(NetClock::time_point const& now, TxSet_t const& txSet);

    /** Simulate the consensus process without any network traffic.

       The end result, is that consensus begins and completes as if everyone
       had agreed with whatever we propose.

       This function is only called from the rpc "ledger_accept" path with the
       server in standalone mode and SHOULD NOT be used during the normal
       consensus process.

       Simulate will call onForceAccept since clients are manually driving
       consensus to the accept phase.

       @param now The current network adjusted time.
       @param consensusDelay Duration to delay between closing and accepting the
                             ledger. Uses 100ms if unspecified.
    */
    void
    simulate(
        NetClock::time_point const& now,
        boost::optional<std::chrono::milliseconds> consensusDelay);

    /** Get the previous ledger ID.

        The previous ledger is the last ledger seen by the consensus code and
        should correspond to the most recent validated ledger seen by this peer.

        @return ID of previous ledger
    */
    typename Ledger_t::ID
    prevLedgerID() const
    {
        return prevLedgerID_;
    }

    ConsensusPhase
    phase() const
    {
        return phase_;
    }

    /** Get the Json state of the consensus process.

        Called by the consensus_info RPC.

        @param full True if verbose response desired.
        @return     The Json state.
    */
    Json::Value
    getJson(bool full) const;

private:
    void
    startRoundInternal(
        NetClock::time_point const& now,
        typename Ledger_t::ID const& prevLedgerID,
        Ledger_t const& prevLedger,
        ConsensusMode mode);

    // Change our view of the previous ledger
    void
    handleWrongLedger(typename Ledger_t::ID const& lgrId);

    /** Check if our previous ledger matches the network's.

        If the previous ledger differs, we are no longer in sync with
        the network and need to bow out/switch modes.
    */
    void
    checkLedger();

    /** If we radically changed our consensus context for some reason,
        we need to replay recent proposals so that they're not lost.
    */
    void
    playbackProposals();

    /** Handle a replayed or a new peer proposal.
    */
    bool
    peerProposalInternal(
        NetClock::time_point const& now,
        PeerPosition_t const& newProposal);

    /** Handle pre-close phase.

        In the pre-close phase, the ledger is open as we wait for new
        transactions.  After enough time has elapsed, we will close the ledger,
        switch to the establish phase and start the consensus process.
    */
    void
    phaseOpen();

    /** Handle establish phase.

        In the establish phase, the ledger has closed and we work with peers
        to reach consensus. Update our position only on the timer, and in this
        phase.

        If we have consensus, move to the accepted phase.
    */
    void
    phaseEstablish();

    /** Evaluate whether pausing increases likelihood of validation.
     *
     *  As a validator that has previously synced to the network, if our most
     *  recent locally-validated ledger did not also achieve
     *  full validation, then consider pausing for awhile based on
     *  the state of other validators.
     *
     *  Pausing may be beneficial in this situation if at least one validator
     *  is known to be on a sequence number earlier than ours. The minimum
     *  number of validators on the same sequence number does not guarantee
     *  consensus, and waiting for all validators may be too time-consuming.
     *  Therefore, a variable threshold is enacted based on the number
     *  of ledgers past network validation that we are on. For the first phase,
     *  the threshold is also the minimum required for quorum. For the last,
     *  no online validators can have a lower sequence number. For intermediate
     *  phases, the threshold is linear between the minimum required for
     *  quorum and 100%. For example, with 3 total phases and a quorum of
     *  80%, the 2nd phase would be 90%. Once the final phase is reached,
     *  if consensus still fails to occur, the cycle is begun again at phase 1.
     *
     * @return Whether to pause to wait for lagging proposers.
     */
    bool
    shouldPause() const;

    // Close the open ledger and establish initial position.
    void
    closeLedger();

    // Adjust our positions to try to agree with other validators.
    void
    updateOurPositions();

    bool
    haveConsensus();

    // Create disputes between our position and the provided one.
    void
    createDisputes(TxSet_t const& o);

    // Update our disputes given that this node has adopted a new position.
    // Will call createDisputes as needed.
    void
    updateDisputes(NodeID_t const& node, TxSet_t const& other);

    // Revoke our outstanding proposal, if any, and cease proposing
    // until this round ends.
    void
    leaveConsensus();

    // The rounded or effective close time estimate from a proposer
    NetClock::time_point
    asCloseTime(NetClock::time_point raw) const;

private:
    Adaptor& adaptor_;

    ConsensusPhase phase_{ConsensusPhase::accepted};
    MonitoredMode mode_{ConsensusMode::observing};
    bool firstRound_ = true;
    bool haveCloseTimeConsensus_ = false;

    clock_type const& clock_;

    // How long the consensus convergence has taken, expressed as
    // a percentage of the time that we expected it to take.
    int convergePercent_{0};

    // How long has this round been open
    ConsensusTimer openTime_;

    NetClock::duration closeResolution_ = ledgerDefaultTimeResolution;

    // Time it took for the last consensus round to converge
    std::chrono::milliseconds prevRoundTime_;

    //-------------------------------------------------------------------------
    // Network time measurements of consensus progress

    // The current network adjusted time.  This is the network time the
    // ledger would close if it closed now
    NetClock::time_point now_;
    NetClock::time_point prevCloseTime_;

    //-------------------------------------------------------------------------
    // Non-peer (self) consensus data

    // Last validated ledger ID provided to consensus
    typename Ledger_t::ID prevLedgerID_;
    // Last validated ledger seen by consensus
    Ledger_t previousLedger_;

    // Transaction Sets, indexed by hash of transaction tree
    hash_map<typename TxSet_t::ID, const TxSet_t> acquired_;

    boost::optional<Result> result_;
    ConsensusCloseTimes rawCloseTimes_;

    //-------------------------------------------------------------------------
    // Peer related consensus data

    // Peer proposed positions for the current round
    hash_map<NodeID_t, PeerPosition_t> currPeerPositions_;

    // Recently received peer positions, available when transitioning between
    // ledgers or rounds
    hash_map<NodeID_t, std::deque<PeerPosition_t>> recentPeerPositions_;

    // The number of proposers who participated in the last consensus round
    std::size_t prevProposers_ = 0;

    // nodes that have bowed out of this consensus process
    hash_set<NodeID_t> deadNodes_;

    // Journal for debugging
    beast::Journal const j_;
};

template <class Adaptor>
Consensus<Adaptor>::Consensus(
    clock_type const& clock,
    Adaptor& adaptor,
    beast::Journal journal)
    : adaptor_(adaptor)
    , clock_(clock)
    , j_{journal}
{
    JLOG(j_.debug()) << "Creating consensus object";
}

template <class Adaptor>
void
Consensus<Adaptor>::startRound(
    NetClock::time_point const& now,
    typename Ledger_t::ID const& prevLedgerID,
    Ledger_t prevLedger,
    hash_set<NodeID_t> const& nowUntrusted,
    bool proposing)
{
    if (firstRound_)
    {
        // take our initial view of closeTime_ from the seed ledger
        prevRoundTime_ = adaptor_.parms().ledgerIDLE_INTERVAL;
        prevCloseTime_ = prevLedger.closeTime();
        firstRound_ = false;
    }
    else
    {
        prevCloseTime_ = rawCloseTimes_.self;
    }

    for(NodeID_t const& n : nowUntrusted)
        recentPeerPositions_.erase(n);

    ConsensusMode startMode =
        proposing ? ConsensusMode::proposing : ConsensusMode::observing;

    // We were handed the wrong ledger
    if (prevLedger.id() != prevLedgerID)
    {
        // try to acquire the correct one
        if (auto newLedger = adaptor_.acquireLedger(prevLedgerID))
        {
            prevLedger = *newLedger;
        }
        else  // Unable to acquire the correct ledger
        {
            startMode = ConsensusMode::wrongLedger;
            JLOG(j_.info())
                << "Entering consensus with: " << previousLedger_.id();
            JLOG(j_.info()) << "Correct LCL is: " << prevLedgerID;
        }
    }

    startRoundInternal(now, prevLedgerID, prevLedger, startMode);
}
template <class Adaptor>
void
Consensus<Adaptor>::startRoundInternal(
    NetClock::time_point const& now,
    typename Ledger_t::ID const& prevLedgerID,
    Ledger_t const& prevLedger,
    ConsensusMode mode)
{
    phase_ = ConsensusPhase::open;
    mode_.set(mode, adaptor_);
    now_ = now;
    prevLedgerID_ = prevLedgerID;
    previousLedger_ = prevLedger;
    result_.reset();
    convergePercent_ = 0;
    haveCloseTimeConsensus_ = false;
    openTime_.reset(clock_.now());
    currPeerPositions_.clear();
    acquired_.clear();
    rawCloseTimes_.peers.clear();
    rawCloseTimes_.self = {};
    deadNodes_.clear();

    closeResolution_ = getNextLedgerTimeResolution(
        previousLedger_.closeTimeResolution(),
        previousLedger_.closeAgree(),
        previousLedger_.seq() + typename Ledger_t::Seq{1});

    playbackProposals();
    if (currPeerPositions_.size() > (prevProposers_ / 2))
    {
        // We may be falling behind, don't wait for the timer
        // consider closing the ledger immediately
        timerEntry(now_);
    }
}

template <class Adaptor>
bool
Consensus<Adaptor>::peerProposal(
    NetClock::time_point const& now,
    PeerPosition_t const& newPeerPos)
{
    NodeID_t const& peerID = newPeerPos.proposal().nodeID();

    // Always need to store recent positions
    {
        auto& props = recentPeerPositions_[peerID];

        if (props.size() >= 10)
            props.pop_front();

        props.push_back(newPeerPos);
    }
    return peerProposalInternal(now, newPeerPos);
}

template <class Adaptor>
bool
Consensus<Adaptor>::peerProposalInternal(
    NetClock::time_point const& now,
    PeerPosition_t const& newPeerPos)
{
    // Nothing to do for now if we are currently working on a ledger
    if (phase_ == ConsensusPhase::accepted)
        return false;

    now_ = now;

    Proposal_t const& newPeerProp = newPeerPos.proposal();

    NodeID_t const& peerID = newPeerProp.nodeID();

    if (newPeerProp.prevLedger() != prevLedgerID_)
    {
        JLOG(j_.debug()) << "Got proposal for " << newPeerProp.prevLedger()
                         << " but we are on " << prevLedgerID_;
        return false;
    }

    if (deadNodes_.find(peerID) != deadNodes_.end())
    {
        using std::to_string;
        JLOG(j_.info()) << "Position from dead node: " << to_string(peerID);
        return false;
    }

    {
        // update current position
        auto peerPosIt = currPeerPositions_.find(peerID);

        if (peerPosIt != currPeerPositions_.end())
        {
            if (newPeerProp.proposeSeq() <=
                peerPosIt->second.proposal().proposeSeq())
            {
                return false;
            }
        }

        if (newPeerProp.isBowOut())
        {
            using std::to_string;

            JLOG(j_.info()) << "Peer bows out: " << to_string(peerID);
            if (result_)
            {
                for (auto& it : result_->disputes)
                    it.second.unVote(peerID);
            }
            if (peerPosIt != currPeerPositions_.end())
                currPeerPositions_.erase(peerID);
            deadNodes_.insert(peerID);

            return true;
        }

        if (peerPosIt != currPeerPositions_.end())
            peerPosIt->second = newPeerPos;
        else
            currPeerPositions_.emplace(peerID, newPeerPos);
    }

    if (newPeerProp.isInitial())
    {
        // Record the close time estimate
        JLOG(j_.trace()) << "Peer reports close time as "
                         << newPeerProp.closeTime().time_since_epoch().count();
        ++rawCloseTimes_.peers[newPeerProp.closeTime()];
    }

    JLOG(j_.trace()) << "Processing peer proposal " << newPeerProp.proposeSeq()
                     << "/" << newPeerProp.position();

    {
        auto const ait = acquired_.find(newPeerProp.position());
        if (ait == acquired_.end())
        {
            // acquireTxSet will return the set if it is available, or
            // spawn a request for it and return none/nullptr.  It will call
            // gotTxSet once it arrives
            if (auto set = adaptor_.acquireTxSet(newPeerProp.position()))
                gotTxSet(now_, *set);
            else
                JLOG(j_.debug()) << "Don't have tx set for peer";
        }
        else if (result_)
        {
            updateDisputes(newPeerProp.nodeID(), ait->second);
        }
    }

    return true;
}

template <class Adaptor>
void
Consensus<Adaptor>::timerEntry(NetClock::time_point const& now)
{
    // Nothing to do if we are currently working on a ledger
    if (phase_ == ConsensusPhase::accepted)
        return;

    now_ = now;

    // Check we are on the proper ledger (this may change phase_)
    checkLedger();

    if (phase_ == ConsensusPhase::open)
    {
        phaseOpen();
    }
    else if (phase_ == ConsensusPhase::establish)
    {
        phaseEstablish();
    }
}

template <class Adaptor>
void
Consensus<Adaptor>::gotTxSet(
    NetClock::time_point const& now,
    TxSet_t const& txSet)
{
    // Nothing to do if we've finished work on a ledger
    if (phase_ == ConsensusPhase::accepted)
        return;

    now_ = now;

    auto id = txSet.id();

    // If we've already processed this transaction set since requesting
    // it from the network, there is nothing to do now
    if (!acquired_.emplace(id, txSet).second)
        return;

    if (!result_)
    {
        JLOG(j_.debug()) << "Not creating disputes: no position yet.";
    }
    else
    {
        // Our position is added to acquired_ as soon as we create it,
        // so this txSet must differ
        assert(id != result_->position.position());
        bool any = false;
        for (auto const& [nodeId, peerPos] : currPeerPositions_)
        {
            if (peerPos.proposal().position() == id)
            {
                updateDisputes(nodeId, txSet);
                any = true;
            }
        }

        if (!any)
        {
            JLOG(j_.warn())
                << "By the time we got " << id << " no peers were proposing it";
        }
    }
}

template <class Adaptor>
void
Consensus<Adaptor>::simulate(
    NetClock::time_point const& now,
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    using namespace std::chrono_literals;
    JLOG(j_.info()) << "Simulating consensus";
    now_ = now;
    closeLedger();
    result_->roundTime.tick(consensusDelay.value_or(100ms));
    result_->proposers = prevProposers_ = currPeerPositions_.size();
    prevRoundTime_ = result_->roundTime.read();
    phase_ = ConsensusPhase::accepted;
    adaptor_.onForceAccept(
        *result_,
        previousLedger_,
        closeResolution_,
        rawCloseTimes_,
        mode_.get(),
        getJson(true));
    JLOG(j_.info()) << "Simulation complete";
}

template <class Adaptor>
Json::Value
Consensus<Adaptor>::getJson(bool full) const
{
    using std::to_string;
    using Int = Json::Value::Int;

    Json::Value ret(Json::objectValue);

    ret["proposing"] = (mode_.get() == ConsensusMode::proposing);
    ret["proposers"] = static_cast<int>(currPeerPositions_.size());

    if (mode_.get() != ConsensusMode::wrongLedger)
    {
        ret["synched"] = true;
        ret["ledger_seq"] = static_cast<std::uint32_t>(previousLedger_.seq())+ 1;
        ret["close_granularity"] = static_cast<Int>(closeResolution_.count());
    }
    else
        ret["synched"] = false;

    ret["phase"] = to_string(phase_);

    if (result_ && !result_->disputes.empty() && !full)
        ret["disputes"] = static_cast<Int>(result_->disputes.size());

    if (result_)
        ret["our_position"] = result_->position.getJson();

    if (full)
    {
        if (result_)
            ret["current_ms"] =
                static_cast<Int>(result_->roundTime.read().count());
        ret["converge_percent"] = convergePercent_;
        ret["close_resolution"] = static_cast<Int>(closeResolution_.count());
        ret["have_time_consensus"] = haveCloseTimeConsensus_;
        ret["previous_proposers"] = static_cast<Int>(prevProposers_);
        ret["previous_mseconds"] = static_cast<Int>(prevRoundTime_.count());

        if (!currPeerPositions_.empty())
        {
            Json::Value ppj(Json::objectValue);

            for (auto const& [nodeId, peerPos] : currPeerPositions_)
            {
                ppj[to_string(nodeId)] = peerPos.getJson();
            }
            ret["peer_positions"] = std::move(ppj);
        }

        if (!acquired_.empty())
        {
            Json::Value acq(Json::arrayValue);
            for (auto const& at : acquired_)
            {
                acq.append(to_string(at.first));
            }
            ret["acquired"] = std::move(acq);
        }

        if (result_ && !result_->disputes.empty())
        {
            Json::Value dsj(Json::objectValue);
            for (auto const& [txId, dispute] : result_->disputes)
            {
                dsj[to_string(txId)] = dispute.getJson();
            }
            ret["disputes"] = std::move(dsj);
        }

        if (!rawCloseTimes_.peers.empty())
        {
            Json::Value ctj(Json::objectValue);
            for (auto const& ct : rawCloseTimes_.peers)
            {
                ctj[std::to_string(ct.first.time_since_epoch().count())] =
                    ct.second;
            }
            ret["close_times"] = std::move(ctj);
        }

        if (!deadNodes_.empty())
        {
            Json::Value dnj(Json::arrayValue);
            for (auto const& dn : deadNodes_)
            {
                dnj.append(to_string(dn));
            }
            ret["dead_nodes"] = std::move(dnj);
        }
    }

    return ret;
}

// Handle a change in the prior ledger during a consensus round
template <class Adaptor>
void
Consensus<Adaptor>::handleWrongLedger(typename Ledger_t::ID const& lgrId)
{
    assert(lgrId != prevLedgerID_ || previousLedger_.id() != lgrId);

    // Stop proposing because we are out of sync
    leaveConsensus();

    // First time switching to this ledger
    if (prevLedgerID_ != lgrId)
    {
        prevLedgerID_ = lgrId;

        // Clear out state
        if (result_)
        {
            result_->disputes.clear();
            result_->compares.clear();
        }

        currPeerPositions_.clear();
        rawCloseTimes_.peers.clear();
        deadNodes_.clear();

        // Get back in sync, this will also recreate disputes
        playbackProposals();
    }

    if (previousLedger_.id() == prevLedgerID_)
        return;

    // we need to switch the ledger we're working from
    if (auto newLedger = adaptor_.acquireLedger(prevLedgerID_))
    {
        JLOG(j_.info()) << "Have the consensus ledger " << prevLedgerID_;
        startRoundInternal(
            now_, lgrId, *newLedger, ConsensusMode::switchedLedger);
    }
    else
    {
        mode_.set(ConsensusMode::wrongLedger, adaptor_);
    }
}

template <class Adaptor>
void
Consensus<Adaptor>::checkLedger()
{
    auto netLgr =
        adaptor_.getPrevLedger(prevLedgerID_, previousLedger_, mode_.get());

    if (netLgr != prevLedgerID_)
    {
        JLOG(j_.warn()) << "View of consensus changed during "
                        << to_string(phase_) << " status=" << to_string(phase_)
                        << ", "
                        << " mode=" << to_string(mode_.get());
        JLOG(j_.warn()) << prevLedgerID_ << " to " << netLgr;
        JLOG(j_.warn()) << Json::Compact{previousLedger_.getJson()};
        JLOG(j_.debug()) << "State on consensus change "
                         << Json::Compact{getJson(true)};
        handleWrongLedger(netLgr);
    }
    else if (previousLedger_.id() != prevLedgerID_)
        handleWrongLedger(netLgr);
}

template <class Adaptor>
void
Consensus<Adaptor>::playbackProposals()
{
    for (auto const& it : recentPeerPositions_)
    {
        for (auto const& pos : it.second)
        {
            if (pos.proposal().prevLedger() == prevLedgerID_)
            {
                if (peerProposalInternal(now_, pos))
                    adaptor_.share(pos);
            }
        }
    }
}

template <class Adaptor>
void
Consensus<Adaptor>::phaseOpen()
{
    using namespace std::chrono;

    // it is shortly before ledger close time
    bool anyTransactions = adaptor_.hasOpenTransactions();
    auto proposersClosed = currPeerPositions_.size();
    auto proposersValidated = adaptor_.proposersValidated(prevLedgerID_);

    openTime_.tick(clock_.now());

    // This computes how long since last ledger's close time
    milliseconds sinceClose;
    {
        bool previousCloseCorrect =
            (mode_.get() != ConsensusMode::wrongLedger) &&
            previousLedger_.closeAgree() &&
            (previousLedger_.closeTime() !=
             (previousLedger_.parentCloseTime() + 1s));

        auto lastCloseTime = previousCloseCorrect
            ? previousLedger_.closeTime()  // use consensus timing
            : prevCloseTime_;              // use the time we saw internally

        if (now_ >= lastCloseTime)
            sinceClose = duration_cast<milliseconds>(now_ - lastCloseTime);
        else
            sinceClose = -duration_cast<milliseconds>(lastCloseTime - now_);
    }

    auto const idleInterval = std::max<milliseconds>(
        adaptor_.parms().ledgerIDLE_INTERVAL,
        2 * previousLedger_.closeTimeResolution());

    // Decide if we should close the ledger
    if (shouldCloseLedger(
            anyTransactions,
            prevProposers_,
            proposersClosed,
            proposersValidated,
            prevRoundTime_,
            sinceClose,
            openTime_.read(),
            idleInterval,
            adaptor_.parms(),
            j_))
    {
        closeLedger();
    }
}

template <class Adaptor>
bool
Consensus<Adaptor>::shouldPause() const
{
    auto const& parms = adaptor_.parms();
    std::uint32_t const ahead (previousLedger_.seq() -
        std::min(adaptor_.getValidLedgerIndex(), previousLedger_.seq()));
    auto [quorum, trustedKeys] = adaptor_.getQuorumKeys();
    std::size_t const totalValidators = trustedKeys.size();
    std::size_t laggards = adaptor_.laggards(previousLedger_.seq(),
        trustedKeys);
    std::size_t const offline = trustedKeys.size();

    std::stringstream vars;
    vars << " (working seq: " << previousLedger_.seq() << ", "
         << "validated seq: " << adaptor_.getValidLedgerIndex() << ", "
         << "am validator: " << adaptor_.validator() << ", "
         << "have validated: " << adaptor_.haveValidated() << ", "
         << "roundTime: " << result_->roundTime.read().count() << ", "
         << "max consensus time: " << parms.ledgerMAX_CONSENSUS.count() << ", "
         << "validators: " << totalValidators << ", "
         << "laggards: " << laggards << ", "
         << "offline: " << offline << ", "
         << "quorum: " << quorum << ")";

    if (!ahead ||
        !laggards ||
        !totalValidators ||
        !adaptor_.validator() ||
        !adaptor_.haveValidated() ||
        result_->roundTime.read() > parms.ledgerMAX_CONSENSUS)
    {
        j_.debug() << "not pausing" << vars.str();
        return false;
    }

    bool willPause = false;

    /** Maximum phase with distinct thresholds to determine how
     *  many validators must be on our same ledger sequence number.
     *  The threshold for the 1st (0) phase is >= the minimum number that
     *  can achieve quorum. Threshold for the maximum phase is 100%
     *  of all trusted validators. Progression from min to max phase is
     *  simply linear. If there are 5 phases (maxPausePhase = 4)
     *  and minimum quorum is 80%, then thresholds progress as follows:
     *  0: >=80%
     *  1: >=85%
     *  2: >=90%
     *  3: >=95%
     *  4: =100%
     */
    constexpr static std::size_t maxPausePhase = 4;

    /**
     * No particular threshold guarantees consensus. Lower thresholds
     * are easier to achieve than higher, but higher thresholds are
     * more likely to reach consensus. Cycle through the phases if
     * lack of synchronization continues.
     *
     * Current information indicates that no phase is likely to be intrinsically
     * better than any other: the lower the threshold, the less likely that
     * up-to-date nodes will be able to reach consensus without the laggards.
     * But the higher the threshold, the longer the likely resulting pause.
     * 100% is slightly less desirable in the long run because the potential
     * of a single dawdling peer to slow down everything else. So if we
     * accept that no phase is any better than any other phase, but that
     * all of them will potentially enable us to arrive at consensus, cycling
     * through them seems to be appropriate. Further, if we do reach the
     * point of having to cycle back around, then it's likely that something
     * else out of the scope of this delay mechanism is wrong with the
     * network.
     */
    std::size_t const phase = (ahead - 1) % (maxPausePhase + 1);

    // validators that remain after the laggards() function are considered
    // offline, and should be considered as laggards for purposes of
    // evaluating whether the threshold for non-laggards has been reached.
    switch (phase)
    {
        case 0:
            // Laggards and offline shouldn't preclude consensus.
            if (laggards + offline > totalValidators - quorum)
                willPause = true;
            break;
        case maxPausePhase:
            // No tolerance.
            willPause = true;
            break;
        default:
            // Ensure that sufficient validators are known to be not lagging.
            // Their sufficiently most recent validation sequence was equal to
            // or greater than our own.
            //
            // The threshold is the amount required for quorum plus
            // the proportion of the remainder based on number of intermediate
            // phases between 0 and max.
            float const nonLaggards = totalValidators - (laggards + offline);
            float const quorumRatio =
                static_cast<float>(quorum) / totalValidators;
            float const allowedDissent = 1.0f - quorumRatio;
            float const phaseFactor = static_cast<float>(phase) / maxPausePhase;

            if (nonLaggards / totalValidators <
                quorumRatio + (allowedDissent * phaseFactor))
            {
                willPause = true;
            }
    }

    if (willPause)
        j_.warn() << "pausing" << vars.str();
    else
        j_.debug() << "not pausing" << vars.str();
    return willPause;
}

template <class Adaptor>
void
Consensus<Adaptor>::phaseEstablish()
{
    // can only establish consensus if we already took a stance
    assert(result_);

    using namespace std::chrono;
    ConsensusParms const & parms = adaptor_.parms();

    result_->roundTime.tick(clock_.now());
    result_->proposers = currPeerPositions_.size();

    convergePercent_ = result_->roundTime.read() * 100 /
        std::max<milliseconds>(prevRoundTime_, parms.avMIN_CONSENSUS_TIME);

    // Give everyone a chance to take an initial position
    if (result_->roundTime.read() < parms.ledgerMIN_CONSENSUS)
        return;

    updateOurPositions();

    // Nothing to do if too many laggards or we don't have consensus.
    if (shouldPause() || !haveConsensus())
        return;

    if (!haveCloseTimeConsensus_)
    {
        JLOG(j_.info()) << "We have TX consensus but not CT consensus";
        return;
    }

    JLOG(j_.info()) << "Converge cutoff (" << currPeerPositions_.size()
                    << " participants)";
    adaptor_.updateOperatingMode(currPeerPositions_.size());
    prevProposers_ = currPeerPositions_.size();
    prevRoundTime_ = result_->roundTime.read();
    phase_ = ConsensusPhase::accepted;
    adaptor_.onAccept(
        *result_,
        previousLedger_,
        closeResolution_,
        rawCloseTimes_,
        mode_.get(),
        getJson(true));
}

template <class Adaptor>
void
Consensus<Adaptor>::closeLedger()
{
    // We should not be closing if we already have a position
    assert(!result_);

    phase_ = ConsensusPhase::establish;
    rawCloseTimes_.self = now_;

    result_.emplace(adaptor_.onClose(previousLedger_, now_, mode_.get()));
    result_->roundTime.reset(clock_.now());
    // Share the newly created transaction set if we haven't already
    // received it from a peer
    if (acquired_.emplace(result_->txns.id(), result_->txns).second)
        adaptor_.share(result_->txns);

    if (mode_.get() == ConsensusMode::proposing)
        adaptor_.propose(result_->position);

    // Create disputes with any peer positions we have transactions for
    for (auto const& pit : currPeerPositions_)
    {
        auto const& pos = pit.second.proposal().position();
        auto const it = acquired_.find(pos);
        if (it != acquired_.end())
        {
            createDisputes(it->second);
        }
    }
}

/** How many of the participants must agree to reach a given threshold?

Note that the number may not precisely yield the requested percentage.
For example, with with size = 5 and percent = 70, we return 3, but
3 out of 5 works out to 60%. There are no security implications to
this.

@param participants The number of participants (i.e. validators)
@param percent The percent that we want to reach

@return the number of participants which must agree
*/
inline int
participantsNeeded(int participants, int percent)
{
    int result = ((participants * percent) + (percent / 2)) / 100;

    return (result == 0) ? 1 : result;
}

template <class Adaptor>
void
Consensus<Adaptor>::updateOurPositions()
{
    // We must have a position if we are updating it
    assert(result_);
    ConsensusParms const & parms = adaptor_.parms();

    // Compute a cutoff time
    auto const peerCutoff = now_ - parms.proposeFRESHNESS;
    auto const ourCutoff = now_ - parms.proposeINTERVAL;

    // Verify freshness of peer positions and compute close times
    std::map<NetClock::time_point, int> closeTimeVotes;
    {
        auto it = currPeerPositions_.begin();
        while (it != currPeerPositions_.end())
        {
            Proposal_t const& peerProp = it->second.proposal();
            if (peerProp.isStale(peerCutoff))
            {
                // peer's proposal is stale, so remove it
                NodeID_t const& peerID = peerProp.nodeID();
                JLOG(j_.warn()) << "Removing stale proposal from " << peerID;
                for (auto& dt : result_->disputes)
                    dt.second.unVote(peerID);
                it = currPeerPositions_.erase(it);
            }
            else
            {
                // proposal is still fresh
                ++closeTimeVotes[asCloseTime(peerProp.closeTime())];
                ++it;
            }
        }
    }

    // This will stay unseated unless there are any changes
    boost::optional<TxSet_t> ourNewSet;

    // Update votes on disputed transactions
    {
        boost::optional<typename TxSet_t::MutableTxSet> mutableSet;
        for (auto& [txId, dispute] : result_->disputes)
        {
            // Because the threshold for inclusion increases,
            //  time can change our position on a dispute
            if (dispute.updateVote(
                    convergePercent_,
                    mode_.get()== ConsensusMode::proposing,
                    parms))
            {
                if (!mutableSet)
                    mutableSet.emplace(result_->txns);

                if (dispute.getOurVote())
                {
                    // now a yes
                    mutableSet->insert(dispute.tx());
                }
                else
                {
                    // now a no
                    mutableSet->erase(txId);
                }
            }
        }

        if (mutableSet)
            ourNewSet.emplace(std::move(*mutableSet));
    }

    NetClock::time_point consensusCloseTime = {};
    haveCloseTimeConsensus_ = false;

    if (currPeerPositions_.empty())
    {
        // no other times
        haveCloseTimeConsensus_ = true;
        consensusCloseTime = asCloseTime(result_->position.closeTime());
    }
    else
    {
        int neededWeight;

        if (convergePercent_ < parms.avMID_CONSENSUS_TIME)
            neededWeight = parms.avINIT_CONSENSUS_PCT;
        else if (convergePercent_ < parms.avLATE_CONSENSUS_TIME)
            neededWeight = parms.avMID_CONSENSUS_PCT;
        else if (convergePercent_ < parms.avSTUCK_CONSENSUS_TIME)
            neededWeight = parms.avLATE_CONSENSUS_PCT;
        else
            neededWeight = parms.avSTUCK_CONSENSUS_PCT;

        int participants = currPeerPositions_.size();
        if (mode_.get() == ConsensusMode::proposing)
        {
            ++closeTimeVotes[asCloseTime(result_->position.closeTime())];
            ++participants;
        }

        // Threshold for non-zero vote
        int threshVote = participantsNeeded(participants, neededWeight);

        // Threshold to declare consensus
        int const threshConsensus =
            participantsNeeded(participants, parms.avCT_CONSENSUS_PCT);

        JLOG(j_.info()) << "Proposers:" << currPeerPositions_.size()
                        << " nw:" << neededWeight << " thrV:" << threshVote
                        << " thrC:" << threshConsensus;

        for (auto const& [t, v] : closeTimeVotes)
        {
            JLOG(j_.debug())
                << "CCTime: seq "
                << static_cast<std::uint32_t>(previousLedger_.seq()) + 1 << ": "
                << t.time_since_epoch().count() << " has " << v
                << ", " << threshVote << " required";

            if (v >= threshVote)
            {
                // A close time has enough votes for us to try to agree
                consensusCloseTime = t;
                threshVote = v;

                if (threshVote >= threshConsensus)
                    haveCloseTimeConsensus_ = true;
            }
        }

        if (!haveCloseTimeConsensus_)
        {
            JLOG(j_.debug())
                << "No CT consensus:"
                << " Proposers:" << currPeerPositions_.size()
                << " Mode:" << to_string(mode_.get())
                << " Thresh:" << threshConsensus
                << " Pos:" << consensusCloseTime.time_since_epoch().count();
        }
    }

    if (!ourNewSet &&
        ((consensusCloseTime != asCloseTime(result_->position.closeTime())) ||
         result_->position.isStale(ourCutoff)))
    {
        // close time changed or our position is stale
        ourNewSet.emplace(result_->txns);
    }

    if (ourNewSet)
    {
        auto newID = ourNewSet->id();

        result_->txns = std::move(*ourNewSet);

        JLOG(j_.info()) << "Position change: CTime "
                        << consensusCloseTime.time_since_epoch().count()
                        << ", tx " << newID;

        result_->position.changePosition(newID, consensusCloseTime, now_);

        // Share our new transaction set and update disputes
        // if we haven't already received it
        if (acquired_.emplace(newID, result_->txns).second)
        {
            if (!result_->position.isBowOut())
                adaptor_.share(result_->txns);

            for (auto const& [nodeId, peerPos] : currPeerPositions_)
            {
                Proposal_t const& p = peerPos.proposal();
                if (p.position() == newID)
                    updateDisputes(nodeId, result_->txns);
            }
        }

        // Share our new position if we are still participating this round
        if (!result_->position.isBowOut() &&
            (mode_.get() == ConsensusMode::proposing))
            adaptor_.propose(result_->position);
    }
}

template <class Adaptor>
bool
Consensus<Adaptor>::haveConsensus()
{
    // Must have a stance if we are checking for consensus
    assert(result_);

    // CHECKME: should possibly count unacquired TX sets as disagreeing
    int agree = 0, disagree = 0;

    auto ourPosition = result_->position.position();

    // Count number of agreements/disagreements with our position
    for (auto const& [nodeId, peerPos] : currPeerPositions_)
    {
        Proposal_t const& peerProp = peerPos.proposal();
        if (peerProp.position() == ourPosition)
        {
            ++agree;
        }
        else
        {
            using std::to_string;

            JLOG(j_.debug()) << to_string(nodeId) << " has "
                             << to_string(peerProp.position());
            ++disagree;
        }
    }
    auto currentFinished =
        adaptor_.proposersFinished(previousLedger_, prevLedgerID_);

    JLOG(j_.debug()) << "Checking for TX consensus: agree=" << agree
                     << ", disagree=" << disagree;

    // Determine if we actually have consensus or not
    result_->state = checkConsensus(
        prevProposers_,
        agree + disagree,
        agree,
        currentFinished,
        prevRoundTime_,
        result_->roundTime.read(),
        adaptor_.parms(),
        mode_.get() == ConsensusMode::proposing,
        j_);

    if (result_->state == ConsensusState::No)
        return false;

    // There is consensus, but we need to track if the network moved on
    // without us.
    if (result_->state == ConsensusState::MovedOn)
    {
        JLOG(j_.error()) << "Unable to reach consensus";
        JLOG(j_.error()) << Json::Compact{getJson(true)};
    }

    return true;
}

template <class Adaptor>
void
Consensus<Adaptor>::leaveConsensus()
{
    if (mode_.get() == ConsensusMode::proposing)
    {
        if (result_ && !result_->position.isBowOut())
        {
            result_->position.bowOut(now_);
            adaptor_.propose(result_->position);
        }

        mode_.set(ConsensusMode::observing, adaptor_);
        JLOG(j_.info()) << "Bowing out of consensus";
    }
}

template <class Adaptor>
void
Consensus<Adaptor>::createDisputes(TxSet_t const& o)
{
    // Cannot create disputes without our stance
    assert(result_);

    // Only create disputes if this is a new set
    if (!result_->compares.emplace(o.id()).second)
        return;

    // Nothing to dispute if we agree
    if (result_->txns.id() == o.id())
        return;

    JLOG(j_.debug()) << "createDisputes " << result_->txns.id() << " to "
                     << o.id();

    auto differences = result_->txns.compare(o);

    int dc = 0;

    for (auto const& [txId, inThisSet] : differences)
    {
        ++dc;
        // create disputed transactions (from the ledger that has them)
        assert(
            (inThisSet && result_->txns.find(txId) && !o.find(txId)) ||
            (!inThisSet && !result_->txns.find(txId) && o.find(txId)));

        Tx_t tx = inThisSet ? *result_->txns.find(txId) : *o.find(txId);
        auto txID = tx.id();

        if (result_->disputes.find(txID) != result_->disputes.end())
            continue;

        JLOG(j_.debug()) << "Transaction " << txID << " is disputed";

        typename Result::Dispute_t dtx{tx, result_->txns.exists(txID),
         std::max(prevProposers_, currPeerPositions_.size()), j_};

        // Update all of the available peer's votes on the disputed transaction
        for (auto const& [nodeId, peerPos] : currPeerPositions_)
        {
            Proposal_t const& peerProp = peerPos.proposal();
            auto const cit = acquired_.find(peerProp.position());
            if (cit != acquired_.end())
                dtx.setVote(nodeId, cit->second.exists(txID));
        }
        adaptor_.share(dtx.tx());

        result_->disputes.emplace(txID, std::move(dtx));
    }
    JLOG(j_.debug()) << dc << " differences found";
}

template <class Adaptor>
void
Consensus<Adaptor>::updateDisputes(NodeID_t const& node, TxSet_t const& other)
{
    // Cannot updateDisputes without our stance
    assert(result_);

    // Ensure we have created disputes against this set if we haven't seen
    // it before
    if (result_->compares.find(other.id()) == result_->compares.end())
        createDisputes(other);

    for (auto& it : result_->disputes)
    {
        auto& d = it.second;
        d.setVote(node, other.exists(d.tx().id()));
    }
}

template <class Adaptor>
NetClock::time_point
Consensus<Adaptor>::asCloseTime(NetClock::time_point raw) const
{
    if (adaptor_.parms().useRoundedCloseTime)
        return roundCloseTime(raw, closeResolution_);
    else
        return effCloseTime(raw, closeResolution_, previousLedger_.closeTime());
}

}  // namespace ripple

#endif

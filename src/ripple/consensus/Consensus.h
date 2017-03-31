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
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/consensus/DisputedTx.h>
#include <ripple/json/json_writer.h>

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


/** Whether we have or don't have a consensus */
enum class ConsensusState {
    No,       //!< We do not have consensus
    MovedOn,  //!< The network has consensus without us
    Yes       //!< We have consensus along with the network
};

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
       phase, the node is waiting for transactions to include in its open ledger.
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

  This class uses CRTP to allow adapting Consensus for specific applications.

  The Derived template argument is used to embed consensus within the
  larger application framework. The Traits template identifies types that
  play important roles in Consensus (transactions, ledgers, etc.) and which must
  conform to the generic interface outlined below. The Traits types must be copy
  constructible and assignable.

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

    // Unique identifier of ledgerr
    ID const id() const;
    auto seq() const;
    auto closeTimeResolution() const;
    auto closeAgree() const;
    auto closeTime() const;
    auto parentCloseTime() const;
    Json::Value getJson() const;
  };

  struct Traits
  {
    using Ledger_t = Ledger;
    using NodeID_t = std::uint32_t;
    using TxSet_t = TxSet;
  }

  class ConsensusImp : public Consensus<ConsensusImp, Traits>
  {
      // Attempt to acquire a specific ledger.
      boost::optional<Ledger> acquireLedger(Ledger::ID const & ledgerID);

      // Acquire the transaction set associated with a proposed position.
      boost::optional<TxSet> acquireTxSet(TxSet::ID const & setID);

      // Get peers' proposed positions. Returns an iterable
      // with value_type convertable to ConsensusPosition<...>
      auto const & proposals(Ledger::ID const & ledgerID);

      // Whether any transactions are in the open ledger
      bool hasOpenTransactions() const;

      // Number of proposers that have validated the given ledger
      std::size_t proposersValidated(Ledger::ID const & prevLedger) const;

      // Number of proposers that have validated a ledger descended from the
      // given ledger
      std::size_t proposersFinished(Ledger::ID const & prevLedger) const;

      // Return the ID of the last closed (and validated) ledger that the
      // application thinks consensus should use as the prior ledger.
      Ledger::ID getPrevLedger(Ledger::ID const & prevLedgerID,
                      Ledger const & prevLedger,
                      Mode mode);


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

      // Relay a received peer proposal on to other peer's.
      // The argument type should be convertible to ConsensusProposal<...>
      // but may be a different type.
      void relay(implementation_defined const & pos);

      // Relay a disputed transaction to peers
      void relay(Txn const & tx);

      // Share given transaction set with peers
      void relay(TxSet const &s);

  };
  @endcode

  @tparam Derived The deriving class which adapts the Consensus algorithm.
  @tparam Traits Provides definitions of types used in Consensus.
*/
template <class Derived, class Traits>
class Consensus
{
    //! Phases of consensensus:
    //!        "close"             "accept"
    //!   open ------- > establish ---------> accepted
    //!     ^               |                    |
    //!     |---------------|                    |
    //!     ^                     "startRound"   |
    //!     |------------------------------------|
    //!
    //! The typical transition goes from open to establish to accepted and
    //! then a call to startRound begins the process anew.
    //! However, if a wrong prior ledger is detected and recovered
    //! during the establish or accept phase, consensus will internally go back
    //! to open (see handleWrongLedger).
    enum class Phase {
        //! We haven't closed our ledger yet, but others might have
        open,

        //! Establishing consensus by exchanging proposals with our peers
        establish,

        //! We have accepted a new last closed ledger and are waiting on a call
        //! to startRound to begin the next consensus round.  No changes
        //! to consensus phase occur while in this phase.
        accepted,
    };

    using Ledger_t = typename Traits::Ledger_t;
    using TxSet_t = typename Traits::TxSet_t;
    using NodeID_t = typename Traits::NodeID_t;
    using Tx_t = typename TxSet_t::Tx;
    using Proposal_t = ConsensusProposal<
        NodeID_t,
        typename Ledger_t::ID,
        typename TxSet_t::ID>;

protected:
    //! How we participating in Consensus
    //!    proposing               observing
    //!       \                       /
    //|        \---> wrongLedger <---/
    //!                   ^
    //!                   |
    //!                   |
    //!                   v
    //!              switchedLedger
    //! We enter the round proposing or observing. If we detect we
    //! are working on the wrong prior ledger, we go to
    //! wrongLedger and attempt to acquire the right one (ref
    //! handleWrongLedger). Once we acquire the right one, we go to
    //! switchedLedger.  If we again detect the wrongLedger before this round
    //! ends, we go back to wrongLedger until we acquire the right one.
    enum class Mode {
        //! We a normal participant in consensus and propose our position
        proposing,
        //! We are observing peer positions, but not proposing our position
        observing,
        //! We have the wrong ledger and are attempting to acquire it
        wrongLedger,
        //! We switched ledger since we started this consensus round but are now
        //! running on what we believe is the correct ledger.  This mode is as
        //! if we entered the round observing, but is used to indicate we did
        //! have the wrongLedger at some point.
        switchedLedger
    };

    //! Measure duration of phases of consensus
    class Stopwatch
    {
        using time_point = std::chrono::steady_clock::time_point;
        time_point start_;
        std::chrono::milliseconds dur_;

    public:
        std::chrono::milliseconds
        read() const
        {
            return dur_;
        }

        void
        tick(std::chrono::milliseconds fixed)
        {
            dur_ += fixed;
        }

        void
        reset(time_point tp)
        {
            start_ = tp;
            dur_ = std::chrono::milliseconds{0};
        }

        void
        tick(time_point tp)
        {
            using namespace std::chrono;
            dur_ = duration_cast<milliseconds>(tp - start_);
        }
    };

    //! Initial ledger close times, not rounded by closeTimeResolution
    struct CloseTimes
    {
        // Close time estimates, keep ordered for predictable traverse
        std::map<NetClock::time_point, int> peers;
        NetClock::time_point self;
    };

    /** Enacpsulates the result of consensus.

        While in the establish phase, represents our work in progress consensus
        result.  In the accept phase, represents our final consensus result
        for this round.
    */
    struct Result
    {
        using Dispute_t = DisputedTx<Tx_t, NodeID_t>;

        Result(TxSet_t&& s, Proposal_t&& p)
            : set{std::move(s)}, position{std::move(p)}
        {
            assert(set.id() == position.position());
        }

        //! The set of transactions consensus agrees go in the ledger
        TxSet_t set;

        //! Our proposed position on transactions/close time
        Proposal_t position;

        //! Transactions which are under dispute with our peers
        hash_map<typename Tx_t::ID, Dispute_t> disputes;

        // Set of TxSet ids we have already compared/created disputes
        hash_set<typename TxSet_t::ID> compares;

        // Measures the duration of the establish phase for this consensus round
        Stopwatch roundTime;

        // Indicates state in which consensus ended.  Once in the accept phase
        // will be either Yes or MovedOn
        ConsensusState state = ConsensusState::No;
    };

public:
    //! Clock type for measuring time within the consensus code
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

    Consensus(Consensus&&) = default;

    /** Constructor.

        @param clock The clock used to internally sample consensus progress
        @param p Consensus parameters to use
        @param j The journal to log debug output
    */
    Consensus(clock_type const& clock, ConsensusParms const & p, beast::Journal j);

    /** Kick-off the next round of consensus.

        Called by the client code to start each round of consensus.

        @param now The network adjusted time
        @param prevLedgerID the ID of the last ledger
        @param prevLedger The last ledger
        @param proposing Whether we want to send proposals to peers this round.

        @note @b prevLedgerID is not required to the ID of @b prevLedger since
        the ID may be known locally before the contents of the ledger arrive
    */
    void
    startRound(
        NetClock::time_point const& now,
        typename Ledger_t::ID const& prevLedgerID,
        Ledger_t prevLedger,
        bool proposing);

    /** A peer has proposed a new position, adjust our tracking.

        @param now The network adjusted time
        @param newProposal The new proposal from a peer
        @return Whether we should do delayed relay of this proposal.
    */
    bool
    peerProposal(
        NetClock::time_point const& now,
        Proposal_t const& newProposal);

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
        std::lock_guard<std::recursive_mutex> _(*lock_);
        return prevLedgerID_;
    }

    //! Get the number of proposing peers that participated in the previous
    //! round.
    std::size_t
    prevProposers() const
    {
        return prevProposers_;
    }

    /** Get duration of the previous round.

        The duration of the round is the establish phase, measured from closing
        the open ledger to accepting the consensus result.

        @return Last round duration in milliseconds
    */
    std::chrono::milliseconds
    prevRoundTime() const
    {
        return prevRoundTime_;
    }

    /** Get the current consensus mode.
     */
    Mode
    mode() const
    {
        return mode_;
    }

    /** Get the Json state of the consensus process.

        Called by the consensus_info RPC.

        @param full True if verbose response desired.
        @return     The Json state.
    */
    Json::Value
    getJson(bool full) const;

    /** Get the consensus parameters
    */
    ConsensusParms const &
    parms() const
    {
        return parms_;
    }
protected:

    // Prevent deleting derived instance through base pointer
    ~Consensus() = default;

    /** Revoke our outstanding proposal, if any, and cease proposing
        until this round ends.
    */
    void
    leaveConsensus();

private:
    void
    startRoundInternal(
        NetClock::time_point const& now,
        typename Ledger_t::ID const& prevLedgerID,
        Ledger_t const& prevLedger,
        Mode mode);

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

    Derived&
    impl()
    {
        return *static_cast<Derived*>(this);
    }

    static std::string
    to_string(Phase p);

    static std::string
    to_string(Mode m);

private:
    // TODO: Move this to clients
    std::unique_ptr<std::recursive_mutex> lock_;

    Phase phase_ = Phase::accepted;
    Mode mode_ = Mode::observing;
    bool firstRound_ = true;
    bool haveCloseTimeConsensus_ = false;

    clock_type const& clock_;

    // How long the consensus convergence has taken, expressed as
    // a percentage of the time that we expected it to take.
    int convergePercent_{0};

    // How long has this round been open
    Stopwatch openTime_;

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
    CloseTimes rawCloseTimes_;
    //-------------------------------------------------------------------------
    // Peer related consensus data
    // Convergence tracking, trusted peers indexed by hash of public key
    hash_map<NodeID_t, Proposal_t> peerProposals_;

    // The number of proposers who participated in the last consensus round
    std::size_t prevProposers_ = 0;

    // nodes that have bowed out of this consensus process
    hash_set<NodeID_t> deadNodes_;

    // Parameters that control consensus algorithm
    ConsensusParms const parms_;

    // Journal for debugging
    beast::Journal j_;
};

template <class Derived, class Traits>
Consensus<Derived, Traits>::Consensus(
    clock_type const& clock,
    ConsensusParms const & p,
    beast::Journal journal)
    : lock_{std::make_unique<std::recursive_mutex>()}
    , clock_{clock}
    , prevRoundTime_{p.ledgerIDLE_INTERVAL}
    , parms_(p)
    , j_{journal}
{
    JLOG(j_.debug()) << "Creating consensus object";
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::startRound(
    NetClock::time_point const& now,
    typename Ledger_t::ID const& prevLedgerID,
    Ledger_t prevLedger,
    bool proposing)
{
    std::lock_guard<std::recursive_mutex> _(*lock_);

    if (firstRound_)
    {
        // take our initial view of closeTime_ from the seed ledger
        prevCloseTime_ = prevLedger.closeTime();
        firstRound_ = false;
    }
    else
    {
        prevCloseTime_ = rawCloseTimes_.self;
    }

    Mode startMode = proposing ? Mode::proposing : Mode::observing;

    // We were handed the wrong ledger
    if (prevLedger.id() != prevLedgerID)
    {
        // try to acquire the correct one
        if(auto newLedger = impl().acquireLedger(prevLedgerID))
        {
            prevLedger = *newLedger;
        }
        else // Unable to acquire the correct ledger
        {
            startMode = Mode::wrongLedger;
            JLOG(j_.info())
                << "Entering consensus with: " << previousLedger_.id();
            JLOG(j_.info()) << "Correct LCL is: " << prevLedgerID;
        }
    }

    startRoundInternal(
        now,
        prevLedgerID,
        prevLedger,
        startMode);
}
template <class Derived, class Traits>
void
Consensus<Derived, Traits>::startRoundInternal(
    NetClock::time_point const& now,
    typename Ledger_t::ID const& prevLedgerID,
    Ledger_t const& prevLedger,
    Mode mode)
{
    phase_ = Phase::open;
    mode_ = mode;
    now_ = now;
    prevLedgerID_ = prevLedgerID;
    previousLedger_ = prevLedger;
    result_.reset();
    convergePercent_ = 0;
    haveCloseTimeConsensus_ = false;
    openTime_.reset(clock_.now());
    peerProposals_.clear();
    acquired_.clear();
    rawCloseTimes_.peers.clear();
    rawCloseTimes_.self = {};
    deadNodes_.clear();

    closeResolution_ = getNextLedgerTimeResolution(
        previousLedger_.closeTimeResolution(),
        previousLedger_.closeAgree(),
        previousLedger_.seq() + 1);

    playbackProposals();
    if (peerProposals_.size() > (prevProposers_ / 2))
    {
        // We may be falling behind, don't wait for the timer
        // consider closing the ledger immediately
        timerEntry(now_);
    }
}

template <class Derived, class Traits>
bool
Consensus<Derived, Traits>::peerProposal(
    NetClock::time_point const& now,
    Proposal_t const& newProposal)
{
    auto const peerID = newProposal.nodeID();

    std::lock_guard<std::recursive_mutex> _(*lock_);

    // Nothing to do if we are currently working on a ledger
    if (phase_ == Phase::accepted)
        return false;

    now_ = now;

    if (newProposal.prevLedger() != prevLedgerID_)
    {
        JLOG(j_.debug()) << "Got proposal for " << newProposal.prevLedger()
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
        auto currentPosition = peerProposals_.find(peerID);

        if (currentPosition != peerProposals_.end())
        {
            if (newProposal.proposeSeq() <=
                currentPosition->second.proposeSeq())
            {
                return false;
            }
        }

        if (newProposal.isBowOut())
        {
            using std::to_string;

            JLOG(j_.info()) << "Peer bows out: " << to_string(peerID);
            if (result_)
            {
                for (auto& it : result_->disputes)
                    it.second.unVote(peerID);
            }
            if (currentPosition != peerProposals_.end())
                peerProposals_.erase(peerID);
            deadNodes_.insert(peerID);

            return true;
        }

        if (currentPosition != peerProposals_.end())
            currentPosition->second = newProposal;
        else
            peerProposals_.emplace(peerID, newProposal);
    }

    if (newProposal.isInitial())
    {
        // Record the close time estimate
        JLOG(j_.trace()) << "Peer reports close time as "
                         << newProposal.closeTime().time_since_epoch().count();
        ++rawCloseTimes_.peers[newProposal.closeTime()];
    }

    JLOG(j_.trace()) << "Processing peer proposal " << newProposal.proposeSeq()
                     << "/" << newProposal.position();

    {
        auto ait = acquired_.find(newProposal.position());
        if (ait == acquired_.end())
        {
            // acquireTxSet will return the set if it is available, or
            // spawn a request for it and return none/nullptr.  It will call
            // gotTxSet once it arrives
            if (auto set = impl().acquireTxSet(newProposal.position()))
                gotTxSet(now_, *set);
            else
                JLOG(j_.debug()) << "Don't have tx set for peer";
        }
        else if (result_)
        {
            updateDisputes(newProposal.nodeID(), ait->second);
        }
    }

    return true;
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::timerEntry(NetClock::time_point const& now)
{
    std::lock_guard<std::recursive_mutex> _(*lock_);

    // Nothing to do if we are currently working on a ledger
    if (phase_ == Phase::accepted)
        return;

    now_ = now;

    // Check we are on the proper ledger (this may change phase_)
    checkLedger();

    if(phase_ == Phase::open)
    {
        phaseOpen();
    }
    else if (phase_ == Phase::establish)
    {
        phaseEstablish();
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::gotTxSet(
    NetClock::time_point const& now,
    TxSet_t const& txSet)
{
    std::lock_guard<std::recursive_mutex> _(*lock_);

    // Nothing to do if we've finished work on a ledger
    if (phase_ == Phase::accepted)
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
        for (auto const& p : peerProposals_)
        {
            if (p.second.position() == id)
            {
                updateDisputes(p.first, txSet);
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

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::simulate(
    NetClock::time_point const& now,
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    std::lock_guard<std::recursive_mutex> _(*lock_);

    JLOG(j_.info()) << "Simulating consensus";
    now_ = now;
    closeLedger();
    result_->roundTime.tick(consensusDelay.value_or(100ms));
    prevProposers_ = peerProposals_.size();
    prevRoundTime_ = result_->roundTime.read();
    phase_ = Phase::accepted;
    impl().onForceAccept(
        *result_, previousLedger_, closeResolution_, rawCloseTimes_, mode_);
    JLOG(j_.info()) << "Simulation complete";
}

template <class Derived, class Traits>
Json::Value
Consensus<Derived, Traits>::getJson(bool full) const
{
    using std::to_string;
    using Int = Json::Value::Int;

    Json::Value ret(Json::objectValue);
    std::lock_guard<std::recursive_mutex> _(*lock_);

    ret["proposing"] = (mode_ == Mode::proposing);
    ret["proposers"] = static_cast<int>(peerProposals_.size());

    if (mode_ != Mode::wrongLedger)
    {
        ret["synched"] = true;
        ret["ledger_seq"] = previousLedger_.seq() + 1;
        ret["close_granularity"] = static_cast<Int>(closeResolution_.count());
    }
    else
        ret["synched"] = false;

    ret["phase"] = Consensus::to_string(phase_);

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

        if (!peerProposals_.empty())
        {
            Json::Value ppj(Json::objectValue);

            for (auto& pp : peerProposals_)
            {
                ppj[to_string(pp.first)] = pp.second.getJson();
            }
            ret["peer_positions"] = std::move(ppj);
        }

        if (!acquired_.empty())
        {
            Json::Value acq(Json::arrayValue);
            for (auto& at : acquired_)
            {
                acq.append(to_string(at.first));
            }
            ret["acquired"] = std::move(acq);
        }

        if (result_ && !result_->disputes.empty())
        {
            Json::Value dsj(Json::objectValue);
            for (auto& dt : result_->disputes)
            {
                dsj[to_string(dt.first)] = dt.second.getJson();
            }
            ret["disputes"] = std::move(dsj);
        }

        if (!rawCloseTimes_.peers.empty())
        {
            Json::Value ctj(Json::objectValue);
            for (auto& ct : rawCloseTimes_.peers)
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
template <class Derived, class Traits>
void
Consensus<Derived, Traits>::handleWrongLedger(
    typename Ledger_t::ID const& lgrId)
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

        peerProposals_.clear();
        rawCloseTimes_.peers.clear();
        deadNodes_.clear();

        // Get back in sync, this will also recreate disputes
        playbackProposals();
    }

    if (previousLedger_.id() == prevLedgerID_)
        return;

    // we need to switch the ledger we're working from
    if (auto newLedger = impl().acquireLedger(prevLedgerID_))
    {
        JLOG(j_.info()) << "Have the consensus ledger " << prevLedgerID_;
        startRoundInternal(now_, lgrId, *newLedger, Mode::switchedLedger);
    }
    else
    {
        mode_ = Mode::wrongLedger;
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::checkLedger()
{
    auto netLgr = impl().getPrevLedger(prevLedgerID_, previousLedger_, mode_);

    if (netLgr != prevLedgerID_)
    {
        JLOG(j_.warn()) << "View of consensus changed during "
                        << to_string(phase_) << " status=" << to_string(phase_)
                        << ", "
                        << " mode=" << to_string(mode_);
        JLOG(j_.warn()) << prevLedgerID_ << " to " << netLgr;
        JLOG(j_.warn()) << previousLedger_.getJson();
        handleWrongLedger(netLgr);
    }
    else if (previousLedger_.id() != prevLedgerID_)
        handleWrongLedger(netLgr);
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::playbackProposals()
{
    for (auto const& p : impl().proposals(prevLedgerID_))
    {
        if (peerProposal(now_, p))
            impl().relay(p);
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::phaseOpen()
{
    using namespace std::chrono;

    // it is shortly before ledger close time
    bool anyTransactions = impl().hasOpenTransactions();
    auto proposersClosed = peerProposals_.size();
    auto proposersValidated = impl().proposersValidated(prevLedgerID_);

    openTime_.tick(clock_.now());

    // This computes how long since last ledger's close time
    milliseconds sinceClose;
    {
        bool previousCloseCorrect = (mode_ != Mode::wrongLedger) &&
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
        parms_.ledgerIDLE_INTERVAL,
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
            parms_,
            j_))
    {
        closeLedger();
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::phaseEstablish()
{
    // can only establish consensus if we already took a stance
    assert(result_);

    using namespace std::chrono;
    result_->roundTime.tick(clock_.now());

    convergePercent_ = result_->roundTime.read() * 100 /
        std::max<milliseconds>(prevRoundTime_, parms_.avMIN_CONSENSUS_TIME);

    // Give everyone a chance to take an initial position
    if (result_->roundTime.read() < parms_.ledgerMIN_CONSENSUS)
        return;

    updateOurPositions();

    // Nothing to do if we don't have consensus.
    if (!haveConsensus())
        return;

    if (!haveCloseTimeConsensus_)
    {
        JLOG(j_.info()) << "We have TX consensus but not CT consensus";
        return;
    }

    JLOG(j_.info()) << "Converge cutoff (" << peerProposals_.size()
                    << " participants)";
    prevProposers_ = peerProposals_.size();
    prevRoundTime_ = result_->roundTime.read();
    phase_ = Phase::accepted;
    impl().onAccept(
        *result_, previousLedger_, closeResolution_, rawCloseTimes_, mode_);
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::closeLedger()
{
    // We should not be closing if we already have a position
    assert(!result_);

    phase_ = Phase::establish;
    rawCloseTimes_.self = now_;

    result_.emplace(impl().onClose(previousLedger_, now_, mode_));
    result_->roundTime.reset(clock_.now());
    // Share the newly created transaction set if we haven't already
    // received it from a peer
    if (acquired_.emplace(result_->set.id(), result_->set).second)
        impl().relay(result_->set);

    if (mode_ == Mode::proposing)
        impl().propose(result_->position);

    // Create disputes with any peer positions we have transactions for
    for (auto const& p : peerProposals_)
    {
        auto pos = p.second.position();
        auto it = acquired_.find(pos);
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

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::updateOurPositions()
{
    // We must have a position if we are updating it
    assert(result_);

    // Compute a cutoff time
    auto const peerCutoff = now_ - parms_.proposeFRESHNESS;
    auto const ourCutoff = now_ - parms_.proposeINTERVAL;

    // Verify freshness of peer positions and compute close times
    std::map<NetClock::time_point, int> effCloseTimes;
    {
        auto it = peerProposals_.begin();
        while (it != peerProposals_.end())
        {
            if (it->second.isStale(peerCutoff))
            {
                // peer's proposal is stale, so remove it
                auto const& peerID = it->second.nodeID();
                JLOG(j_.warn()) << "Removing stale proposal from " << peerID;
                for (auto& dt : result_->disputes)
                    dt.second.unVote(peerID);
                it = peerProposals_.erase(it);
            }
            else
            {
                // proposal is still fresh
                ++effCloseTimes[effCloseTime(
                    it->second.closeTime(),
                    closeResolution_,
                    previousLedger_.closeTime())];
                ++it;
            }
        }
    }

    // This will stay unseated unless there are any changes
    boost::optional<TxSet_t> ourNewSet;

    // Update votes on disputed transactions
    {
        boost::optional<typename TxSet_t::MutableTxSet> mutableSet;
        for (auto& it : result_->disputes)
        {
            // Because the threshold for inclusion increases,
            //  time can change our position on a dispute
            if (it.second.updateVote(
                    convergePercent_, (mode_ == Mode::proposing), parms_))
            {
                if (!mutableSet)
                    mutableSet.emplace(result_->set);

                if (it.second.getOurVote())
                {
                    // now a yes
                    mutableSet->insert(it.second.tx());
                }
                else
                {
                    // now a no
                    mutableSet->erase(it.first);
                }
            }
        }

        if (mutableSet)
            ourNewSet.emplace(*mutableSet);
    }

    NetClock::time_point consensusCloseTime = {};
    haveCloseTimeConsensus_ = false;

    if (peerProposals_.empty())
    {
        // no other times
        haveCloseTimeConsensus_ = true;
        consensusCloseTime = effCloseTime(
            result_->position.closeTime(),
            closeResolution_,
            previousLedger_.closeTime());
    }
    else
    {
        int neededWeight;

        if (convergePercent_ < parms_.avMID_CONSENSUS_TIME)
            neededWeight = parms_.avINIT_CONSENSUS_PCT;
        else if (convergePercent_ < parms_.avLATE_CONSENSUS_TIME)
            neededWeight = parms_.avMID_CONSENSUS_PCT;
        else if (convergePercent_ < parms_.avSTUCK_CONSENSUS_TIME)
            neededWeight = parms_.avLATE_CONSENSUS_PCT;
        else
            neededWeight = parms_.avSTUCK_CONSENSUS_PCT;

        int participants = peerProposals_.size();
        if (mode_ == Mode::proposing)
        {
            ++effCloseTimes[effCloseTime(
                result_->position.closeTime(),
                closeResolution_,
                previousLedger_.closeTime())];
            ++participants;
        }

        // Threshold for non-zero vote
        int threshVote = participantsNeeded(participants, neededWeight);

        // Threshold to declare consensus
        int const threshConsensus =
            participantsNeeded(participants, parms_.avCT_CONSENSUS_PCT);

        JLOG(j_.info()) << "Proposers:" << peerProposals_.size()
                        << " nw:" << neededWeight << " thrV:" << threshVote
                        << " thrC:" << threshConsensus;

        for (auto const& it : effCloseTimes)
        {
            JLOG(j_.debug())
                << "CCTime: seq " << previousLedger_.seq() + 1 << ": "
                << it.first.time_since_epoch().count() << " has " << it.second
                << ", " << threshVote << " required";

            if (it.second >= threshVote)
            {
                // A close time has enough votes for us to try to agree
                consensusCloseTime = it.first;
                threshVote = it.second;

                if (threshVote >= threshConsensus)
                    haveCloseTimeConsensus_ = true;
            }
        }

        if (!haveCloseTimeConsensus_)
        {
            JLOG(j_.debug())
                << "No CT consensus:"
                << " Proposers:" << peerProposals_.size()
                << " Mode:" << to_string(mode_) << " Thresh:" << threshConsensus
                << " Pos:" << consensusCloseTime.time_since_epoch().count();
        }
    }

    if (!ourNewSet &&
        ((consensusCloseTime !=
          effCloseTime(
              result_->position.closeTime(),
              closeResolution_,
              previousLedger_.closeTime())) ||
         result_->position.isStale(ourCutoff)))
    {
        // close time changed or our position is stale
        ourNewSet.emplace(result_->set);
    }

    if (ourNewSet)
    {
        auto newID = ourNewSet->id();

        result_->set = std::move(*ourNewSet);

        JLOG(j_.info()) << "Position change: CTime "
                        << consensusCloseTime.time_since_epoch().count()
                        << ", tx " << newID;

        result_->position.changePosition(newID, consensusCloseTime, now_);

        // Share our new transaction set and update disputes
        // if we haven't already received it
        if (acquired_.emplace(newID, result_->set).second)
        {
            if (!result_->position.isBowOut())
                impl().relay(result_->set);

            for (auto const& p : peerProposals_)
            {
                if (p.second.position() == newID)
                {
                    updateDisputes(p.first, result_->set);
                }
            }
        }

        // Share our new position if we are still participating this round
        if (!result_->position.isBowOut() && (mode_ == Mode::proposing))
            impl().propose(result_->position);
    }
}

template <class Derived, class Traits>
bool
Consensus<Derived, Traits>::haveConsensus()
{
    // Must have a stance if we are checking for consensus
    assert(result_);

    // CHECKME: should possibly count unacquired TX sets as disagreeing
    int agree = 0, disagree = 0;

    auto ourPosition = result_->position.position();

    // Count number of agreements/disagreements with our position
    for (auto& it : peerProposals_)
    {
        if (it.second.position() == ourPosition)
        {
            ++agree;
        }
        else
        {
            using std::to_string;

            JLOG(j_.debug()) << to_string(it.first) << " has "
                             << to_string(it.second.position());
            ++disagree;
        }
    }
    auto currentFinished = impl().proposersFinished(prevLedgerID_);

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
        parms_,
        mode_ == Mode::proposing,
        j_);

    if (result_->state == ConsensusState::No)
        return false;

    // There is consensus, but we need to track if the network moved on
    // without us.
    if (result_->state == ConsensusState::MovedOn)
    {
        JLOG(j_.error()) << "Unable to reach consensus";
        JLOG(j_.error()) << getJson(true);
    }

    return true;
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::leaveConsensus()
{
    if (mode_ == Mode::proposing)
    {
        if (result_ && !result_->position.isBowOut())
        {
            result_->position.bowOut(now_);
            impl().propose(result_->position);
        }

        mode_ = Mode::observing;
        JLOG(j_.info()) << "Bowing out of consensus";
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::createDisputes(TxSet_t const& o)
{
    // Cannot create disputes without our stance
    assert(result_);

    // Only create disputes if this is a new set
    if (!result_->compares.emplace(o.id()).second)
        return;

    // Nothing to dispute if we agree
    if (result_->set.id() == o.id())
        return;

    JLOG(j_.debug()) << "createDisputes " << result_->set.id() << " to "
                     << o.id();

    auto differences = result_->set.compare(o);

    int dc = 0;

    for (auto& id : differences)
    {
        ++dc;
        // create disputed transactions (from the ledger that has them)
        assert(
            (id.second && result_->set.find(id.first) && !o.find(id.first)) ||
            (!id.second && !result_->set.find(id.first) && o.find(id.first)));

        Tx_t tx = id.second ? *result_->set.find(id.first) : *o.find(id.first);
        auto txID = tx.id();

        if (result_->disputes.find(txID) != result_->disputes.end())
            continue;

        JLOG(j_.debug()) << "Transaction " << txID << " is disputed";

        typename Result::Dispute_t dtx{tx, result_->set.exists(txID), j_};

        // Update all of the available peer's votes on the disputed transaction
        for (auto& pit : peerProposals_)
        {
            auto cit(acquired_.find(pit.second.position()));

            if (cit != acquired_.end())
                dtx.setVote(pit.first, cit->second.exists(txID));
        }
        impl().relay(dtx.tx());

        result_->disputes.emplace(txID, std::move(dtx));
    }
    JLOG(j_.debug()) << dc << " differences found";
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::updateDisputes(
    NodeID_t const& node,
    TxSet_t const& other)
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

template <class Derived, class Traits>
std::string
Consensus<Derived, Traits>::to_string(Phase p)
{
    switch (p)
    {
        case Phase::open:
            return "open";
        case Phase::establish:
            return "establish";
        case Phase::accepted:
            return "accepted";
        default:
            return "unknown";
    }
}

template <class Derived, class Traits>
std::string
Consensus<Derived, Traits>::to_string(Mode m)
{
    switch (m)
    {
        case Mode::proposing:
            return "proposing";
        case Mode::observing:
            return "observing";
        case Mode::wrongLedger:
            return "wrongLedger";
        case Mode::switchedLedger:
            return "switchedLedger";
        default:
            return "unknown";
    }
}
}  // ripple

#endif

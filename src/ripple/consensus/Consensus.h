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

#ifndef RIPPLE_CONSENSUS_CONSENSUS_H_INCLUDED
#define RIPPLE_CONSENSUS_CONSENSUS_H_INCLUDED

#include <ripple/consensus/DisputedTx.h>
#include <ripple/beast/utility/Journal.h>

namespace ripple {

/** Generic implementation of consensus algorithm.

  Achieves consensus on the next ledger.

  Two things need consensus:
    1.  The set of transactions included in the ledger.
    2.  The close time for the ledger.

  This class uses CRTP to allow adapting Consensus for specific applications.

  @tparam Derived The deriving class which adapts the Consensus algorithm.
  @tparam Traits Provides definitions of types used in Consensus.
*/
template <class Derived, class Traits>
class Consensus
{
    enum class State
    {
        // We haven't closed our ledger yet, but others might have
        open,

        // Establishing consensus
        establish,

        // We have closed on a transaction set and are
        // processing the new ledger
        processing,

        // We have accepted / validated a new last closed ledger
        // and need to start a new round
        accepted,
    };

public:
    using clock_type = beast::abstract_clock <std::chrono::steady_clock>;

    using NetTime_t = typename Traits::NetTime_t;
    using Ledger_t = typename Traits::Ledger_t;
    using Proposal_t = typename Traits::Proposal_t;
    using TxSet_t = typename Traits::TxSet_t;
    using Tx_t = typename TxSet_t::Tx;
    using NodeID_t = typename Proposal_t::NodeID;
    using Dispute_t = DisputedTx<Tx_t, NodeID_t>;

    Consensus(Consensus const&) = delete;
    Consensus& operator=(Consensus const&) = delete;
    ~Consensus () = default;

	/** Constructor.

	    @param clock The clock used to internally measure consensus progress
		@param j The journal to log debug output
	*/
	Consensus(clock_type const & clock, beast::Journal j);


    /** Kick-off the next round of consensus.

        @param now The network adjusted time
        @param prevLgrId the ID/hash of the last ledger
        @param previousLedger Best guess of what the last closed ledger was.

        Note that @b prevLgrId is not required to the ID of @b prevLgr since
        the ID is shared independent of the full ledger.
    */
    void
	startRound (
        NetTime_t const& now,
        typename Ledger_t::ID const& prevLgrId,
        Ledger_t const& prevLgr);


    /** A peer has proposed a new position, adjust our tracking

        @param now The network adjusted time
        @param newPosition The new peer's position
        @return Whether we should do delayed relay of this proposal.
    */
    bool
    peerProposal (
        NetTime_t const& now,
        Proposal_t const& newPosition);

    /** Call periodically to drive consensus forward

        @param now The network adjusted time
    */
    void
    timerEntry (NetTime_t const& now);

    /**
        Process a transaction set, typically acquired from the network
        @param now The network adjusted time
        @param txSet the transaction set
    */
    void
    gotTxSet (
        NetTime_t const& now,
        TxSet_t const& txSet);

    /** Simulate the consensus process without any network traffic.

         The end result, is that consensus begins and completes as if everyone
         had agreed with whatever we propose.

         This function is only called from the rpc "ledger_accept" path with the
         server in standalone mode and SHOULD NOT be used during the normal
         consensus process.

         @param now The current network adjusted time.
         @param consensusDelay (Optional) duration to delay between closing and
                                accepting the ledger. Uses 100ms if unspecified.
    */
    void
    simulate(
        NetTime_t const& now,
        boost::optional<std::chrono::milliseconds> consensusDelay);

    /* @return the ID/hash of the last closed ledger */
    typename Ledger_t::ID
    LCL()
    {
       std::lock_guard<std::recursive_mutex> _(lock_);
       return prevLedgerHash_;
    }

    /** @return The number of proposing peers that participated in the
                previous round.
    */
    int
    getLastCloseProposers() const
    {
        return previousProposers_;
    }

    /** @return The duration of the previous round measured from closing the
                open ledger to starting accepting the results.
    */
    std::chrono::milliseconds
    getLastCloseDuration() const
    {
        return previousRoundTime_;
    }

    /** @return Whether we are sending proposals during consensus.
    */
    bool
    proposing() const
    {
        return proposing_;
    }

    /** @return Whether we are validating consensus ledgers.
    */
    bool
    validating() const
    {
        return validating_;
    }

    /** @return Whether we have the correct last closed ledger.
    */
    bool
    haveCorrectLCL() const
    {
        return haveCorrectLCL_;
    }

    /** Get the Json state of the consensus process.

        Called by the consensus_info RPC.

        @param full True if verbose response desired.
        @return     The Json state.
    */
    Json::Value
    getJson (bool full) const;

private:

    /** Change our view of the last closed ledger

        @param lgrId The ID of the last closed ledger to switch to.
    */
    void
    handleLCL (typename Ledger_t::ID const& lgrId);

    /** Check if our last closed ledger matches the network's.

        If the last closed ledger differs,  we are no longer in sync with
        the network. If we enter the consensus round with
        the wrong ledger, we can leave it with the correct ledger so
        that we can participate in the next round.
    */
    void
    checkLCL ();

    /** If we radically changed our consensus context for some reason,
        we need to replay recent proposals so that they're not lost.
    */
    void
    playbackProposals ();


    /** Handle pre-close state.

        In the pre-close state, the ledger is open as we wait for new
        transactions.  After enough time has elapsed, we will close the ledger
        and start the consensus process.
    */
    void
    statePreClose ();

    /** Handle establish state.

        In the establish state, the ledger has closed and we work with peers
        to reach consensus. Update our position only on the timer, and in this
        state.

        If we have consensus, move to the processing state.
    */
    void
    stateEstablish ();

    /** Close the open ledger and establish initial position.
    */
    void
    closeLedger ();

    /** Take an initial position on the consensus set.
    */
    void
    takeInitialPosition ();

    /** Compare two proposed transaction sets and create disputed
        transctions structures for any mismatches

        @param m1 One transaction set
        @param m2 The other transaction set
    */
    void
    createDisputes (TxSet_t const& m1, TxSet_t const& m2);

    /** Add a disputed transaction (one that at least one node wants
        in the consensus set and at least one node does not) to our tracking

        @param tx The disputed transaction
    */
    void
    addDisputedTransaction (Tx_t const& tx);

    /** Adjust the votes on all disputed transactions based
        on the set of peers taking this position

        @param txSet A disputed position
        @param peers Peers which are taking the position txSet
    */
    void adjustCount (TxSet_t const& txSet, std::vector<NodeID_t> const& peers);

    /** Adjust our positions to try to agree with other validators.

    */
    void
    updateOurPositions ();

    /** Revoke our outstanding proposal, if any, and cease proposing at least
        until this round ends.
    */
    void
    leaveConsensus ();

    /** Process complete transaction set.

        Called when:
          * We take our initial position
          * We take a new position
          * We acquire a position a validator took

       We store it, notify peers that we have it, and update our tracking if
       any validators currently propose it.

      @param txSet      the transaction set.
      @param acquired true if we have acquired the transaction set.
    */
    void
    gotTxSetInternal ( TxSet_t const& txSet, bool acquired);

    /** @return The Derived class that implements the CRTP requirements.
    */
    Derived &
	impl()
	{
		return *static_cast<Derived*>(this);
	}


	mutable std::recursive_mutex lock_;

    //-------------------------------------------------------------------------
    // Consensus state variables
    State state_;
    bool proposing_ = false;
    bool validating_ = false;
    bool haveCorrectLCL_ = false;
    bool consensusFail_ = false;
    bool haveCloseTimeConsensus_ = false;
    bool firstRound_ = true;

    //-------------------------------------------------------------------------
    //! Clock for measuring consensus progress
    clock_type const & clock_;

    // How much time has elapsed since the round started
    std::chrono::milliseconds roundTime_ = std::chrono::milliseconds{0};

    // How long the close has taken, expressed as a percentage of the time that
    // we expected it to take.
    int closePercent_{0};
    typename NetTime_t::duration closeResolution_ = ledgerDefaultTimeResolution;
    clock_type::time_point consensusStartTime_;

    // Time it took for the last consensus round to converge
    std::chrono::milliseconds previousRoundTime_ = LEDGER_IDLE_INTERVAL;

    //-------------------------------------------------------------------------
    // Network time measurements of consensus progress
    NetTime_t now_;

    // The network time this ledger closed
    NetTime_t closeTime_;

    // Close time estimates, keep ordered for predictable traverse
    std::map <NetTime_t, int> closeTimes_;

    //-------------------------------------------------------------------------
    // Non-peer (self) consensus data
    typename Ledger_t::ID prevLedgerHash_;
    Ledger_t previousLedger_;

    // Transaction Sets, indexed by hash of transaction tree
    hash_map<typename TxSet_t::ID, const TxSet_t> acquired_;

    boost::optional<Proposal_t> ourPosition_;
    boost::optional<TxSet_t> ourSet_;

    //-------------------------------------------------------------------------
    // Peer related consensus data
    // Convergence tracking, trusted peers indexed by hash of public key
    hash_map<NodeID_t, Proposal_t>  peerProposals_;

    // The number of proposers who participated in the last consensus round
    int previousProposers_ = 0;

    // Disputed transactions
    hash_map<typename Tx_t::ID, Dispute_t> disputes_;

    // Set of TxSet ids we have already compared/created disputes
    hash_set<typename TxSet_t::ID> compares_;

    // nodes that have bowed out of this consensus process
    hash_set<NodeID_t> deadNodes_;

    // Journal for debugging
    beast::Journal j_;

};

template <class Derived, class Traits>
Consensus<Derived, Traits>::Consensus (
        clock_type const & clock,
	    beast::Journal journal)
    : clock_(clock)
    , j_(journal)
{
    JLOG (j_.debug()) << "Creating consensus object";
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::startRound (
    NetTime_t const& now,
    typename Ledger_t::ID const& prevLCLHash,
    Ledger_t const & prevLedger)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    if (state_ == State::processing)
    {
        // We can't start a new round while we're processing
        return;
    }

    if (firstRound_)
    {
        // take our initial view of closeTime_ from the seed ledger
        closeTime_ = prevLedger.closeTime();
        firstRound_ = false;
    }

    state_ = State::open;
    now_ = now;
    prevLedgerHash_ = prevLCLHash;
    previousLedger_ = prevLedger;
    ourPosition_.reset();
    ourSet_.reset();
    consensusFail_ = false;
    roundTime_ = 0ms;
    closePercent_ = 0;
    haveCloseTimeConsensus_ = false;
    consensusStartTime_ = clock_.now();
    haveCorrectLCL_ = (previousLedger_.id() == prevLedgerHash_);

    impl().onStartRound(previousLedger_);

    peerProposals_.clear();
    acquired_.clear();
    disputes_.clear();
    compares_.clear();
    closeTimes_.clear();
    deadNodes_.clear();

    closeResolution_ = getNextLedgerTimeResolution (
        previousLedger_.closeTimeResolution(),
        previousLedger_.closeAgree(),
        previousLedger_.seq() + 1);


    // We should not be proposing but not validating
    // Okay to validate but not propose
    std::tie(proposing_, validating_) = impl().getMode();
    assert (! proposing_ || validating_);

    if (validating_)
    {
        JLOG (j_.info())
            << "Entering consensus process, validating";
    }
    else
    {
        // Otherwise we just want to monitor the validation process.
        JLOG (j_.info())
            << "Entering consensus process, watching";
    }


    if (! haveCorrectLCL_)
    {
        // If we were not handed the correct LCL, then set our state
        // to not proposing.
        handleLCL (prevLedgerHash_);

        if (! haveCorrectLCL_)
        {
            JLOG (j_.info())
                << "Entering consensus with: "
                << previousLedger_.id();
            JLOG (j_.info())
                << "Correct LCL is: " << prevLCLHash;
        }
    }

    playbackProposals ();
    if (peerProposals_.size() > (previousProposers_ / 2))
    {
        // We may be falling behind, don't wait for the timer
        // consider closing the ledger immediately
        timerEntry (now_);
    }
}

template <class Derived, class Traits>
bool
Consensus<Derived, Traits>::peerProposal (
    NetTime_t const& now,
    Proposal_t const& newPosition)
{
    auto const peerID = newPosition.nodeID ();

    std::lock_guard<std::recursive_mutex> _(lock_);

    now_ = now;

    if (newPosition.prevLedger() != prevLedgerHash_)
    {
        JLOG (j_.debug()) << "Got proposal for "
            << newPosition.prevLedger()
            << " but we are on " << prevLedgerHash_;
        return false;
    }

    if (deadNodes_.find (peerID) != deadNodes_.end ())
    {
        using std::to_string;
        JLOG (j_.info())
            << "Position from dead node: " << to_string (peerID);
        return false;
    }

    {
        // update current position
        auto currentPosition = peerProposals_.find(peerID);

        if (currentPosition != peerProposals_.end())
        {
            if (newPosition.proposeSeq ()
                <= currentPosition->second.proposeSeq())
            {
                return false;
            }
        }

        if (newPosition.isBowOut ())
        {
            using std::to_string;

            JLOG (j_.info())
                << "Peer bows out: " << to_string (peerID);

            for (auto& it : disputes_)
                it.second.unVote (peerID);
            if (currentPosition != peerProposals_.end())
                peerProposals_.erase (peerID);
            deadNodes_.insert (peerID);

            return true;
        }

        if (currentPosition != peerProposals_.end())
            currentPosition->second = newPosition;
        else
            peerProposals_.emplace (peerID, newPosition);
    }

    if (newPosition.isInitial ())
    {
        // Record the close time estimate
        JLOG (j_.trace())
            << "Peer reports close time as "
            << newPosition.closeTime().time_since_epoch().count();
        ++closeTimes_[newPosition.closeTime()];
    }

    JLOG (j_.trace()) << "Processing peer proposal "
        << newPosition.proposeSeq () << "/"
        << newPosition.position ();

    {
        auto ait = acquired_.find (newPosition.position());
        if (ait == acquired_.end())
        {
            if (auto set = impl().acquireTxSet(newPosition))
            {
                ait = acquired_.emplace (newPosition.position(),
                    std::move(*set)).first;
            }
        }


        if (ait != acquired_.end())
        {
            for (auto& it : disputes_)
                it.second.setVote (peerID,
                    ait->second.exists (it.first));
        }
        else
        {
            JLOG (j_.debug())
                << "Don't have tx set for peer";
        }
    }

    return true;
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::timerEntry (NetTime_t const& now)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    now_ = now;

    try
    {
       if ((state_ != State::processing) && (state_ != State::accepted))
           checkLCL ();

        using namespace std::chrono;
        roundTime_ = duration_cast<milliseconds>
                           (clock_.now() - consensusStartTime_);

        closePercent_ = roundTime_ * 100 /
            std::max<milliseconds> (
                previousRoundTime_, AV_MIN_CONSENSUS_TIME);

        switch (state_)
        {
        case State::open:
            statePreClose ();

            if (state_ != State::establish) return;

            // Fall through

        case State::establish:
            stateEstablish ();
            return;

        case State::processing:
            // We are processing the finished ledger
            // logic of calculating next ledger advances us out of this state
            // nothing to do
            return;

        case State::accepted:
            // NetworkOPs needs to setup the next round
            // nothing to do
            return;
        }

        assert (false);
    }
    catch (typename Traits::MissingTxException_t const& mn)
    {
        // This should never happen
        leaveConsensus ();
        JLOG (j_.error()) <<
           "Missing node during consensus process " << mn;
        Rethrow();
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::gotTxSet (
    NetTime_t const& now,
    TxSet_t const& txSet)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    now_ = now;

    try
    {
        gotTxSetInternal (txSet, true);
    }
    catch (typename Traits::MissingTxException_t const& mn)
    {
        // This should never happen
        leaveConsensus();
        JLOG (j_.error()) <<
            "Missing node processing complete map " << mn;
        Rethrow();
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::simulate (
    NetTime_t const& now,
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    JLOG (j_.info()) << "Simulating consensus";
    now_ = now;
    closeLedger ();
    roundTime_ = consensusDelay.value_or(100ms);
    beginAccept (true);
    JLOG (j_.info()) << "Simulation complete";
}

template <class Derived, class Traits>
Json::Value
Consensus<Derived, Traits>::getJson (bool full) const
{
    using std::to_string;
    using Int = Json::Value::Int;

    Json::Value ret (Json::objectValue);
    std::lock_guard<std::recursive_mutex> _(lock_);

    ret["proposing"] = proposing_;
    ret["validating"] = validating_;
    ret["proposers"] = static_cast<int> (peerProposals_.size ());

    if (haveCorrectLCL_)
    {
        ret["synched"] = true;
        ret["ledger_seq"] = previousLedger_.seq() + 1;
        ret["close_granularity"] = static_cast<Int>(closeResolution_.count());
    }
    else
        ret["synched"] = false;

    switch (state_)
    {
    case State::open:
        ret["state"] = "open";
        break;

    case State::establish:
        ret["state"] = "consensus";
        break;

    case State::processing:
        ret["state"] = "processing";
        break;

    case State::accepted:
        ret["state"] = "accepted";
        break;
    }

    int v = disputes_.size ();

    if ((v != 0) && !full)
        ret["disputes"] = v;

    if (ourPosition_)
        ret["our_position"] = ourPosition_->getJson ();

    if (full)
    {
        ret["current_ms"] = static_cast<Int>(roundTime_.count());
        ret["close_percent"] = closePercent_;
        ret["close_resolution"] = static_cast<Int>(closeResolution_.count());
        ret["have_time_consensus"] = haveCloseTimeConsensus_;
        ret["previous_proposers"] = previousProposers_;
        ret["previous_mseconds"] =
            static_cast<Int>(previousRoundTime_.count());

        if (! peerProposals_.empty ())
        {
            Json::Value ppj (Json::objectValue);

            for (auto& pp : peerProposals_)
            {
                ppj[to_string (pp.first)] = pp.second.getJson ();
            }
            ret["peer_positions"] = std::move(ppj);
        }

        if (! acquired_.empty ())
        {
            Json::Value acq (Json::arrayValue);
            for (auto& at : acquired_)
            {
                acq.append (to_string (at.first));
            }
            ret["acquired"] = std::move(acq);
        }

        if (! disputes_.empty ())
        {
            Json::Value dsj (Json::objectValue);
            for (auto& dt : disputes_)
            {
                dsj[to_string (dt.first)] = dt.second.getJson ();
            }
            ret["disputes"] = std::move(dsj);
        }

        if (! closeTimes_.empty ())
        {
            Json::Value ctj (Json::objectValue);
            for (auto& ct : closeTimes_)
            {
                ctj[std::to_string(ct.first.time_since_epoch().count())] = ct.second;
            }
            ret["close_times"] = std::move(ctj);
        }

        if (! deadNodes_.empty ())
        {
            Json::Value dnj (Json::arrayValue);
            for (auto const& dn : deadNodes_)
            {
                dnj.append (to_string (dn));
            }
            ret["dead_nodes"] = std::move(dnj);
        }
    }

    return ret;
}

// Handle a change in the LCL during a consensus round
template <class Derived, class Traits>
void
Consensus<Derived, Traits>::handleLCL (typename Ledger_t::ID const& lgrId)
{
    assert (lgrId != prevLedgerHash_ ||
            previousLedger_.id() != lgrId);

    if (prevLedgerHash_ != lgrId)
    {
        // first time switching to this ledger
        prevLedgerHash_ = lgrId;

        if (haveCorrectLCL_ && proposing_ && ourPosition_)
        {
            JLOG (j_.info()) << "Bowing out of consensus";
            leaveConsensus();
        }

        // Stop proposing because we are out of sync
        proposing_ = false;
        peerProposals_.clear ();
        disputes_.clear ();
        compares_.clear ();
        closeTimes_.clear ();
        deadNodes_.clear ();
        // To get back in sync:
        playbackProposals ();
    }

    if (previousLedger_.id() == prevLedgerHash_)
        return;

    // we need to switch the ledger we're working from
    if (auto buildLCL = impl().acquireLedger(prevLedgerHash_))
    {
        JLOG (j_.info()) <<
        "Have the consensus ledger " << prevLedgerHash_;

        startRound (now_, lgrId, *buildLCL);
    }
    else
    {
            haveCorrectLCL_ = false;
    }
}


template <class Derived, class Traits>
void
Consensus<Derived, Traits>::checkLCL ()
{
    auto netLgr = impl().getLCL (
        prevLedgerHash_,
        haveCorrectLCL_ ? previousLedger_.parentID() : typename Ledger_t::ID{},
        haveCorrectLCL_);

    if (netLgr != prevLedgerHash_)
    {
        // LCL change
        const char* status;

        switch (state_)
        {
        case State::open:
            status = "open";
            break;

        case State::establish:
            status = "establish";
            break;

        case State::processing:
            status = "processing";
            break;

        case State::accepted:
            status = "accepted";
            break;

        default:
            status = "unknown";
        }

        JLOG (j_.warn())
            << "View of consensus changed during " << status
            << " status=" << status << ", "
            << (haveCorrectLCL_ ? "CorrectLCL" : "IncorrectLCL");
        JLOG (j_.warn()) << prevLedgerHash_
            << " to " << netLgr;
        JLOG (j_.warn())
            << previousLedger_.getJson();
        handleLCL (netLgr);
    }
    else if(previousLedger_.id() != prevLedgerHash_)
        handleLCL(netLgr);
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::playbackProposals ()
{
    for (auto const & p : impl().proposals(prevLedgerHash_))
    {
        if(peerProposal(now_, p))
            impl().relay(p);
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::statePreClose ()
{
    // it is shortly before ledger close time
    bool anyTransactions = impl().hasOpenTransactions();
    int proposersClosed = peerProposals_.size ();
    int proposersValidated = impl().numProposersValidated(prevLedgerHash_);

    // This computes how long since last ledger's close time
    using namespace std::chrono;
    milliseconds sinceClose;
    {
        bool previousCloseCorrect = haveCorrectLCL_
            && previousLedger_.closeAgree ()
            && (previousLedger_.closeTime() !=
                (previousLedger_.parentCloseTime() + 1s));

        auto lastCloseTime = previousCloseCorrect
            ? previousLedger_.closeTime() // use consensus timing
            : closeTime_; // use the time we saw internally

        if (now_ >= lastCloseTime )
            sinceClose = duration_cast<milliseconds>(now_ - lastCloseTime);
        else
            sinceClose = -duration_cast<milliseconds>(lastCloseTime  - now_);
    }

    auto const idleInterval = std::max<seconds>(LEDGER_IDLE_INTERVAL,
        duration_cast<seconds>(2 * previousLedger_.closeTimeResolution()));

    // Decide if we should close the ledger
    if (shouldCloseLedger (anyTransactions
        , previousProposers_, proposersClosed, proposersValidated
        , previousRoundTime_, sinceClose, roundTime_
        , idleInterval, j_))
    {
        closeLedger ();
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::stateEstablish ()
{
    // Give everyone a chance to take an initial position
    if (roundTime_ < LEDGER_MIN_CONSENSUS)
        return;

    updateOurPositions ();

    // Nothing to do if we don't have consensus.
    if (!haveConsensus ())
        return;

    if (!haveCloseTimeConsensus_)
    {
        JLOG (j_.info()) <<
            "We have TX consensus but not CT consensus";
        return;
    }

    JLOG (j_.info()) <<
        "Converge cutoff (" << peerProposals_.size () << " participants)";
    state_ = State::processing;
    beginAccept (false);
}


template <class Derived, class Traits>
void
Consensus<Derived, Traits>::closeLedger ()
{
    state_ = State::establish;
    consensusStartTime_ = clock_.now ();
    closeTime_ = now_;

    impl().onClose (previousLedger_, haveCorrectLCL_);

    takeInitialPosition ();
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::takeInitialPosition()
{
    auto pair = impl().makeInitialPosition(previousLedger_, proposing_,
       haveCorrectLCL_,  closeTime_, now_ );
    auto const& initialSet = pair.first;
    auto const& initialPos = pair.second;
    assert (initialSet.id() == initialPos.position());

    ourPosition_ = initialPos;
    ourSet_ = initialSet;

    for (auto& it : disputes_)
    {
        it.second.setOurVote (initialSet.exists (it.first));
    }

    // When we take our initial position,
    // we need to create any disputes required by our position
    // and any peers who have already taken positions
    compares_.emplace (initialSet.id());
    for (auto& it : peerProposals_)
    {
        auto pos = it.second.position();
        auto iit (acquired_.find (pos));
        if (iit != acquired_.end ())
        {
            if (compares_.emplace (pos).second)
                createDisputes (initialSet, iit->second);
        }
    }

    gotTxSetInternal (initialSet, false);

    if (proposing_)
        impl().propose (*ourPosition_);
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::gotTxSetInternal (
    TxSet_t const& txSet,
    bool acquired)
{
    auto const hash = txSet.id();

    if (acquired_.find (hash) != acquired_.end())
        return;

    if (acquired)
    {
        JLOG (j_.trace()) << "We have acquired txs " << hash;
    }

    // We now have a txSet that we did not have before

    if (! acquired)
    {
        // If we generated this locally,
        // put the txSet where others can get it
        // If we acquired it, it's already shared
        impl().share (txSet);
    }

    if (! ourPosition_)
    {
        JLOG (j_.debug())
            << "Not creating disputes: no position yet.";
    }
    else if (ourPosition_->isBowOut ())
    {
        JLOG (j_.warn())
            << "Not creating disputes: not participating.";
    }
    else if (hash == ourPosition_->position ())
    {
        JLOG (j_.debug())
            << "Not creating disputes: identical position.";
    }
    else
    {
        // Our position is not the same as the acquired position
        // create disputed txs if needed
        createDisputes (*ourSet_, txSet);
        compares_.insert(hash);
    }

    // Adjust tracking for each peer that takes this position
    std::vector<NodeID_t> peers;
    for (auto& it : peerProposals_)
    {
        if (it.second.position () == hash)
            peers.push_back (it.second.nodeID ());
    }

    if (!peers.empty ())
    {
        adjustCount (txSet, peers);
    }
    else if (acquired)
    {
        JLOG (j_.warn())
            << "By the time we got the map " << hash
            << " no peers were proposing it";
    }

    acquired_.emplace (hash, txSet);
}

template <class Derived, class Traits>
void Consensus<Derived, Traits>::createDisputes (
    TxSet_t const& m1,
    TxSet_t const& m2)
{
    if (m1.id() == m2.id())
        return;

    JLOG (j_.debug()) << "createDisputes "
        << m1.id() << " to " << m2.id();
    auto differences = m1.compare (m2);

    int dc = 0;
    // for each difference between the transactions
    for (auto& id : differences)
    {
        ++dc;
        // create disputed transactions (from the ledger that has them)
        assert (
            (id.second && m1.find(id.first) && !m2.find(id.first)) ||
            (!id.second && !m1.find(id.first) && m2.find(id.first))
        );
        if (id.second)
            addDisputedTransaction (*m1.find (id.first));
        else
            addDisputedTransaction (*m2.find (id.first));
    }
    JLOG (j_.debug()) << dc << " differences found";
}

template <class Derived, class Traits>
void Consensus<Derived, Traits>::addDisputedTransaction (
    Tx_t const& tx)
{
    auto txID = tx.id();

    if (disputes_.find (txID) != disputes_.end ())
        return;

    JLOG (j_.debug()) << "Transaction "
        << txID << " is disputed";

    bool ourVote = false;

    // Update our vote on the disputed transaction
    if (ourSet_)
        ourVote = ourSet_->exists (txID);

    Dispute_t dtx {tx, ourVote, j_};

    // Update all of the peer's votes on the disputed transaction
    for (auto& pit : peerProposals_)
    {
        auto cit (acquired_.find (pit.second.position ()));

        if (cit != acquired_.end ())
            dtx.setVote (pit.first,
                cit->second.exists (txID));
    }

    impl().relay(dtx);

    disputes_.emplace (txID, std::move (dtx));
}

template <class Derived, class Traits>
void Consensus<Derived, Traits>::adjustCount (TxSet_t const& txSet,
    std::vector<NodeID_t> const& peers)
{
    for (auto& it : disputes_)
    {
        bool setHas = txSet.exists (it.first);
        for (auto const& pit : peers)
            it.second.setVote (pit, setHas);
    }
}

/** How many of the participants must agree to reach a given threshold?

    Note that the number may not precisely yield the requested percentage.
    For example, with with size = 5 and percent = 70, we return 3, but
    3 out of 5 works out to 60%. There are no security implications to
    this.

    @param participants the number of participants (i.e. validators)
    @param the percent that we want to reach

    @return the number of participants which must agree
*/
inline int
participantsNeeded (int participants, int percent)
{
    int result = ((participants * percent) + (percent / 2)) / 100;

    return (result == 0) ? 1 : result;
}

template <class Derived, class Traits>
void Consensus<Derived, Traits>::updateOurPositions ()
{
    // Compute a cutoff time
    auto peerCutoff = now_ - PROPOSE_FRESHNESS;
    auto ourCutoff = now_ - PROPOSE_INTERVAL;

    // Verify freshness of peer positions and compute close times
    std::map<NetTime_t, int> closeTimes;
    {
        auto it = peerProposals_.begin ();
        while (it != peerProposals_.end ())
        {
            if (it->second.isStale (peerCutoff))
            {
                // peer's proposal is stale, so remove it
                auto const& peerID = it->second.nodeID ();
                JLOG (j_.warn())
                    << "Removing stale proposal from " << peerID;
                for (auto& dt : disputes_)
                    dt.second.unVote (peerID);
                it = peerProposals_.erase (it);
            }
            else
            {
                // proposal is still fresh
                ++closeTimes[effectiveCloseTime(it->second.closeTime(),
                    closeResolution_, previousLedger_.closeTime())];
                ++it;
            }
        }
    }

    // This will stay unseated unless there are any changes
    boost::optional <TxSet_t> ourNewSet;

    // Update votes on disputed transactions
    {
        boost::optional <TxSet_t> changedSet;
        for (auto& it : disputes_)
        {
            // Because the threshold for inclusion increases,
            //  time can change our position on a dispute
            if (it.second.updateVote (closePercent_, proposing_))
            {
                if (! changedSet)
                    changedSet = *ourSet_;

                if (it.second.getOurVote ())
                {
                    // now a yes
                    changedSet->insert (it.second.tx());
                }
                else
                {
                    // now a no
                    changedSet->erase (it.first);
                }
            }
        }
        if (changedSet)
        {
            ourNewSet.emplace (*changedSet);
        }
    }

    int neededWeight;

    if (closePercent_ < AV_MID_CONSENSUS_TIME)
        neededWeight = AV_INIT_CONSENSUS_PCT;
    else if (closePercent_ < AV_LATE_CONSENSUS_TIME)
        neededWeight = AV_MID_CONSENSUS_PCT;
    else if (closePercent_ < AV_STUCK_CONSENSUS_TIME)
        neededWeight = AV_LATE_CONSENSUS_PCT;
    else
        neededWeight = AV_STUCK_CONSENSUS_PCT;

    NetTime_t closeTime = {};
    haveCloseTimeConsensus_ = false;

    if (peerProposals_.empty ())
    {
        // no other times
        haveCloseTimeConsensus_ = true;
        closeTime = effectiveCloseTime(ourPosition_->closeTime(),
            closeResolution_, previousLedger_.closeTime());
    }
    else
    {
        int participants = peerProposals_.size ();
        if (proposing_)
        {
            ++closeTimes[effectiveCloseTime(ourPosition_->closeTime(),
                closeResolution_, previousLedger_.closeTime())];
            ++participants;
        }

        // Threshold for non-zero vote
        int threshVote = participantsNeeded (participants,
            neededWeight);

        // Threshold to declare consensus
        int const threshConsensus = participantsNeeded (
            participants, AV_CT_CONSENSUS_PCT);

        JLOG (j_.info()) << "Proposers:"
            << peerProposals_.size () << " nw:" << neededWeight
            << " thrV:" << threshVote << " thrC:" << threshConsensus;

        for (auto const& it : closeTimes)
        {
            JLOG (j_.debug()) << "CCTime: seq "
                << previousLedger_.seq() + 1 << ": "
                << it.first.time_since_epoch().count()
                << " has " << it.second << ", "
                << threshVote << " required";

            if (it.second >= threshVote)
            {
                // A close time has enough votes for us to try to agree
                closeTime = it.first;
                threshVote = it.second;

                if (threshVote >= threshConsensus)
                    haveCloseTimeConsensus_ = true;
            }
        }

        if (!haveCloseTimeConsensus_)
        {
            JLOG (j_.debug()) << "No CT consensus:"
                << " Proposers:" << peerProposals_.size ()
                << " Proposing:" << (proposing_ ? "yes" : "no")
                << " Thresh:" << threshConsensus
                << " Pos:" << closeTime.time_since_epoch().count();
        }
    }

    // Temporarily send a new proposal if there's any change to our
    // claimed close time. Once the new close time code is deployed
    // to the full network, this can be relaxed to force a change
    // only if the rounded close time has changed.
    if (! ourNewSet &&
            ((closeTime != ourPosition_->closeTime())
            || ourPosition_->isStale (ourCutoff)))
    {
        // close time changed or our position is stale
        ourNewSet.emplace (*ourSet_);
    }

    if (ourNewSet)
    {
        auto newHash = ourNewSet->id();

        // Setting ourSet_ here prevents gotTxSetInternal
        // from checking for new disputes. But we only changed
        // positions on existing disputes, so no need to.
        ourSet_ = ourNewSet;

        JLOG (j_.info())
            << "Position change: CTime "
            << closeTime.time_since_epoch().count()
            << ", tx " << newHash;

        if (ourPosition_->changePosition (
            newHash, closeTime, now_))
        {
            if (proposing_)
                impl().propose (*ourPosition_);

            gotTxSetInternal (*ourNewSet, false);
        }
    }
}

template <class Derived, class Traits>
void
Consensus<Derived, Traits>::leaveConsensus ()
{
    if (ourPosition_ && ! ourPosition_->isBowOut ())
    {
        ourPosition_->bowOut(now_);
        impl().propose(*ourPosition_);
    }
    proposing_ = false;
}

} // ripple

#endif

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

private:
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
void Consensus<Derived, Traits>::startRound (
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

} // ripple

#endif

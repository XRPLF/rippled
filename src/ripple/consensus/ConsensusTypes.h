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

#ifndef RIPPLE_CONSENSUS_CONSENSUS_TYPES_H_INCLUDED
#define RIPPLE_CONSENSUS_CONSENSUS_TYPES_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/consensus/ConsensusProposal.h>
#include <ripple/consensus/DisputedTx.h>
#include <chrono>
#include <map>

namespace ripple {

/** Represents how a node currently participates in Consensus.

    A node participates in consensus in varying modes, depending on how
    the node was configured by its operator and how well it stays in sync
    with the network during consensus.

    @code
      proposing               observing
         \                       /
          \---> wrongLedger <---/
                     ^
                     |
                     |
                     v
                switchedLedger
   @endcode

   We enter the round proposing or observing. If we detect we are working
   on the wrong prior ledger, we go to wrongLedger and attempt to acquire
   the right one. Once we acquire the right one, we go to the switchedLedger
   mode.  It is possible we fall behind again and find there is a new better
   ledger, moving back and forth between wrongLedger and switchLedger as
   we attempt to catch up.
*/
enum class ConsensusMode {
    //! We are normal participant in consensus and propose our position
    proposing,
    //! We are observing peer positions, but not proposing our position
    observing,
    //! We have the wrong ledger and are attempting to acquire it
    wrongLedger,
    //! We switched ledgers since we started this consensus round but are now
    //! running on what we believe is the correct ledger.  This mode is as
    //! if we entered the round observing, but is used to indicate we did
    //! have the wrongLedger at some point.
    switchedLedger
};

inline std::string
to_string(ConsensusMode m)
{
    switch (m)
    {
        case ConsensusMode::proposing:
            return "proposing";
        case ConsensusMode::observing:
            return "observing";
        case ConsensusMode::wrongLedger:
            return "wrongLedger";
        case ConsensusMode::switchedLedger:
            return "switchedLedger";
        default:
            return "unknown";
    }
}

/** Phases of consensus for a single ledger round.

    @code
          "close"             "accept"
     open ------- > establish ---------> accepted
       ^               |                    |
       |---------------|                    |
       ^                     "startRound"   |
       |------------------------------------|
   @endcode

   The typical transition goes from open to establish to accepted and
   then a call to startRound begins the process anew. However, if a wrong prior
   ledger is detected and recovered during the establish or accept phase,
   consensus will internally go back to open (see Consensus::handleWrongLedger).
*/
enum class ConsensusPhase {
    //! We haven't closed our ledger yet, but others might have
    open,

    //! Establishing consensus by exchanging proposals with our peers
    establish,

    //! We have accepted a new last closed ledger and are waiting on a call
    //! to startRound to begin the next consensus round.  No changes
    //! to consensus phase occur while in this phase.
    accepted,
};

inline std::string
to_string(ConsensusPhase p)
{
    switch (p)
    {
        case ConsensusPhase::open:
            return "open";
        case ConsensusPhase::establish:
            return "establish";
        case ConsensusPhase::accepted:
            return "accepted";
        default:
            return "unknown";
    }
}

/** Measures the duration of phases of consensus
 */
class ConsensusTimer
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

/** Stores the set of initial close times

    The initial consensus proposal from each peer has that peer's view of
    when the ledger closed.  This object stores all those close times for
    analysis of clock drift between peerss.
*/
struct ConsensusCloseTimes
{
    //! Close time estimates, keep ordered for predictable traverse
    std::map<NetClock::time_point, int> peers;

    //! Our close time estimate
    NetClock::time_point self;
};

/** Whether we have or don't have a consensus */
enum class ConsensusState {
    No,       //!< We do not have consensus
    MovedOn,  //!< The network has consensus without us
    Yes       //!< We have consensus along with the network
};

/** Encapsulates the result of consensus.

    Stores all relevant data for the outcome of consensus on a single
   ledger.

    @tparam Traits Traits class defining the concrete consensus types used
                   by the application.
*/
template <class Traits>
struct ConsensusResult
{
    using Ledger_t = typename Traits::Ledger_t;
    using TxSet_t = typename Traits::TxSet_t;
    using NodeID_t = typename Traits::NodeID_t;

    using Tx_t = typename TxSet_t::Tx;
    using Proposal_t = ConsensusProposal<
        NodeID_t,
        typename Ledger_t::ID,
        typename TxSet_t::ID>;
    using Dispute_t = DisputedTx<Tx_t, NodeID_t>;

    ConsensusResult(TxSet_t&& s, Proposal_t&& p)
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
    ConsensusTimer roundTime;

    // Indicates state in which consensus ended.  Once in the accept phase
    // will be either Yes or MovedOn
    ConsensusState state = ConsensusState::No;

    // The number of peers proposing during the round
    std::size_t proposers = 0;
};
}  // namespace ripple

#endif

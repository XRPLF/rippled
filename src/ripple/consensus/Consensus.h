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
public:
    using clock_type = beast::abstract_clock <std::chrono::steady_clock>;

    using NetTime_t = typename Traits::NetTime_t;
    using Ledger_t = typename Traits::Ledger_t;
    using Proposal_t = typename Traits::Proposal_t;
    using TxSet_t = typename Traits::TxSet_t;
    using Tx_t = typename TxSet_t::Tx;

    Consensus(Consensus const&) = delete;
    Consensus& operator=(Consensus const&) = delete;
    ~Consensus () = default;

	/** Constructor.

	    @param clock The clock used to internally measure consensus progress
		@param j The journal to log debug output
	*/
	Consensus(clock_type const & clock, beast::Journal j);

private:
    //! Clock for measuring consensus progress
    clock_type const & clock_;

    //! Journal for debugging
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

} // ripple

#endif

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

#ifndef RIPPLE_APP_LEDGER_IMPL_CONSENSUS_H_INCLUDED
#define RIPPLE_APP_LEDGER_IMPL_CONSENSUS_H_INCLUDED

#include <ripple/app/ledger/LedgerConsensus.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/InboundTransactions.h>
#include <ripple/app/tx/LocalTxs.h>

#include <beast/cxx14/memory.h> // <memory>

namespace ripple {

/** Implements the consensus process and provides inter-round state. */
class Consensus
{
public:
    virtual
    ~Consensus () = default;

    /** */
    virtual
    void
    setProposing (bool p, bool v) = 0;

    /** */
    virtual
    bool
    isProposing () const = 0;

    /** */
    virtual
    bool
    isValidating () const = 0;

    virtual
    STValidation::ref
    getLastValidation () const = 0;

    virtual
    void
    setLastValidation (STValidation::ref v) = 0;

    virtual
    int
    getLastCloseProposers () const = 0;

    virtual
    int
    getLastCloseDuration () const = 0;

    /** Called when a new ledger is closed by a round

        @param proposers the number of participants in the consensus round
        @param convergeTime the time the consensus round took
        @param ledgerHash the hash of the ledger that just closed
    */
    virtual
    void
    newLCL (
        int proposers,
        int convergeTime,
        uint256 const& ledgerHash) = 0;

    /** Called when a new round of consensus is about to begin */
    virtual
    std::shared_ptr<LedgerConsensus>
    startRound (
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerHash const &prevLCLHash,
        Ledger::ref previousLedger,
        std::uint32_t closeTime,
        FeeVote& feeVote) = 0;

    /** Use *only* to timestamp our own validation */
    virtual
    std::uint32_t
    validationTimestamp () = 0;

    virtual
    std::uint32_t
    getLastCloseTime () const = 0;

    virtual
    void
    setLastCloseTime (std::uint32_t t) = 0;
};

std::unique_ptr<Consensus>
make_Consensus (NetworkOPs& netops);

}

#endif

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

#ifndef RIPPLE_APP_LEDGER_IMPL_CONSENSUSIMP_H_INCLUDED
#define RIPPLE_APP_LEDGER_IMPL_CONSENSUSIMP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/ledger/Consensus.h>
#include <ripple/app/ledger/LedgerConsensus.h>
#include <beast/utility/Journal.h>

namespace ripple {

/** Implements the consensus process and provides inter-round state. */
class ConsensusImp
    : public Consensus
{
public:
    ConsensusImp (NetworkOPs& netops);

    ~ConsensusImp () = default;

    bool
    isProposing () const override;

    bool
    isValidating () const override;

    int
    getLastCloseProposers () const override;

    int
    getLastCloseDuration () const override;

    std::shared_ptr<LedgerConsensus>
    startRound (
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerHash const &prevLCLHash,
        Ledger::ref previousLedger,
        std::uint32_t closeTime,
        FeeVote& feeVote) override;

    void
    setProposing (bool p, bool v);

    STValidation::ref
    getLastValidation () const;

    void
    setLastValidation (STValidation::ref v);

    void
    newLCL (
        int proposers,
        int convergeTime,
        uint256 const& ledgerHash);

    std::uint32_t
    validationTimestamp ();

    std::uint32_t
    getLastCloseTime () const;

    void
    setLastCloseTime (std::uint32_t t);

private:
    beast::Journal journal_;

    NetworkOPs& netops_;

    bool proposing_;
    bool validating_;

    // A pointer to the last validation that we issued
    STValidation::pointer lastValidation_;

    // The number of proposers who participated in the last ledger close
    int lastCloseProposers_;

    // How long the last ledger close took, in milliseconds
    int lastCloseConvergeTook_;

    // The hash of the last closed ledger
    uint256 lastCloseHash_;

    // The timestamp of the last validation we used, in network time. This is
    // only used for our own validations.
    std::uint32_t lastValidationTimestamp_;

    // The last close time
    std::uint32_t lastCloseTime_;
};

}

#endif

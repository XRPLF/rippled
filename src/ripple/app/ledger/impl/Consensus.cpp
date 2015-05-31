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

#include <BeastConfig.h>
#include <ripple/app/ledger/Consensus.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <beast/utility/Journal.h>

namespace ripple {

/** Implements the consensus process and provides inter-round state. */
class ConsensusImp
    : public Consensus
{
public:
    ConsensusImp (NetworkOPs& netops)
        : journal_ (deprecatedLogs().journal("Consensus"))
        , netops_ (netops)
        , proposing_ (false)
        , validating_ (false)
        , lastCloseProposers_ (0)
        , lastCloseConvergeTook_ (1000 * LEDGER_IDLE_INTERVAL)
        , lastValidationTimestamp_ (0)
        , lastCloseTime_ (0)
    {
    }

    ~ConsensusImp ()
    {
    }

    void
    setProposing (bool p, bool v) override
    {
        proposing_ = p;
        validating_ = v;
    }

    bool
    isProposing () const override
    {
        return proposing_;
    }

    bool
    isValidating () const override
    {
        return validating_;
    }

    STValidation::ref
    getLastValidation () const override
    {
        return lastValidation_;
    }

    void
    setLastValidation (STValidation::ref v) override
    {
        lastValidation_ = v;
    }

    virtual
    int
    getLastCloseProposers () const override
    {
        return lastCloseProposers_;
    }

    virtual
    int
    getLastCloseDuration () const override
    {
        return lastCloseConvergeTook_;
    }

    void
    newLCL (
        int proposers,
        int convergeTime,
        uint256 const& ledgerHash) override
    {
        lastCloseProposers_ = proposers;
        lastCloseConvergeTook_ = convergeTime;
        lastCloseHash_ = ledgerHash;
    }

    std::shared_ptr<LedgerConsensus>
    startRound (
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerHash const &prevLCLHash,
        Ledger::ref previousLedger,
        std::uint32_t closeTime,
        FeeVote& feeVote) override
    {
        return make_LedgerConsensus (*this, lastCloseProposers_,
            lastCloseConvergeTook_, inboundTransactions, localtx,
            prevLCLHash, previousLedger, closeTime, feeVote);
    }

    std::uint32_t
    validationTimestamp () override
    {
        std::uint32_t vt = netops_.getNetworkTimeNC ();

        if (vt <= lastValidationTimestamp_)
            vt = lastValidationTimestamp_ + 1;

        lastValidationTimestamp_ = vt;
        return vt;
    }

    std::uint32_t
    getLastCloseTime () const override
    {
        return lastCloseTime_;
    }

    void
    setLastCloseTime (std::uint32_t t) override
    {
        lastCloseTime_ = t;
    }

private:
    beast::Journal journal_;

    NetworkOPs& netops_;

    bool proposing_;
    bool validating_;

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

std::unique_ptr<Consensus>
make_Consensus (NetworkOPs& netops)
{
    return std::make_unique<ConsensusImp> (netops);
}

}

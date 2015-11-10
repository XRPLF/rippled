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
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/ledger/impl/ConsensusImp.h>
#include <ripple/app/ledger/impl/LedgerConsensusImp.h>

namespace ripple {

ConsensusImp::ConsensusImp (
        FeeVote::Setup const& voteSetup,
        Logs& logs)
    : journal_ (logs.journal("Consensus"))
    , feeVote_ (make_FeeVote (voteSetup,
        logs.journal("FeeVote")))
    , proposing_ (false)
    , validating_ (false)
    , lastCloseProposers_ (0)
    , lastCloseConvergeTook_ (1000 * LEDGER_IDLE_INTERVAL)
    , lastValidationTimestamp_ (0)
    , lastCloseTime_ (0)
{
}

bool
ConsensusImp::isProposing () const
{
    return proposing_;
}

bool
ConsensusImp::isValidating () const
{
    return validating_;
}

int
ConsensusImp::getLastCloseProposers () const
{
    return lastCloseProposers_;
}

int
ConsensusImp::getLastCloseDuration () const
{
    return lastCloseConvergeTook_;
}

std::shared_ptr<LedgerConsensus>
ConsensusImp::startRound (
    Application& app,
    InboundTransactions& inboundTransactions,
    LocalTxs& localtx,
    LedgerMaster& ledgerMaster,
    LedgerHash const &prevLCLHash,
    Ledger::ref previousLedger,
    std::uint32_t closeTime)
{
    return make_LedgerConsensus (app, *this, lastCloseProposers_,
        lastCloseConvergeTook_, inboundTransactions, localtx, ledgerMaster,
        prevLCLHash, previousLedger, closeTime, *feeVote_);
}


void
ConsensusImp::setProposing (bool p, bool v)
{
    proposing_ = p;
    validating_ = v;
}

STValidation::ref
ConsensusImp::getLastValidation () const
{
    return lastValidation_;
}

void
ConsensusImp::setLastValidation (STValidation::ref v)
{
    lastValidation_ = v;
}

void
ConsensusImp::newLCL (
    int proposers,
    int convergeTime,
    uint256 const& ledgerHash)
{
    lastCloseProposers_ = proposers;
    lastCloseConvergeTook_ = convergeTime;
    lastCloseHash_ = ledgerHash;
}

std::uint32_t
ConsensusImp::validationTimestamp (std::uint32_t vt)
{
    if (vt <= lastValidationTimestamp_)
        vt = lastValidationTimestamp_ + 1;

    lastValidationTimestamp_ = vt;
    return vt;
}

std::uint32_t
ConsensusImp::getLastCloseTime () const
{
    return lastCloseTime_;
}

void
ConsensusImp::setLastCloseTime (std::uint32_t t)
{
    lastCloseTime_ = t;
}

void
ConsensusImp::storeProposal (
    LedgerProposal::ref proposal,
    RippleAddress const& peerPublic)
{
    auto& props = storedProposals_[peerPublic.getNodeID ()];

    if (props.size () >= 10)
        props.pop_front ();

    props.push_back (proposal);
}

// Must be called while holding the master lock
void
ConsensusImp::takePosition (int seq, std::shared_ptr<SHAMap> const& position)
{
    recentPositions_[position->getHash ().as_uint256()] = std::make_pair (seq, position);

    if (recentPositions_.size () > 4)
    {
        for (auto i = recentPositions_.begin (); i != recentPositions_.end ();)
        {
            if (i->second.first < (seq - 2))
            {
                recentPositions_.erase (i);
                return;
            }

            ++i;
        }
    }
}

Consensus::Proposals&
ConsensusImp::peekStoredProposals ()
{
    return storedProposals_;
}

//==============================================================================

std::unique_ptr<Consensus>
make_Consensus (Config const& config, Logs& logs)
{
    return std::make_unique<ConsensusImp> (
        setup_FeeVote (config.section ("voting")),
        logs);
}

}

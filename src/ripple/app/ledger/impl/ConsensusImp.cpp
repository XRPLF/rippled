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
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/app/ledger/impl/ConsensusImp.h>
#include <ripple/app/ledger/impl/LedgerConsensusImp.h>

namespace ripple {



void
ConsensusImp::newLCL (
    int proposers,
    std::chrono::milliseconds convergeTime)
{
    lastCloseProposers_ = proposers;
    lastCloseConvergeTook_ = convergeTime;
}

void
ConsensusImp::storeProposal (
    LedgerProposal::ref proposal,
    NodeID const& nodeID)
{
    std::lock_guard <std::mutex> _(lock_);

    auto& props = storedProposals_[nodeID];

    if (props.size () >= 10)
        props.pop_front ();

    props.push_back (proposal);
}

std::vector <LedgerProposal>
ConsensusImp::getStoredProposals (uint256 const& prevLedger)
{

    std::vector <LedgerProposal> ret;

    {
        std::lock_guard <std::mutex> _(lock_);

        for (auto const& it : storedProposals_)
            for (auto const& prop : it.second)
                if (prop->prevLedger() == prevLedger)
                    ret.emplace_back (*prop);
    }

    return ret;
}

}

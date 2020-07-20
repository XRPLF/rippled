//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerReplay.h>

namespace ripple {

LedgerReplay::LedgerReplay(
    std::shared_ptr<Ledger const> parent,
    std::shared_ptr<Ledger const> replay)
    : parent_{std::move(parent)}, replay_{std::move(replay)}
{
    for (auto const& item : replay_->txMap())
    {
        auto txPair = replay_->txRead(item.key());  // non-const so can be moved
        auto const txIndex = (*txPair.second)[sfTransactionIndex];
        orderedTxns_.emplace(txIndex, std::move(txPair.first));
    }
}

LedgerReplay::LedgerReplay(
    std::shared_ptr<Ledger const> parent,
    std::shared_ptr<Ledger const> replay,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns)
    : parent_{std::move(parent)}
    , replay_{std::move(replay)}
    , orderedTxns_{std::move(orderedTxns)}
{
}

}  // namespace ripple

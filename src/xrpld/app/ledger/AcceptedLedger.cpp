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

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/main/Application.h>
#include <algorithm>

namespace ripple {

AcceptedLedger::AcceptedLedger(
    std::shared_ptr<ReadView const> const& ledger,
    Application& app)
    : mLedger(ledger)
{
    transactions_.reserve(256);

    auto insertAll = [&](auto const& txns) {
        for (auto const& item : txns)
            transactions_.emplace_back(std::make_unique<AcceptedLedgerTx>(
                ledger, item.first, item.second));
    };

    if (app.config().reporting())
    {
        auto const txs = flatFetchTransactions(*ledger, app);
        transactions_.reserve(txs.size());
        insertAll(txs);
    }
    else
    {
        transactions_.reserve(256);
        insertAll(ledger->txs);
    }

    std::sort(
        transactions_.begin(),
        transactions_.end(),
        [](auto const& a, auto const& b) {
            return a->getTxnSeq() < b->getTxnSeq();
        });
}

}  // namespace ripple

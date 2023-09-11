//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_SYNC_REPLAYLEDGER_H_INCLUDED
#define RIPPLE_SYNC_REPLAYLEDGER_H_INCLUDED

#include <ripple/basics/promises.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/protocol/LedgerHeader.h>
#include <ripple/sync/LedgerGetter.h>

namespace ripple {
namespace sync {

class ReplayLedger
{
private:
    LedgerGetter& getter_;
    beast::Journal journal_;
    LedgerDigest digest_;
    FuturePtr<LedgerHeader> header_;
    FuturePtr<TxSet> txSet_;
    FuturePtr<ConstLedgerPtr> parent_;

public:
    ReplayLedger(
        Application& app,
        LedgerGetter& getter,
        LedgerDigest&& digest,
        FuturePtr<LedgerHeader>&& header,
        FuturePtr<TxSet>&& txSet,
        FuturePtr<ConstLedgerPtr>&& parent)
        : getter_(getter)
        , journal_(app.journal("ReplayLedger"))
        , digest_(std::move(digest))
        , header_(std::move(header))
        , txSet_(std::move(txSet))
        , parent_(std::move(parent))
    {
    }

    LedgerFuturePtr
    start();

private:
    ConstLedgerPtr
    build(
        ConstLedgerPtr const& parent,
        LedgerHeader const& header,
        TxSet const& txSet);
};

}  // namespace sync
}  // namespace ripple

#endif

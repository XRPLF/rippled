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

#include <ripple/sync/ReplayLedger.h>

#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/basics/utility.h>

#include <cassert>
#include <functional>

namespace ripple {
// REVIEW: When should we introduce nested namespaces?
namespace sync {

LedgerFuturePtr
ReplayLedger::start()
{
    JLOG(journal_.info()) << digest_ << " start";
    return getter_.jscheduler_
        .apply(
            std::bind_front(&ReplayLedger::build, this),
            parent_,
            header_,
            txSet_)
        // TODO: Add `or_else` method to `AsyncPromise`.
        // TODO: Consider renaming `then` to `and_then` like `std::optional`.
        ->then([this](auto const& ledgerf) {
            return ledgerf->rejected() ? getter_.copy(std::move(digest_))
                                       : ledgerf;
        });
}

ConstLedgerPtr
ReplayLedger::build(
    ConstLedgerPtr const& parent,
    LedgerHeader const& header,
    TxSet const& txSet)
{
    assert(parent);
    auto& app = getter_.app_;
    auto stub =
        std::make_shared<Ledger>(header, app.config(), app.getNodeFamily());
    LedgerReplay replayData(parent, stub, ripple::copy(txSet));
    auto child =
        buildLedger(replayData, tapNONE, app, app.journal("buildLedger"));
    if (child->info().hash != digest_)
    {
        throw std::runtime_error("built wrong ledger");
    }
    JLOG(journal_.info()) << digest_ << " finish";
    return child;
}

}  // namespace sync
}  // namespace ripple

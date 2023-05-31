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

#ifndef RIPPLE_SYNC_REPLAYLEDGERS_H_INCLUDED
#define RIPPLE_SYNC_REPLAYLEDGERS_H_INCLUDED

#include <ripple/ledger/LedgerHeader.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/peerclient/PeerClient.h>
#include <ripple/sync/LedgerGetter.h>

namespace ripple {
namespace sync {

class ReplayLedgers
{
private:
    LedgerGetter& getter_;
    LedgerDigest digest_;
    FuturePtr<LedgerHeader> header_;
    FuturePtr<TxSet> txSet_;
    // Cumulative skip list.
    // This one is ordered newest to oldest so that we can insert at the end.
    SkipList skipList_;
    // How many skip lists to search for an ancestor.
    // TODO: Make this configurable.
    unsigned int limit_ = 4;

public:
    ReplayLedgers(Application& app, LedgerGetter& getter, LedgerDigest&& digest)
        : getter_(getter), digest_(std::move(digest))
    {
    }

    LedgerFuturePtr
    start();

private:
    LedgerFuturePtr
    withHeader(LedgerHeader const& header);

    LedgerFuturePtr
    withSkipList(SkipList const& skipList);
};

}  // namespace sync
}  // namespace ripple

#endif

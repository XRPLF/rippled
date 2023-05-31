//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef RIPPLE_SYNC_LEDGERGETTER_H_INCLUDED
#define RIPPLE_SYNC_LEDGERGETTER_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/JobScheduler.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/peerclient/PeerClient.h>

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace ripple {

class Application;
class LedgerMaster;
class MessageScheduler;

namespace sync {

// REVIEW: Rename to `LedgerPtrFuturePtr`? Eliminate?
using LedgerFuturePtr = FuturePtr<ConstLedgerPtr>;

class LedgerGetter
{
private:
    beast::Journal journal_;
    JobScheduler jscheduler_;
    PeerClient peerClient_;
    LedgerMaster& ledgerMaster_;
    Application& app_;
    std::mutex mutex_;
    hash_map<LedgerDigest, LedgerFuturePtr> cache_;

    friend class ReplayLedger;
    friend class ReplayLedgers;

public:
    LedgerGetter(Application& app);

    LedgerFuturePtr
    get(LedgerDigest const& digest);

    LedgerFuturePtr
    get(LedgerIdentifier const& id)
    {
        return get(id.digest);
    }

private:
    /** Replay one or more ledgers. */
    LedgerFuturePtr
    replay(LedgerDigest&& digest);

    /** Replay exactly one ledger. */
    LedgerFuturePtr
    replay(
        LedgerDigest&& digest,
        FuturePtr<LedgerHeader>&& header,
        FuturePtr<TxSet>&& txSet,
        LedgerFuturePtr&& parent);

    LedgerFuturePtr
    copy(LedgerDigest&& digest);

    LedgerFuturePtr
    find(LedgerDigest const& digest);
};

}  // namespace sync
}  // namespace ripple

#endif

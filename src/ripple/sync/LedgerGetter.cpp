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

#include <ripple/sync/LedgerGetter.h>

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/Coroutine.h>
#include <ripple/basics/promises.h>
#include <ripple/basics/utility.h>
#include <ripple/peerclient/MessageScheduler.h>
#include <ripple/sync/CopyLedger.h>
#include <ripple/sync/ReplayLedger.h>
#include <ripple/sync/ReplayLedgers.h>

#include <array>
#include <exception>
#include <sstream>
#include <stdexcept>

namespace ripple {
namespace sync {

LedgerGetter::LedgerGetter(Application& app)
    : journal_(app.journal("LedgerGetter"))
    , jscheduler_(app.getJobQueue(), jtLEDGER_DATA, "LedgerGetter")
    , peerClient_(app, jscheduler_)
    , ledgerMaster_(app.getLedgerMaster())
    , app_(app)
{
}

LedgerFuturePtr
LedgerGetter::get(LedgerDigest const& digest)
{
    LedgerFuturePtr output;

    auto ledger = ledgerMaster_.getLedgerByHash(digest, /*acquire=*/false);
    if (ledger)
    {
        output = jscheduler_.fulfilled<ConstLedgerPtr>(ledger);
        JLOG(journal_.trace()) << "in database";
        return output;
    }

    // Lock the mutex and look in the cache.
    // If it has a future, then return it.
    // Otherwise, cache a pending future, unlock the mutex,
    // and then work on fulfilling the future.
    {
        std::lock_guard lock(mutex_);
        auto& found = cache_[digest];
        if (found)
        {
            return found;
        }
        output = found = jscheduler_.pending<ConstLedgerPtr>();
    }

    JLOG(journal_.trace()) << "get(" << digest;

    this->replay(ripple::copy(digest))
        ->thenv([this, digest](auto const& ledger) {
            if (!ledger)
            {
                throw std::runtime_error("returned a null pointer");
            }

            auto actualDigest = ledger->info().hash;
            if (actualDigest != digest)
            {
                std::stringstream stream;
                stream << "built wrong ledger. expected " << digest
                       << " but got " << actualDigest;
                throw std::runtime_error(stream.str());
            }

            ledgerMaster_.storeLedger(ledger);
            ledgerMaster_.checkAccept(ledger);

            auto ledger2 =
                ledgerMaster_.getLedgerByHash(digest, /*acquire=*/false);
            if (!ledger2)
            {
                throw std::runtime_error("ledger master could not find");
            }

            if (ledger != ledger2)
            {
                JLOG(journal_.error()) << "ledgers not deduplicated";
            }
            if (ledger->info().hash != ledger2->info().hash)
            {
                JLOG(journal_.error()) << "ledgers not identical";
            }

            JLOG(journal_.trace()) << "done," << actualDigest;
            return ledger;
        })
        ->link(output);

    output->subscribe([this, digest](auto const& ledgerf) {
        // Remove from the cache.
        {
            std::lock_guard lock(mutex_);
            cache_.erase(digest);
        }

        if (ledgerf->rejected())
        {
            JLOG(journal_.error()) << "failed to build ledger " << digest
                                   << ": " << ledgerf->message();
        }
    });

    return output;
}

LedgerFuturePtr
LedgerGetter::replay(LedgerDigest&& digest)
{
    return start<ReplayLedgers>(app_, *this, std::move(digest));
}

LedgerFuturePtr
LedgerGetter::replay(
    LedgerDigest&& digest,
    FuturePtr<LedgerHeader>&& header,
    FuturePtr<TxSet>&& txSet,
    LedgerFuturePtr&& parent)
{
    return start<ReplayLedger>(
        app_,
        *this,
        std::move(digest),
        std::move(header),
        std::move(txSet),
        std::move(parent));
}

LedgerFuturePtr
LedgerGetter::copy(LedgerDigest&& digest)
{
    return start<CopyLedger>(app_, jscheduler_, std::move(digest));
}

LedgerFuturePtr
LedgerGetter::find(LedgerDigest const& digest)
{
    LedgerFuturePtr output;
    {
        std::lock_guard lock(mutex_);
        auto it = cache_.find(digest);
        if (it != cache_.end())
        {
            output = it->second;
        }
    }
    if (!output)
    {
        auto ledger = ledgerMaster_.getLedgerByHash(digest, /*acquire=*/false);
        if (ledger)
        {
            output = jscheduler_.fulfilled<ConstLedgerPtr>(ledger);
        }
    }
    return output;
}

}  // namespace sync
}  // namespace ripple

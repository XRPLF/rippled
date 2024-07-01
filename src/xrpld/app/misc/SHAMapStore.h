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

#ifndef RIPPLE_APP_MISC_SHAMAPSTORE_H_INCLUDED
#define RIPPLE_APP_MISC_SHAMAPSTORE_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/protocol/ErrorCodes.h>
#include <optional>

namespace ripple {

class TransactionMaster;

/**
 * class to create database, launch online delete thread, and
 * related SQLite database
 */
class SHAMapStore
{
public:
    virtual ~SHAMapStore() = default;

    /** Called by LedgerMaster every time a ledger validates. */
    virtual void
    onLedgerClosed(std::shared_ptr<Ledger const> const& ledger) = 0;

    virtual void
    start() = 0;

    virtual void
    rendezvous() const = 0;

    virtual void
    stop() = 0;

    virtual std::uint32_t
    clampFetchDepth(std::uint32_t fetch_depth) const = 0;

    virtual std::unique_ptr<NodeStore::Database>
    makeNodeStore(int readThreads) = 0;

    /** Highest ledger that may be deleted. */
    virtual LedgerIndex
    setCanDelete(LedgerIndex canDelete) = 0;

    /** Whether advisory delete is enabled. */
    virtual bool
    advisoryDelete() const = 0;

    /** Maximum ledger that has been deleted, or will be deleted if
     *  currently in the act of online deletion.
     */
    virtual LedgerIndex
    getLastRotated() = 0;

    /** Highest ledger that may be deleted. */
    virtual LedgerIndex
    getCanDelete() = 0;

    /** Returns the number of file descriptors that are needed. */
    virtual int
    fdRequired() const = 0;

    /** The minimum ledger to try and maintain in our database.

        This defines the lower bound for attempting to acquire historical
        ledgers over the peer to peer network.

        If online_delete is enabled, then each time online_delete executes
        and just prior to clearing SQL databases of historical ledgers,
        move the value forward to one past the greatest ledger being deleted.
        This minimizes fetching of ledgers that are in the process of being
        deleted. Without online_delete or before online_delete is
        executed, this value is always the minimum value persisted in the
        ledger database, if any.

        @return The minimum ledger sequence to keep online based on the
            description above. If not set, then an unseated optional.
    */
    virtual std::optional<LedgerIndex>
    minimumOnline() const = 0;
};

//------------------------------------------------------------------------------

std::unique_ptr<SHAMapStore>
make_SHAMapStore(
    Application& app,
    NodeStore::Scheduler& scheduler,
    beast::Journal journal);
}  // namespace ripple

#endif

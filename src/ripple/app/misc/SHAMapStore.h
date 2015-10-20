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
#include <ripple/core/Config.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/Scheduler.h>
#include <ripple/protocol/ErrorCodes.h>
#include <beast/threads/Stoppable.h>

namespace ripple {

class TransactionMaster;

/**
 * class to create database, launch online delete thread, and
 * related sqlite databse
 */
class SHAMapStore
    : public beast::Stoppable
{
public:
    struct Setup
    {
        std::uint32_t deleteInterval = 0;
        bool advisoryDelete = false;
        std::uint32_t ledgerHistory = 0;
        Section nodeDatabase;
        std::string databasePath;
        std::uint32_t deleteBatch = 100;
        std::uint32_t backOff = 100;
        std::int32_t ageThreshold = 60;
    };

    SHAMapStore (Stoppable& parent) : Stoppable ("SHAMapStore", parent) {}

    /** Called by LedgerMaster every time a ledger validates. */
    virtual void onLedgerClosed (Ledger::pointer validatedLedger) = 0;

    virtual std::uint32_t clampFetchDepth (std::uint32_t fetch_depth) const = 0;

    virtual std::unique_ptr <NodeStore::Database> makeDatabase (
            std::string const& name, std::int32_t readThreads) = 0;

    /** Highest ledger that may be deleted. */
    virtual LedgerIndex setCanDelete (LedgerIndex canDelete) = 0;

    /** Whether advisory delete is enabled. */
    virtual bool advisoryDelete() const = 0;

    /** Last ledger which was copied during rotation of backends. */
    virtual LedgerIndex getLastRotated() = 0;

    /** Highest ledger that may be deleted. */
    virtual LedgerIndex getCanDelete() = 0;
};

//------------------------------------------------------------------------------

SHAMapStore::Setup
setup_SHAMapStore(Config const& c);

std::unique_ptr<SHAMapStore>
make_SHAMapStore(
    Application& app,
    SHAMapStore::Setup const& s,
    beast::Stoppable& parent,
    NodeStore::Scheduler& scheduler,
    beast::Journal journal,
    beast::Journal nodeStoreJournal,
    TransactionMaster& transactionMaster,
    BasicConfig const& conf);
}

#endif

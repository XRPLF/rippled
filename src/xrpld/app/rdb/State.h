//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_RDB_STATE_H_INCLUDED
#define RIPPLE_APP_RDB_STATE_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/core/Config.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/peerfinder/impl/Store.h>
#include <boost/filesystem.hpp>

namespace ripple {

struct SavedState
{
    std::string writableDb;
    std::string archiveDb;
    LedgerIndex lastRotated;
};

/**
 * @brief initStateDB Opens a session with the State database.
 * @param session Provides a session with the database.
 * @param config Path to the database and other opening parameters.
 * @param dbName Name of the database.
 */
void
initStateDB(
    soci::session& session,
    BasicConfig const& config,
    std::string const& dbName);

/**
 * @brief getCanDelete Returns the ledger sequence which can be deleted.
 * @param session Session with the database.
 * @return Ledger sequence.
 */
LedgerIndex
getCanDelete(soci::session& session);

/**
 * @brief setCanDelete Updates the ledger sequence which can be deleted.
 * @param session Session with the database.
 * @param canDelete Ledger sequence to save.
 * @return Previous value of the ledger sequence which can be deleted.
 */
LedgerIndex
setCanDelete(soci::session& session, LedgerIndex canDelete);

/**
 * @brief getSavedState Returns the saved state.
 * @param session Session with the database.
 * @return The SavedState structure which contains the names of the writable
 *         database, the archive database and the last rotated ledger sequence.
 */
SavedState
getSavedState(soci::session& session);

/**
 * @brief setSavedState Saves the given state.
 * @param session Session with the database.
 * @param state The SavedState structure which contains the names of the
 *        writable database, the archive database and the last rotated ledger
 *        sequence.
 */
void
setSavedState(soci::session& session, SavedState const& state);

/**
 * @brief setLastRotated Updates the last rotated ledger sequence.
 * @param session Session with the database.
 * @param seq New value of the last rotated ledger sequence.
 */
void
setLastRotated(soci::session& session, LedgerIndex seq);

}  // namespace ripple

#endif

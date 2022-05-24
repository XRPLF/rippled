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

#include <ripple/app/rdb/State.h>

namespace ripple {

void
initStateDB(
    soci::session& session,
    BasicConfig const& config,
    std::string const& dbName)
{
    open(session, config, dbName);

    session << "PRAGMA synchronous=FULL;";

    session << "CREATE TABLE IF NOT EXISTS DbState ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  WritableDb             TEXT,"
               "  ArchiveDb              TEXT,"
               "  LastRotatedLedger      INTEGER"
               ");";

    session << "CREATE TABLE IF NOT EXISTS CanDelete ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  CanDeleteSeq           INTEGER"
               ");";

    std::int64_t count = 0;
    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM DbState WHERE Key = 1;",
            soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>(
                "Failed to fetch Key Count from DbState.");
        count = *countO;
    }

    if (!count)
    {
        session << "INSERT INTO DbState VALUES (1, '', '', 0);";
    }

    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM CanDelete WHERE Key = 1;",
            soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>(
                "Failed to fetch Key Count from CanDelete.");
        count = *countO;
    }

    if (!count)
    {
        session << "INSERT INTO CanDelete VALUES (1, 0);";
    }
}

LedgerIndex
getCanDelete(soci::session& session)
{
    LedgerIndex seq;
    session << "SELECT CanDeleteSeq FROM CanDelete WHERE Key = 1;",
        soci::into(seq);
    ;
    return seq;
}

LedgerIndex
setCanDelete(soci::session& session, LedgerIndex canDelete)
{
    session << "UPDATE CanDelete SET CanDeleteSeq = :canDelete WHERE Key = 1;",
        soci::use(canDelete);
    return canDelete;
}

SavedState
getSavedState(soci::session& session)
{
    SavedState state;
    session << "SELECT WritableDb, ArchiveDb, LastRotatedLedger"
               " FROM DbState WHERE Key = 1;",
        soci::into(state.writableDb), soci::into(state.archiveDb),
        soci::into(state.lastRotated);

    return state;
}

void
setSavedState(soci::session& session, SavedState const& state)
{
    session << "UPDATE DbState"
               " SET WritableDb = :writableDb,"
               " ArchiveDb = :archiveDb,"
               " LastRotatedLedger = :lastRotated"
               " WHERE Key = 1;",
        soci::use(state.writableDb), soci::use(state.archiveDb),
        soci::use(state.lastRotated);
}

void
setLastRotated(soci::session& session, LedgerIndex seq)
{
    session << "UPDATE DbState SET LastRotatedLedger = :seq"
               " WHERE Key = 1;",
        soci::use(seq);
}

}  // namespace ripple

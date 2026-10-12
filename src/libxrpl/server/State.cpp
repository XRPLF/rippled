#include <xrpl/server/State.h>

#include <xrpl/basics/contract.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/rdb/SociDB.h>

#include <boost/optional/optional.hpp>  // IWYU pragma: keep

#include <soci/boost-optional.h>  // IWYU pragma: keep
#include <soci/into.h>
#include <soci/rowset.h>
#include <soci/session.h>
#include <soci/transaction.h>
#include <soci/use.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace xrpl {

void
initStateDB(soci::session& session, BasicConfig const& config, std::string const& dbName)
{
    open(session, config, dbName);

    session << "PRAGMA synchronous=FULL;";

    session << "CREATE TABLE IF NOT EXISTS DbState ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  WritableDb             TEXT,"
               "  ArchiveDb              TEXT,"
               "  LastRotatedLedger      INTEGER"
               ");";

    // The online_delete generation ring, one row per generation ordered oldest ->
    // newest by Ordinal. A dedicated table (rather than a delimited column) round-trips
    // arbitrary backend directory names with no escaping and is rewritten atomically
    // in a transaction by setSavedState. Absent/empty for an older two-backend state,
    // in which case getSavedState reconstructs the ring from {ArchiveDb, WritableDb}.
    session << "CREATE TABLE IF NOT EXISTS DbGenerations ("
               "  Ordinal                INTEGER PRIMARY KEY,"
               "  Name                   TEXT NOT NULL"
               ");";

    session << "CREATE TABLE IF NOT EXISTS CanDelete ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  CanDeleteSeq           INTEGER"
               ");";

    std::int64_t count = 0;
    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM DbState WHERE Key = 1;", soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>("Failed to fetch Key Count from DbState.");
        count = *countO;
    }

    if (count == 0)
    {
        session << "INSERT INTO DbState VALUES (1, '', '', 0);";
    }

    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM CanDelete WHERE Key = 1;", soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>("Failed to fetch Key Count from CanDelete.");
        count = *countO;
    }

    if (count == 0)
    {
        session << "INSERT INTO CanDelete VALUES (1, 0);";
    }
}

LedgerIndex
getCanDelete(soci::session& session)
{
    LedgerIndex seq = 0;
    session << "SELECT CanDeleteSeq FROM CanDelete WHERE Key = 1;", soci::into(seq);
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
        soci::into(state.writableDb), soci::into(state.archiveDb), soci::into(state.lastRotated);

    // Read the generation ring, oldest -> newest.
    soci::rowset<std::string> const rs =
        (session.prepare << "SELECT Name FROM DbGenerations ORDER BY Ordinal ASC;");
    for (auto const& name : rs)
        state.generations.push_back(name);

    // Stale ring detection: setSavedState always writes the pair as the ring ends, so
    // a mismatch means an older two-backend build rotated after this ring was written
    // (it updates the pair but not DbGenerations, and deletes middle-generation
    // directories as orphans). Trust the pair; the stale rows are rewritten on the
    // next setSavedState.
    if (!state.generations.empty() &&
        (state.generations.back() != state.writableDb ||
         state.generations.front() != state.archiveDb))
    {
        state.generations.clear();
    }

    // Legacy two-backend state (written before the generation ring existed, or
    // invalidated above): no usable rows in DbGenerations but the pair is populated.
    // Reconstruct the ring oldest -> newest so boot opens the same on-disk backends.
    if (state.generations.empty() && !state.writableDb.empty())
    {
        if (!state.archiveDb.empty())
            state.generations.push_back(state.archiveDb);
        state.generations.push_back(state.writableDb);
    }

    return state;
}

void
setSavedState(soci::session& session, SavedState const& state)
{
    // Rewrite the state row and the whole generation ring atomically: a crash between
    // the two would otherwise leave the persisted ring inconsistent with the pair,
    // and on restart the node could open the wrong backends (missing nodes).
    soci::transaction tr(session);

    session << "UPDATE DbState"
               " SET WritableDb = :writableDb,"
               " ArchiveDb = :archiveDb,"
               " LastRotatedLedger = :lastRotated"
               " WHERE Key = 1;",
        soci::use(state.writableDb), soci::use(state.archiveDb), soci::use(state.lastRotated);

    session << "DELETE FROM DbGenerations;";
    for (std::size_t i = 0; i < state.generations.size(); ++i)
    {
        auto const ordinal = static_cast<std::int64_t>(i);
        auto const& name = state.generations[i];
        session << "INSERT INTO DbGenerations (Ordinal, Name) VALUES (:ordinal, :name);",
            soci::use(ordinal), soci::use(name);
    }

    tr.commit();
}

void
setLastRotated(soci::session& session, LedgerIndex seq)
{
    session << "UPDATE DbState SET LastRotatedLedger = :seq"
               " WHERE Key = 1;",
        soci::use(seq);
}

}  // namespace xrpl

#pragma once

#include <xrpl/protocol/Protocol.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/rdb/SociDB.h>

#include <string>
#include <vector>

namespace xrpl {

struct SavedState
{
    // Legacy two-backend fields. Retained for on-disk back-compat: a state written
    // by an older (two-backend) build has only these. On read, an empty `generations`
    // is reconstructed as {archiveDb, writableDb} (oldest -> newest). On write, these
    // are kept in sync with the ring ends (archiveDb = generations.front(),
    // writableDb = generations.back()). CAUTION: a downgraded build boots from the
    // pair alone — it deletes middle-generation directories as orphans (losing any
    // node whose only copy lives there) and its rotations leave DbGenerations rows
    // stale; getSavedState detects that staleness (ring ends disagreeing with the
    // pair) and falls back to the pair.
    std::string writableDb;
    std::string archiveDb;
    LedgerIndex lastRotated{};

    // The online_delete generation ring, ordered oldest -> newest. New nodes are
    // written to generations.back() (the writable generation); reads probe
    // newest -> oldest. Persisted one row per generation in the DbGenerations
    // table, ordered by position, so a variable number of generations round-trips.
    std::vector<std::string> generations;
};

/**
 * @brief initStateDB Opens a session with the State database.
 * @param session Provides a session with the database.
 * @param config Path to the database and other opening parameters.
 * @param dbName Name of the database.
 */
void
initStateDB(soci::session& session, BasicConfig const& config, std::string const& dbName);

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

}  // namespace xrpl

#ifndef XRPL_APP_RDB_STATE_H_INCLUDED
#define XRPL_APP_RDB_STATE_H_INCLUDED

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/misc/Manifest.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/DatabaseCon.h>
#include <xrpld/peerfinder/detail/Store.h>

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

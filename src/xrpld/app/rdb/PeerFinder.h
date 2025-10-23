#ifndef XRPL_APP_RDB_PEERFINDER_H_INCLUDED
#define XRPL_APP_RDB_PEERFINDER_H_INCLUDED

#include <xrpld/core/Config.h>
#include <xrpld/core/DatabaseCon.h>
#include <xrpld/peerfinder/detail/Store.h>

namespace ripple {

/**
 * @brief initPeerFinderDB Opens a session with the peer finder database.
 * @param session Session with the peer finder database.
 * @param config Path to the database and other opening parameters.
 * @param j Journal.
 */
void
initPeerFinderDB(
    soci::session& session,
    BasicConfig const& config,
    beast::Journal j);

/**
 * @brief updatePeerFinderDB Updates the peer finder database to a new version.
 * @param session Session with the database.
 * @param currentSchemaVersion New version of the database.
 * @param j Journal.
 */
void
updatePeerFinderDB(
    soci::session& session,
    int currentSchemaVersion,
    beast::Journal j);

/**
 * @brief readPeerFinderDB Reads all entries from the peer finder database and
 *        invokes the given callback for each entry.
 * @param session Session with the database.
 * @param func Callback to invoke for each entry.
 */
void
readPeerFinderDB(
    soci::session& session,
    std::function<void(std::string const&, int)> const& func);

/**
 * @brief savePeerFinderDB Saves a new entry to the peer finder database.
 * @param session Session with the database.
 * @param v Entry to save which contains information about a new peer.
 */
void
savePeerFinderDB(
    soci::session& session,
    std::vector<PeerFinder::Store::Entry> const& v);

}  // namespace ripple

#endif

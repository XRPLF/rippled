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

#ifndef RIPPLE_APP_RDB_PEERFINDER_H_INCLUDED
#define RIPPLE_APP_RDB_PEERFINDER_H_INCLUDED

#include <ripple/core/Config.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/peerfinder/impl/Store.h>

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

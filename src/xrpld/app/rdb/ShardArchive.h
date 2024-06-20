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

#ifndef RIPPLE_APP_RDB_SHARDARCHIVE_H_INCLUDED
#define RIPPLE_APP_RDB_SHARDARCHIVE_H_INCLUDED

#include <ripple/core/DatabaseCon.h>
#include <boost/filesystem.hpp>

namespace ripple {

/**
 * @brief makeArchiveDB Opens the shard archive database and returns its
 *        descriptor.
 * @param dir Path to the database to open.
 * @param dbName Name of the database.
 * @return Unique pointer to the opened database.
 */
std::unique_ptr<DatabaseCon>
makeArchiveDB(boost::filesystem::path const& dir, std::string const& dbName);

/**
 * @brief readArchiveDB Reads entries from the shard archive database and
 *        invokes the given callback for each entry.
 * @param db Session with the database.
 * @param func Callback to invoke for each entry.
 */
void
readArchiveDB(
    DatabaseCon& db,
    std::function<void(std::string const&, int)> const& func);

/**
 * @brief insertArchiveDB Adds an entry to the shard archive database.
 * @param db Session with the database.
 * @param shardIndex Shard index to add.
 * @param url Shard download url to add.
 */
void
insertArchiveDB(
    DatabaseCon& db,
    std::uint32_t shardIndex,
    std::string const& url);

/**
 * @brief deleteFromArchiveDB Deletes an entry from the shard archive database.
 * @param db Session with the database.
 * @param shardIndex Shard index to remove from the database.
 */
void
deleteFromArchiveDB(DatabaseCon& db, std::uint32_t shardIndex);

/**
 * @brief dropArchiveDB Removes a table in the shard archive database.
 * @param db Session with the database.
 */
void
dropArchiveDB(DatabaseCon& db);

}  // namespace ripple

#endif

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

#include <ripple/app/rdb/ShardArchive.h>

namespace ripple {

std::unique_ptr<DatabaseCon>
makeArchiveDB(boost::filesystem::path const& dir, std::string const& dbName)
{
    return std::make_unique<DatabaseCon>(
        dir, dbName, DownloaderDBPragma, ShardArchiveHandlerDBInit);
}

void
readArchiveDB(
    DatabaseCon& db,
    std::function<void(std::string const&, int)> const& func)
{
    soci::rowset<soci::row> rs =
        (db.getSession().prepare << "SELECT * FROM State;");

    for (auto it = rs.begin(); it != rs.end(); ++it)
    {
        func(it->get<std::string>(1), it->get<int>(0));
    }
}

void
insertArchiveDB(
    DatabaseCon& db,
    std::uint32_t shardIndex,
    std::string const& url)
{
    db.getSession() << "INSERT INTO State VALUES (:index, :url);",
        soci::use(shardIndex), soci::use(url);
}

void
deleteFromArchiveDB(DatabaseCon& db, std::uint32_t shardIndex)
{
    db.getSession() << "DELETE FROM State WHERE ShardIndex = :index;",
        soci::use(shardIndex);
}

void
dropArchiveDB(DatabaseCon& db)
{
    db.getSession() << "DROP TABLE State;";
}

}  // namespace ripple

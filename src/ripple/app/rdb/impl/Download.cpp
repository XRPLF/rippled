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

#include <ripple/app/rdb/Download.h>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

std::pair<std::unique_ptr<DatabaseCon>, std::optional<std::uint64_t>>
openDatabaseBodyDb(
    DatabaseCon::Setup const& setup,
    boost::filesystem::path const& path)
{
    // SOCI requires boost::optional (not std::optional) as the parameter.
    boost::optional<std::string> pathFromDb;
    boost::optional<std::uint64_t> size;

    auto conn = std::make_unique<DatabaseCon>(
        setup, "Download", DownloaderDBPragma, DatabaseBodyDBInit);

    auto& session = *conn->checkoutDb();

    session << "SELECT Path FROM Download WHERE Part=0;",
        soci::into(pathFromDb);

    // Try to reuse preexisting
    // database.
    if (pathFromDb)
    {
        // Can't resuse - database was
        // from a different file download.
        if (pathFromDb != path.string())
        {
            session << "DROP TABLE Download;";
        }

        // Continuing a file download.
        else
        {
            session << "SELECT SUM(LENGTH(Data)) FROM Download;",
                soci::into(size);
        }
    }

    return {std::move(conn), (size ? *size : std::optional<std::uint64_t>())};
}

std::uint64_t
databaseBodyDoPut(
    soci::session& session,
    std::string const& data,
    std::string const& path,
    std::uint64_t fileSize,
    std::uint64_t part,
    std::uint16_t maxRowSizePad)
{
    std::uint64_t rowSize = 0;
    soci::indicator rti;

    std::uint64_t remainingInRow = 0;

    auto be =
        dynamic_cast<soci::sqlite3_session_backend*>(session.get_backend());
    BOOST_ASSERT(be);

    // This limits how large we can make the blob
    // in each row. Also subtract a pad value to
    // account for the other values in the row.
    auto const blobMaxSize =
        sqlite_api::sqlite3_limit(be->conn_, SQLITE_LIMIT_LENGTH, -1) -
        maxRowSizePad;

    std::string newpath;

    auto rowInit = [&] {
        session << "INSERT INTO Download VALUES (:path, zeroblob(0), 0, :part)",
            soci::use(newpath), soci::use(part);

        remainingInRow = blobMaxSize;
        rowSize = 0;
    };

    session << "SELECT Path,Size,Part FROM Download ORDER BY Part DESC "
               "LIMIT 1",
        soci::into(newpath), soci::into(rowSize), soci::into(part, rti);

    if (!session.got_data())
    {
        newpath = path;
        rowInit();
    }
    else
        remainingInRow = blobMaxSize - rowSize;

    auto insert = [&session, &rowSize, &part, &fs = fileSize](
                      auto const& data) {
        std::uint64_t updatedSize = rowSize + data.size();

        session << "UPDATE Download SET Data = CAST(Data || :data AS blob), "
                   "Size = :size WHERE Part = :part;",
            soci::use(data), soci::use(updatedSize), soci::use(part);

        fs += data.size();
    };

    size_t currentBase = 0;

    while (currentBase + remainingInRow < data.size())
    {
        if (remainingInRow)
        {
            insert(data.substr(currentBase, remainingInRow));
            currentBase += remainingInRow;
        }

        ++part;
        rowInit();
    }

    insert(data.substr(currentBase));

    return part;
}

void
databaseBodyFinish(soci::session& session, std::ofstream& fout)
{
    soci::rowset<std::string> rs =
        (session.prepare << "SELECT Data FROM Download ORDER BY PART ASC;");

    // iteration through the resultset:
    for (auto it = rs.begin(); it != rs.end(); ++it)
        fout.write(it->data(), it->size());
}

}  // namespace ripple

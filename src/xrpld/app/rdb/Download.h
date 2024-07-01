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

#ifndef RIPPLE_APP_RDB_DOWNLOAD_H_INCLUDED
#define RIPPLE_APP_RDB_DOWNLOAD_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/core/Config.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/peerfinder/impl/Store.h>
#include <boost/filesystem.hpp>

namespace ripple {

/**
 * @brief openDatabaseBodyDb Opens a database that will store the contents of a
 *        file being downloaded, returns its descriptor, and starts a new
 *        download process or continues an existing one.
 * @param setup Path to the database and other opening parameters.
 * @param path Path of the new file to download.
 * @return Pair containing a unique pointer to the database and the amount of
 *         bytes already downloaded if a download is being continued.
 */
std::pair<std::unique_ptr<DatabaseCon>, std::optional<std::uint64_t>>
openDatabaseBodyDb(
    DatabaseCon::Setup const& setup,
    boost::filesystem::path const& path);

/**
 * @brief databaseBodyDoPut Saves a new fragment of a downloaded file.
 * @param session Session with the database.
 * @param data Downloaded fragment of file data to save.
 * @param path Path to the file currently being downloaded.
 * @param fileSize Size of the portion of the file already downloaded.
 * @param part The index of the most recently updated database row.
 * @param maxRowSizePad A constant padding value that accounts for other data
 *        stored in each row of the database.
 * @return Index of the most recently updated database row.
 */
std::uint64_t
databaseBodyDoPut(
    soci::session& session,
    std::string const& data,
    std::string const& path,
    std::uint64_t fileSize,
    std::uint64_t part,
    std::uint16_t maxRowSizePad);

/**
 * @brief databaseBodyFinish Finishes the download process and writes the file
 *        to disk.
 * @param session Session with the database.
 * @param fout Opened file into which the downloaded data from the database will
 *        be written.
 */
void
databaseBodyFinish(soci::session& session, std::ofstream& fout);

}  // namespace ripple

#endif

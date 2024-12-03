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

#include <xrpld/app/rdb/Vacuum.h>
#include <boost/format.hpp>

namespace ripple {

bool
doVacuumDB(DatabaseCon::Setup const& setup, beast::Journal j)
{
    boost::filesystem::path dbPath = setup.dataDir / TxDBName;

    uintmax_t const dbSize = file_size(dbPath);
    ASSERT(
        dbSize != static_cast<uintmax_t>(-1),
        "ripple:doVacuumDB : file_size succeeded");

    if (auto available = space(dbPath.parent_path()).available;
        available < dbSize)
    {
        std::cerr << "The database filesystem must have at least as "
                     "much free space as the size of "
                  << dbPath.string() << ", which is " << dbSize
                  << " bytes. Only " << available << " bytes are available.\n";
        return false;
    }

    auto txnDB = std::make_unique<DatabaseCon>(
        setup, TxDBName, setup.txPragma, TxDBInit, j);
    auto& session = txnDB->getSession();
    std::uint32_t pageSize;

    // Only the most trivial databases will fit in memory on typical
    // (recommended) hardware. Force temp files to be written to disk
    // regardless of the config settings.
    session << boost::format(CommonDBPragmaTemp) % "file";
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM beginning. page_size: " << pageSize << std::endl;

    session << "VACUUM;";
    ASSERT(
        setup.globalPragma != nullptr,
        "ripple:doVacuumDB : non-null global pragma");
    for (auto const& p : *setup.globalPragma)
        session << p;
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM finished. page_size: " << pageSize << std::endl;

    return true;
}

}  // namespace ripple

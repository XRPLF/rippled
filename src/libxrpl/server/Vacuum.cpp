#include <xrpl/server/Vacuum.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/rdb/DBInit.h>
#include <xrpl/rdb/DatabaseCon.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>  // IWYU pragma: keep

#include <soci/into.h>

#include <cstdint>
#include <iostream>
#include <memory>

namespace xrpl {

bool
doVacuumDB(DatabaseCon::Setup const& setup, beast::Journal j)
{
    boost::filesystem::path const dbPath = setup.dataDir / kTxDbName;

    uintmax_t const dbSize = file_size(dbPath);
    XRPL_ASSERT(dbSize != static_cast<uintmax_t>(-1), "xrpl::doVacuumDB : file_size succeeded");

    if (auto available = space(dbPath.parent_path()).available; available < dbSize)
    {
        std::cerr << "The database filesystem must have at least as "
                     "much free space as the size of "
                  << dbPath.string() << ", which is " << dbSize << " bytes. Only " << available
                  << " bytes are available.\n";
        return false;
    }

    auto txnDB = std::make_unique<DatabaseCon>(setup, kTxDbName, setup.txPragma, kTxDbInit, j);
    auto& session = txnDB->getSession();
    std::uint32_t pageSize = 0;

    // Only the most trivial databases will fit in memory on typical
    // (recommended) hardware. Force temp files to be written to disk
    // regardless of the config settings.
    session << boost::format(kCommonDbPragmaTemp) % "file";
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM beginning. page_size: " << pageSize << std::endl;

    session << "VACUUM;";
    XRPL_ASSERT(setup.globalPragma, "xrpl::doVacuumDB : non-null global pragma");
    for (auto const& p : *setup.globalPragma)
        session << p;
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM finished. page_size: " << pageSize << std::endl;

    return true;
}

}  // namespace xrpl

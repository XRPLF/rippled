#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/rdb/RelationalDatabase.h>

#include <cstdint>
#include <string>

//------------------------------------------------------------------------------

namespace xrpl {

void
convertBlobsToTxResult(
    RelationalDatabase::AccountTxs& to,
    std::uint32_t ledgerIndex,
    std::string const& status,
    Blob const& rawTxn,
    Blob const& rawMeta,
    Application& app);

void
saveLedgerAsync(Application& app, std::uint32_t seq);

}  // namespace xrpl

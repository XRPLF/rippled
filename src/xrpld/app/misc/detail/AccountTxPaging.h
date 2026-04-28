#pragma once

#include <xrpl/rdb/RelationalDatabase.h>

#include <cstdint>

//------------------------------------------------------------------------------

namespace xrpl {

void
convertBlobsToTxResult(
    RelationalDatabase::AccountTxs& to,
    std::uint32_t ledger_index,
    std::string const& status,
    Blob const& rawTxn,
    Blob const& rawMeta,
    Application& app);

void
saveLedgerAsync(Application& app, std::uint32_t seq);

}  // namespace xrpl

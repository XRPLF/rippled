#ifndef XRPL_APP_MISC_IMPL_ACCOUNTTXPAGING_H_INCLUDED
#define XRPL_APP_MISC_IMPL_ACCOUNTTXPAGING_H_INCLUDED

#include <xrpld/app/rdb/RelationalDatabase.h>

#include <cstdint>

//------------------------------------------------------------------------------

namespace ripple {

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

}  // namespace ripple

#endif

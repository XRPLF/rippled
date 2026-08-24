#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/rdb/RelationalDatabase.h>

#include <cstdint>
#include <string>

//------------------------------------------------------------------------------

namespace xrpl {

class STTx;

/**
 * @brief Determines whether a transaction should be included in account_tx
 *        results based on a delegation filter.
 */
bool
passesDelegateFilter(STTx const& tx, DelegateFilter const& filter, AccountID const& contextAccount);

/**
 * @brief Deserializes a transaction blob and applies the account_tx
 *        delegation filter.
 */
bool
passesDelegateFilter(
    Blob const& rawData,
    DelegateFilter const& filter,
    AccountID const& contextAccount);

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

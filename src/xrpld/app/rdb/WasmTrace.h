#ifndef XRPL_APP_RDB_WasmTrace_H_INCLUDED
#define XRPL_APP_RDB_WasmTrace_H_INCLUDED

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/DatabaseCon.h>

#include <xrpl/protocol/Indexes.h>

namespace xrpl {

std::unique_ptr<DatabaseCon>
makeWasmTraceDB(DatabaseCon::Setup const& setup, beast::Journal j);

void
addWasmTraceLogs(soci::session& session, TxID const& txId, Keylet const& keylet, std::vector<std::string> const& data);

std::map<uint256, std::vector<std::string>>
getWasmTraceByTxID(soci::session& session, TxID const& txId);

}  // namespace xrpl

#endif

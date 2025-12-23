#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

// Define a function pointer type that can be used to delete ledger node types.
using DeleterFuncPtr = TER (*)(
    ServiceRegistry& registry,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j);

DeleterFuncPtr
nonObligationDeleter(LedgerEntryType t);

TER
deletePreclaim(
    PreclaimContext const& ctx,
    std::uint32_t seqDelta,
    AccountID const account,
    AccountID const dest,
    bool isPseudoAccount = false);

TER
deleteDoApply(
    ApplyContext& applyCtx,
    STAmount const& accountBalance,
    AccountID const& account,
    AccountID const& dest);

}  // namespace xrpl

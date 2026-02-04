#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/digest.h>

namespace xrpl {

// =========================================================
// SECTION: LEDGER HEADER FUNCTIONS
// =========================================================

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerSqn()
{
    return ctx.view().seq();
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerTime()
{
    return ctx.view().parentCloseTime().time_since_epoch().count();
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerHash()
{
    return ctx.view().header().parentHash;
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getBaseFee()
{
    return ctx.view().fees().base.drops();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(uint256 const& amendmentId)
{
    return ctx.view().rules().enabled(amendmentId);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(std::string_view const& amendmentName)
{
    auto const& table = ctx.app.getAmendmentTable();
    auto const amendment = table.find(std::string(amendmentName));
    return ctx.view().rules().enabled(amendment);
}

}  // namespace xrpl

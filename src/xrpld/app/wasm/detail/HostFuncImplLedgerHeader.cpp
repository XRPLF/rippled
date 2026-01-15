#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/digest.h>

namespace xrpl {

// =========================================================
// SECTION: LEDGER HEADER FUNCTIONS
// =========================================================

Expected<std::int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerSqn()
{
    auto seq = ctx.view().seq();
    if (seq > std::numeric_limits<int32_t>::max())
        return Unexpected(HostFunctionError::INTERNAL);  // LCOV_EXCL_LINE
    return static_cast<int32_t>(seq);
}

Expected<std::int32_t, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerTime()
{
    auto time = ctx.view().parentCloseTime().time_since_epoch().count();
    if (time > std::numeric_limits<int32_t>::max())
        return Unexpected(HostFunctionError::INTERNAL);
    return static_cast<int32_t>(time);
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerHash()
{
    return ctx.view().header().parentHash;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getBaseFee()
{
    auto fee = ctx.view().fees().base.drops();
    if (fee > std::numeric_limits<int32_t>::max())
        return Unexpected(HostFunctionError::INTERNAL);
    return static_cast<int32_t>(fee);
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

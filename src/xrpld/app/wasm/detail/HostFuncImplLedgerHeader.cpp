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
WasmHostFunctionsImpl::isAmendmentEnabled(Slice const& data)
{
    // If the slice is exactly 32 bytes, interpret as amendment ID (uint256)
    if (data.size() == uint256::bytes)
    {
        if (ctx.view().rules().enabled(uint256::fromVoid(data.data())))
            return 1;
    }

    // Otherwise interpret as amendment name string
    if (data.size() > 64)
        return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);

    auto const amendmentName = std::string_view(reinterpret_cast<char const*>(data.data()), data.size());
    auto const& table = ctx.app.getAmendmentTable();
    auto const amendment = table.find(std::string(amendmentName));
    return ctx.view().rules().enabled(amendment);
}

}  // namespace xrpl

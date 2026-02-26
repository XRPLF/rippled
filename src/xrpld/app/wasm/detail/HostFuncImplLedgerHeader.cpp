#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/ledger/AmendmentTable.h>
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
    auto const& table = ctx.registry.getAmendmentTable();
    auto const amendment = table.find(std::string(amendmentName));
    return ctx.view().rules().enabled(amendment);
}

}  // namespace xrpl

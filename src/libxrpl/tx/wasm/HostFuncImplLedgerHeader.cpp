#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl {

// =========================================================
// SECTION: LEDGER HEADER FUNCTIONS
// =========================================================

std::expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerSqn() const
{
    return ctx_.view().seq();
}

std::expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerTime() const
{
    return ctx_.view().parentCloseTime().time_since_epoch().count();
}

std::expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerHash() const
{
    return ctx_.view().header().parentHash;
}

std::expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getBaseFee() const
{
    return ctx_.view().fees().base.drops();
}

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(uint256 const& amendmentId) const
{
    return ctx_.view().rules().enabled(amendmentId);
}

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(std::string_view const& amendmentName) const
{
    auto const& table = ctx_.registry.get().getAmendmentTable();
    auto const amendment = table.find(std::string(amendmentName));
    return ctx_.view().rules().enabled(amendment);
}

}  // namespace xrpl

#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/protocol/digest.h>

namespace xrpl {

// =========================================================
// SECTION: LEDGER HEADER FUNCTIONS
// =========================================================

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerSqn() const
{
    return ctx_.view().seq();
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerTime() const
{
    return ctx_.view().parentCloseTime().time_since_epoch().count();
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerHash() const
{
    return ctx_.view().header().parentHash;
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getBaseFee() const
{
    return ctx_.view().fees().base.drops();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(uint256 const& amendmentId) const
{
    return ctx_.view().rules().enabled(amendmentId);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(std::string_view const& amendmentName) const
{
    auto const& table = ctx_.registry.getAmendmentTable();
    auto const amendment = table.find(std::string(amendmentName));
    return ctx_.view().rules().enabled(amendment);
}

}  // namespace xrpl

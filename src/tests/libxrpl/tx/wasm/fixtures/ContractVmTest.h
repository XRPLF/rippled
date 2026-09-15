#pragma once

#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace xrpl::test {

// End to end for a contract: a WAT guest through the real VM, the real `HostContext`, the
// real `ContractHostFunctionsImpl`, and a real ledger.
//
// The host is passed in rather than built here, because what these tests are about is
// usually what the contract left behind — its data cache, its event map, the transactions it
// queued — which only the caller's `ContractHost` can be asked about afterwards.
struct ContractVmTest : ContractHostFixture
{
    std::expected<EscrowResult, WasmTER>
    run(ContractHost const& contractHost,
        std::string_view wat,
        std::int64_t gas = kAmpleGas,
        std::string_view entryPoint = escrowFunctionName)
    {
        return runWat(*contractHost, wat, gas, entryPoint);
    }
};

}  // namespace xrpl::test

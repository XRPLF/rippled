#pragma once

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmRun.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string_view>
#include <utility>

namespace xrpl::test {

// End-to-end: a WAT contract run through the REAL VM against the REAL host
// (`WasmHostFunctionsImpl`) over a REAL `TxTest` ledger. The real-host counterpart of
// `MockVmTest`; both forward to the shared `runWat` harness (`WasmRun.h`), differing only in
// the host. `host_calls/` mocks the host and `host_functions/` skips the VM; this exercises
// the whole host stack at once — VM, `HostContext` marshalling, the impl, and the ledger —
// driven by a guest.
//
// WAT rather than a compiled guest: the guest SDK (`xrpl-wasm-stdlib`) is an external repo
// with its own tests, so a compiled guest would couple this suite to that repo and a
// Rust->wasm toolchain. WAT keeps the host-side integration this repo owns and delegates the
// SDK exercise to the SDK's own repo.
struct RealVmTest : RealHostFixture
{
    // Assemble `wat` and run its `entryPoint` through the real VM against a real host built
    // over the current open ledger. `leKey`/`txType`/`assembler` configure the ledger object
    // the contract runs against and the transaction it reads (see `RealHostFixture::makeHost`).
    std::expected<EscrowResult, WasmTER>
    run(
        std::string_view wat,
        Keylet const& leKey = keylet::account(AccountID{}),
        TxType txType = ttESCROW_FINISH,
        std::function<void(STObject&)> assembler = [](STObject&) {},
        std::int64_t gas = kAmpleGas,
        std::string_view entryPoint = escrowFunctionName)
    {
        auto host = makeHost(leKey, txType, std::move(assembler));
        return runWat(*host, wat, gas, entryPoint);
    }
};

}  // namespace xrpl::test

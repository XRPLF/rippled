#pragma once

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmRun.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <string_view>
#include <utility>

namespace xrpl::test {

// End-to-end: a WAT contract run through the REAL VM against the REAL host
// over a REAL `TxTest` ledger.
struct RealVmTest : RealHostFixture
{
    // Assemble `wat` and run its `entryPoint` through the real VM against a real host built
    // over the current open ledger. `leKey`/`txType`/`assembler` configure the ledger object
    // the contract runs against and the transaction it reads.
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

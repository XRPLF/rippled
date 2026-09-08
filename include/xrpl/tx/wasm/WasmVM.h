#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace xrpl {

// The export a programmable escrow's contract is run through.
std::string_view inline constexpr escrowFunctionName = "escrow_finish";

// Run `wasmCode`'s `funcName` export with `gasLimit` gas, servicing its host calls
// through `hfs`.
//
// On success the result is what the contract returned - positive means the escrow may
// finish - together with the gas it consumed. On failure it is the TER to apply and,
// when the number means anything, the gas to write to transaction metadata: a contract
// that traps or exhausts its budget is charged for what it burned, while a `tecINTERNAL`
// reports no cost because the fault is the node's rather than the transaction's.
std::expected<EscrowResult, WasmTER>
runEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::int64_t gasLimit,
    std::string_view funcName = escrowFunctionName) noexcept;

// Screen `wasmCode`: whether `runEscrowWasm` would refuse it before the contract's
// first instruction. Compiles the module and reads its imports and exports; runs
// nothing.
//
// Takes no `HostFunctions`, because the verdict comes from the compiled module alone.
// That is what makes this callable from a transactor's `preflight`, which has no view
// to build a host over.
//
// `temINVALID_BYTECODE` for every fault in the module - the transaction carries something this
// engine cannot run, so it is refused before it can reach the ledger.
// `telFAILED_PROCESSING` if the engine itself failed: nothing was learned about the
// module, and a defect here is not evidence that the transaction is malformed.
NotTEC
preflightEscrowWasm(
    Bytes const& wasmCode,
    beast::Journal j,
    std::string_view funcName = escrowFunctionName) noexcept;

}  // namespace xrpl

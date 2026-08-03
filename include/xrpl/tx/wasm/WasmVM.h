#pragma once

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
//
// Does not throw. Every way a run can end - including a Rust panic inside the engine or
// a C++ exception thrown by a host function - arrives as one of those two answers.
std::expected<EscrowResult, WasmTER>
runEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::int64_t gasLimit,
    std::string_view funcName = escrowFunctionName);

}  // namespace xrpl

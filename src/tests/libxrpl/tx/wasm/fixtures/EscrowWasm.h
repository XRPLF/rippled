#pragma once

#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <helpers/TxTest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test {

// Contracts and fee arithmetic shared by the transactor-level escrow tests.

// Reads the ledger sequence and returns 5. A minimal *working* contract: it makes a real
// host call, so it exercises more than validation, but what it returns is uninteresting.
inline constexpr auto kReadsLedgerSqn = std::string_view{R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (drop (call $ldgr_index (i32.const 0) (i32.const 4)))
    (i32.const 5)))
)wat"};

// Traps. A fault rather than a rejection: no return code, and nothing it wrote survives.
inline constexpr auto kTraps = std::string_view{R"wat(
(module
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (unreachable)))
)wat"};

// Loops forever, so the only way it stops is by exhausting its gas allowance.
inline constexpr auto kLoopsForever = std::string_view{R"wat(
(module
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (loop $forever (br $forever))
    (i32.const 1)))
)wat"};

// Imports a host function that does not exist, so screening refuses it. Well-formed wasm —
// the refusal is about the import list, not the bytes.
inline constexpr auto kImportsUnknownHostFunction = std::string_view{R"wat(
(module
  (import "host_lib" "bad" (func $bad (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (call $bad)))
)wat"};

// A contract that approves only once the ledger has reached `threshold`: returns 5 at or
// past it, 0 (a rejection) before.
//
// The escrow's release condition is thus a real predicate over ledger state that changes
// from false to true while the escrow sits there — the shape the whole feature exists for,
// and the one a fixed contract cannot express. Built at runtime because the threshold has
// to be chosen relative to the environment's current sequence.
std::string
gatedOnLedgerSqn(std::uint32_t threshold);

// What an `EscrowCreate` carrying this bytecode must pay: ten base fees plus five drops a
// byte (`EscrowCreate::calculateBaseFee`).
XRPAmount
escrowCreateFee(TxTest const& env, Bytes const& bytecode);

// What an `EscrowFinish` carrying this gas allowance must pay: the base fee plus the
// allowance priced at `gasPrice`, rounded up (`EscrowFinish::calculateBaseFee`).
XRPAmount
escrowFinishFee(TxTest const& env, std::uint32_t allowance);

}  // namespace xrpl::test

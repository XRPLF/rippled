#include <tx/wasm/fixtures/EscrowWasm.h>

#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <helpers/TxTest.h>

#include <cstdint>
#include <format>
#include <string>

namespace xrpl::test {

std::string
gatedOnLedgerSqn(std::uint32_t threshold)
{
    return std::format(
        R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (drop (call $ldgr_index (i32.const 0) (i32.const 4)))
    (if (result i32) (i32.ge_u (i32.load (i32.const 0)) (i32.const {}))
      (then (i32.const 5))
      (else (i32.const 0)))))
)wat",
        threshold);
}

XRPAmount
escrowCreateFee(TxTest const& env, Bytes const& bytecode)
{
    return (env.getOpenLedger().fees().base * 10) +
        XRPAmount{static_cast<std::int64_t>(bytecode.size()) * 5};
}

XRPAmount
escrowFinishFee(TxTest const& env, std::uint32_t allowance)
{
    auto const& fees = env.getOpenLedger().fees();
    // Integer division rounds down, so the transactor adds one drop; match it exactly or
    // the submission fails on the fee rather than on what it meant to test.
    auto const gasFee = ((std::uint64_t{allowance} * fees.gasPrice) / microDropsPerDrop) + 1;
    return fees.base + XRPAmount{static_cast<std::int64_t>(gasFee)};
}

}  // namespace xrpl::test

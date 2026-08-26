#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <tx/wasm/RealVmTest.h>

#include <cstdint>
#include <string_view>

namespace xrpl::test {

// The real ledger's sequence.
struct LedgerSqnE2e : RealVmTest
{
};

TEST_F(LedgerSqnE2e, ContractReadsTheRealLedgerSequence)
{
    // Ask the host for the ledger sequence into offset 0, then return the i32 stored there.
    static constexpr auto kWat = std::string_view{R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (drop (call $ldgr_index (i32.const 0) (i32.const 4)))
    (i32.load (i32.const 0))))
)wat"};

    auto const outcome = run(kWat);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, static_cast<std::int32_t>(ledger.getOpenLedger().header().seq));
}

}  // namespace xrpl::test

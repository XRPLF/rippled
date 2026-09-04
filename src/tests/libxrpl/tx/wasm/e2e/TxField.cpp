#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/RealVmTest.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <cstdint>
#include <format>
#include <string>

namespace xrpl::test {

// A contract reads a field of its transaction end to end.
struct TxFieldE2e : RealVmTest
{
};

TEST_F(TxFieldE2e, ContractReadsAFieldOfItsTransaction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    static constexpr auto kScale = std::uint8_t{8};
    auto const tx = mptIssuanceCreateTx(owner, kScale);

    // Ask the tx for `sfAssetScale` (a single byte) and return the i32 the guest loads — the
    // scale, zero-extended — so the assertion checks the value flowed through, not just a count.
    auto const wat = std::format(
        R"wat(
(module
  (import "host_lib" "tx_field" (func $tx_field (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (drop (call $tx_field (i32.const {}) (i32.const 0) (i32.const 4)))
    (i32.load (i32.const 0))))
)wat",
        sfAssetScale.getCode());

    auto const outcome = run(wat, keylet::account(owner.id()), tx.type, tx.build);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, kScale);
}

}  // namespace xrpl::test

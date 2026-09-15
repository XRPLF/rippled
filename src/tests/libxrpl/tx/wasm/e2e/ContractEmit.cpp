#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractVmTest.h>

#include <format>
#include <string>

namespace xrpl::test {

// A contract builds a payment and emits it, end to end.
//
// Two things meet here that no other layer sees together. The builder index is host state
// that outlives one call — `build_txn` answers it and two later calls name it — and the TER
// crosses as **bytes in a region** rather than as the call's result, because half the TER
// codes are negative and a negative result is a host error.
struct ContractEmitE2e : ContractVmTest
{
    Account const alice = fund("alice");
    Account const contract = fund("contract", XRP(2000));
    Account const carol = fund("carol");

    // Builds a payment of 192 drops to carol, emits it, and returns the TER that was written
    // to the output region.
    [[nodiscard]] std::string
    wat() const
    {
        auto const amount = WasmLedger::toBytes(STAmount{XRP(192)});
        auto const destination = accountField(carol.id());

        auto escape = [](Bytes const& bytes) {
            std::string out;
            for (auto const byte : bytes)
                out += std::format("\\{:02x}", byte);
            return out;
        };

        return std::format(
            R"wat(
(module
  (import "host_lib" "build_txn" (func $build_txn (param i32) (result i32)))
  (import "host_lib" "add_txn_field"
    (func $add_txn_field (param i32 i32 i32 i32) (result i32)))
  (import "host_lib" "emit_built_txn"
    (func $emit_built_txn (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "{}")

  (func (export "escrow_finish") (result i32)
    (local $txn i32)
    (local $n i32)

    ;; The index this answers is the only host state that outlives a call.
    (local.set $txn (call $build_txn (i32.const {})))
    (if (i32.lt_s (local.get $txn) (i32.const 0)) (then (return (local.get $txn))))

    (local.set $n (call $add_txn_field
      (local.get $txn) (i32.const {}) (i32.const 0) (i32.const {})))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))

    (local.set $n (call $add_txn_field
      (local.get $txn) (i32.const {}) (i32.const 32) (i32.const {})))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))

    ;; The TER is written here, not returned: a negative one would read as a host error.
    (local.set $n
      (call $emit_built_txn (local.get $txn) (i32.const 64) (i32.const 4)))
    (if (i32.lt_s (local.get $n) (i32.const 0)) (then (return (local.get $n))))
    (if (i32.ne (local.get $n) (i32.const 4)) (then (return (i32.const -100))))

    (i32.load (i32.const 64))))
)wat",
            escape(amount),
            escape(destination),
            static_cast<int>(ttPAYMENT),
            sfAmount.getCode(),
            amount.size(),
            sfDestination.getCode(),
            destination.size());
    }
};

TEST_F(ContractEmitE2e, AContractBuildsAPaymentAndEmitsIt)
{
    auto const contractHost =
        makeContractHost({.contractAccount = contract.id(), .caller = alice.id()});

    auto const outcome = run(contractHost, wat());
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, TERtoInt(tesSUCCESS))
        << "the TER the guest read out of its own memory";

    ASSERT_EQ(contractHost.context().result.emittedTxns.size(), 1U);
    auto const& emitted = contractHost.context().result.emittedTxns.front();
    EXPECT_EQ(emitted->getAccountID(sfAccount), contract.id());
    EXPECT_EQ(emitted->getAccountID(sfDestination), carol.id());
    EXPECT_EQ(emitted->getFieldAmount(sfAmount), STAmount{XRP(192)});
}

}  // namespace xrpl::test

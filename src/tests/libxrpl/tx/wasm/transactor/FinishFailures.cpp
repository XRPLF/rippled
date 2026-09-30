#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/protocol_autogen/transactions/EscrowFinish.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TestServiceRegistry.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace xrpl::test {
namespace {

// The ways an `EscrowFinish` against a contract-bearing escrow fails, and what each reports.
// A rejection, a fault, and running out of gas are three outcomes; only one carries a return
// code.
struct FinishFailures : testing::Test
{
    TxTest env;
    Account const alice{"alice"};
    Account const carol{"carol"};

    FinishFailures()
    {
        createAccounts(env, XRP(5'000), alice, carol);
    }

    std::uint32_t
    createEscrow(std::string_view wat)
    {
        auto const wasm = assembleWat(wat);
        auto const seq = env.getAccountRoot(alice).getSequence();

        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        builder.setBytecode(makeSlice(wasm));
        builder.setCancelAfter(closeTimeOffset(env, 1'000));

        EXPECT_EQ(env.submit(builder, alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
        env.close();
        return seq;
    }

    std::uint32_t
    createPlainEscrow()
    {
        auto const seq = env.getAccountRoot(alice).getSequence();

        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        // A contract-free escrow needs a `FinishAfter` or a condition; `CancelAfter` alone is
        // `temMALFORMED`.
        builder.setFinishAfter(closeTimeOffset(env, 1));
        builder.setCancelAfter(closeTimeOffset(env, 1'000));

        EXPECT_EQ(env.submit(builder, alice, XRPAmount{100'000}).ter, tesSUCCESS);
        env.close();
        env.close();  // past the finish time
        return seq;
    }

    [[nodiscard]] ClosedResult
    finish(std::uint32_t seq, std::optional<std::uint32_t> allowance, XRPAmount fee)
    {
        auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
        if (allowance)
        {
            builder.setGas(*allowance);
        }

        return env.submitAndClose(builder, carol, fee);
    }
};

TEST_F(FinishFailures, finish_is_refused_while_smart_escrow_is_disabled)
{
    auto disabled = TxTest{allFeatures() - featureSmartEscrow};
    createAccounts(disabled, XRP(5'000), alice, carol);

    auto builder = transactions::EscrowFinishBuilder{carol, alice, 1};
    builder.setGas(4);

    EXPECT_EQ(disabled.submit(builder, carol, XRPAmount{100'000}).ter, temDISABLED);
}

// The protocol ceiling is bounded in `preflight`, which has no view: no escrow is looked up,
// so a nonexistent one still reports the allowance problem rather than tecNO_TARGET. That is
// what separates the ceiling from the voted limit, which is checked in `preclaim`.
TEST_F(FinishFailures, an_allowance_past_the_protocol_ceiling_is_refused_without_an_escrow)
{
    auto builder = transactions::EscrowFinishBuilder{carol, alice, 1};
    builder.setGas(kMaxGasLimit + 1);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{10'000'000}).ter, temBAD_LIMIT);
}

// Execution cannot be bought unbounded just by asking for it.
TEST_F(FinishFailures, an_allowance_past_the_gas_limit_is_refused)
{
    // The escrow has to exist: the voted limit is checked in `preclaim`, which
    // reads the escrow first and would otherwise report tecNO_TARGET.
    auto const seq = createEscrow(kReadsLedgerSqn);

    auto fees = TestServiceRegistry::defaultFees();
    fees.gasLimit = 1'000;
    env.setFees(fees);
    env.close();

    auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
    builder.setGas(1'001);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{10'000'000}).ter, temBAD_LIMIT);
}

// A zero gas limit turns the runtime off.
TEST_F(FinishFailures, a_zero_gas_limit_disables_finishing)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    auto fees = TestServiceRegistry::defaultFees();
    fees.gasLimit = 0;
    env.setFees(fees);
    env.close();

    auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
    builder.setGas(1'000);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{10'000'000}).ter, temTEMP_DISABLED);
}

// The other rung: zeroing the size limit stops new uploads but must leave an existing escrow
// finishable, so the feature can be wound down without stranding holders until CancelAfter.
// Folding the two limits into one predicate would leave every other test here passing.
TEST_F(FinishFailures, a_zero_size_limit_still_allows_finishing)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    auto fees = TestServiceRegistry::defaultFees();
    fees.bytecodeSizeLimit = 0;
    env.setFees(fees);
    env.close();

    auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
    builder.setGas(1'000);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{10'000'000}).ter, tesSUCCESS);
}

TEST_F(FinishFailures, a_finish_without_a_gas_field_is_refused)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    EXPECT_EQ(finish(seq, std::nullopt, XRPAmount{100'000}).ter, tefBYTECODE_NOT_INCLUDED);
}

TEST_F(FinishFailures, a_zero_allowance_is_refused)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
    builder.setGas(0);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{100'000}).ter, temBAD_LIMIT);
}

// The allowance is paid up front, so under-paying is caught before anything runs.
TEST_F(FinishFailures, a_fee_that_does_not_cover_the_allowance_is_refused)
{
    auto const seq = createEscrow(kReadsLedgerSqn);
    constexpr std::uint32_t kAllowance = 1'000;

    auto const fee = escrowFinishFee(env, kAllowance) - XRPAmount{1};
    EXPECT_EQ(finish(seq, kAllowance, fee).ter, telINSUF_FEE_P);
}

TEST_F(FinishFailures, gas_against_an_escrow_without_bytecode_is_refused)
{
    auto const seq = createPlainEscrow();
    constexpr std::uint32_t kAllowance = 100;

    EXPECT_EQ(finish(seq, kAllowance, escrowFinishFee(env, kAllowance)).ter, tefNO_BYTECODE);
}

// A band rather than an equality: the meter stops at the last instruction it could afford,
// leaving a few units unspent. That band is what separates this from a trap, which stops
// early and reports a small fraction.
TEST_F(
    FinishFailures,
    running_out_of_gas_consumes_essentially_the_whole_allowance_and_reports_no_return_code)
{
    auto const seq = createEscrow(kLoopsForever);
    constexpr std::uint32_t kAllowance = 10'000;

    auto const result = finish(seq, kAllowance, escrowFinishFee(env, kAllowance));
    EXPECT_EQ(result.ter, tecOUT_OF_GAS);

    ASSERT_TRUE(result.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    auto const meta = result.meta->getAsObject();
    ASSERT_TRUE(meta.isFieldPresent(sfGasUsed));

    auto const used = meta.getFieldU32(sfGasUsed);
    EXPECT_LE(used, kAllowance) << "the engine cannot spend more than it was given";
    EXPECT_GT(used, kAllowance - (kAllowance / 100));
    EXPECT_FALSE(meta.isFieldPresent(sfVMReturnCode));
}

// A trap is a fault, not a rejection: gas actually burned, and no return code.
TEST_F(FinishFailures, a_trap_reports_partial_gas_and_no_return_code)
{
    auto const seq = createEscrow(kTraps);
    constexpr std::uint32_t kAllowance = 1'000;

    auto const result = finish(seq, kAllowance, escrowFinishFee(env, kAllowance));
    EXPECT_EQ(result.ter, tecFAILED_PROCESSING);

    ASSERT_TRUE(result.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    auto const meta = result.meta->getAsObject();
    ASSERT_TRUE(meta.isFieldPresent(sfGasUsed));
    EXPECT_LT(meta.getFieldU32(sfGasUsed), kAllowance);
    EXPECT_FALSE(meta.isFieldPresent(sfVMReturnCode));
}

}  // namespace
}  // namespace xrpl::test

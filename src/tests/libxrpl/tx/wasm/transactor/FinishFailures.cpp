#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Feature.h>
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

// The ways an `EscrowFinish` against a contract-bearing escrow fails, and what each one
// reports. The distinctions matter to a client: a rejection, a fault, and running out of gas
// are three different outcomes, and only one of them carries a return code.
struct FinishFailures : testing::Test
{
    TxTest env;
    Account const alice{"alice"};
    Account const carol{"carol"};

    void
    SetUp() override
    {
        env.createAccount(alice, XRP(5'000));
        env.createAccount(carol, XRP(5'000));
    }

    std::uint32_t
    createEscrow(std::string_view wat)
    {
        auto const wasm = assembleWat(wat);
        auto const seq = env.getAccountRoot(alice).getSequence();

        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        builder.setBytecode(makeSlice(wasm));
        builder.setCancelAfter(
            static_cast<std::uint32_t>(env.getCloseTime().time_since_epoch().count()) + 1'000);

        EXPECT_EQ(env.submit(builder, alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
        env.close();
        return seq;
    }

    // An escrow with no contract at all, for the "gas without bytecode" case.
    std::uint32_t
    createPlainEscrow()
    {
        auto const seq = env.getAccountRoot(alice).getSequence();

        auto const now = static_cast<std::uint32_t>(env.getCloseTime().time_since_epoch().count());

        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        // A contract-free escrow needs a `FinishAfter` or a condition — `CancelAfter` alone
        // is `temMALFORMED`, because nothing would ever release it.
        builder.setFinishAfter(now + 1);
        builder.setCancelAfter(now + 1'000);

        EXPECT_EQ(env.submit(builder, alice, XRPAmount{100'000}).ter, tesSUCCESS);
        env.close();
        env.close();  // past the finish time
        return seq;
    }

    struct Finished
    {
        TER ter;
        std::optional<TxMeta> meta;
    };

    [[nodiscard]] Finished
    finish(std::uint32_t seq, std::optional<std::uint32_t> allowance, XRPAmount fee)
    {
        auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
        if (allowance)
            builder.setGas(*allowance);

        auto const result = env.submit(builder, carol, fee);
        env.close();
        return Finished{.ter = result.ter, .meta = env.getMetadata(result.tx->getTransactionID())};
    }
};

TEST_F(FinishFailures, FinishIsRefusedWhileSmartEscrowIsDisabled)
{
    TxTest disabled{allFeatures() - featureSmartEscrow};
    disabled.createAccount(alice, XRP(5'000));
    disabled.createAccount(carol, XRP(5'000));

    auto builder = transactions::EscrowFinishBuilder{carol, alice, 1};
    builder.setGas(4);

    EXPECT_EQ(disabled.submit(builder, carol, XRPAmount{100'000}).ter, temDISABLED);
}

// The allowance is bounded by the voted gas limit, so a contract cannot buy unbounded
// execution by simply asking for it.
TEST_F(FinishFailures, AnAllowancePastTheGasLimitIsRefused)
{
    auto fees = TestServiceRegistry::defaultFees();
    fees.gasLimit = 1'000;
    env.getServiceRegistry().setFees(fees);

    auto builder = transactions::EscrowFinishBuilder{carol, alice, 1};
    builder.setGas(1'001);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{10'000'000}).ter, temBAD_LIMIT);
}

// A zero gas limit turns the runtime off. The old Beast test had to hand-insert an escrow
// ledger entry to reach this, because jtx cannot change its config mid-test; here the escrow
// is created normally and the limit drops afterwards.
TEST_F(FinishFailures, AZeroGasLimitDisablesFinishing)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    auto fees = TestServiceRegistry::defaultFees();
    fees.gasLimit = 0;
    env.getServiceRegistry().setFees(fees);

    auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
    builder.setGas(1'000);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{10'000'000}).ter, temTEMP_DISABLED);
}

TEST_F(FinishFailures, AFinishWithoutAGasFieldIsRefused)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    EXPECT_EQ(finish(seq, std::nullopt, XRPAmount{100'000}).ter, tefBYTECODE_NOT_INCLUDED);
}

TEST_F(FinishFailures, AZeroAllowanceIsRefused)
{
    auto const seq = createEscrow(kReadsLedgerSqn);

    auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
    builder.setGas(0);

    EXPECT_EQ(env.submit(builder, carol, XRPAmount{100'000}).ter, temBAD_LIMIT);
}

// The allowance is paid for up front, so under-paying is caught before anything runs.
TEST_F(FinishFailures, AFeeThatDoesNotCoverTheAllowanceIsRefused)
{
    auto const seq = createEscrow(kReadsLedgerSqn);
    constexpr std::uint32_t kAllowance = 1'000;

    auto const fee = escrowFinishFee(env, kAllowance) - XRPAmount{1};
    EXPECT_EQ(finish(seq, kAllowance, fee).ter, telINSUF_FEE_P);
}

// Gas on an escrow that has no contract: the transaction is about a thing that isn't there.
TEST_F(FinishFailures, GasAgainstAnEscrowWithoutBytecodeIsRefused)
{
    auto const seq = createPlainEscrow();
    constexpr std::uint32_t kAllowance = 100;

    EXPECT_EQ(finish(seq, kAllowance, escrowFinishFee(env, kAllowance)).ter, tefNO_BYTECODE);
}

// Running out of gas: essentially the whole allowance is consumed, and there is no return
// code because the contract never reached a return.
//
// "Essentially" because the meter stops at the last instruction it could afford, which for
// this loop leaves a few units unspent — the reported figure is what was really burned, not
// the allowance rounded up. The band is what distinguishes this from a trap, which stops
// early and reports a small fraction.
TEST_F(FinishFailures, RunningOutOfGasConsumesEssentiallyTheWholeAllowanceAndReportsNoReturnCode)
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

// A trap is a fault, not a rejection: it reports the gas actually burned — less than the
// whole allowance, which is what distinguishes it from running out — and no return code.
TEST_F(FinishFailures, ATrapReportsPartialGasAndNoReturnCode)
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

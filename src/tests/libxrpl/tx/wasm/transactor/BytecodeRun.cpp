#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/protocol_autogen/transactions/EscrowFinish.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <array>
#include <cstdint>
#include <optional>

namespace xrpl::test {
namespace {

// A contract deciding whether an escrow releases, end to end through the transactor. The
// other transactor files are about refusals; this one is about the feature working.

constexpr std::uint32_t kAllowance = 10'000;

// A preimage-sha256 pair, copied from jtx because `src/test/jtx` is not linked here.
constexpr auto kFulfillment = std::array<std::uint8_t, 4>{{0xA0, 0x02, 0x80, 0x00}};
constexpr auto kCondition = std::array<std::uint8_t, 39>{
    {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A,
     0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24, 0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B,
     0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

struct BytecodeRun : testing::Test
{
    TxTest env;
    Account const alice{"alice"};
    Account const carol{"carol"};

    BytecodeRun()
    {
        createAccounts(env, XRP(5'000), alice, carol);
    }

    std::uint32_t
    currentSeq() const
    {
        return env.getOpenLedger().header().seq;
    }

    struct Created
    {
        std::uint32_t seq;
        XRPAmount fee;
    };

    Created
    createEscrow(Bytes const& wasm, bool withCondition = false)
    {
        auto const seq = env.getAccountRoot(alice).getSequence();

        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(1'000)}};
        builder.setBytecode(makeSlice(wasm));
        builder.setCancelAfter(closeTimeOffset(env, 1'000));
        if (withCondition)
        {
            builder.setCondition(makeSlice(kCondition));
        }

        auto const fee = escrowCreateFee(env, wasm);
        EXPECT_EQ(env.submit(builder, alice, fee).ter, tesSUCCESS);
        env.close();
        return Created{.seq = seq, .fee = fee};
    }

    [[nodiscard]] ClosedResult
    finish(std::uint32_t seq, bool withFulfillment = false)
    {
        auto builder = transactions::EscrowFinishBuilder{carol, alice, seq};
        builder.setGas(kAllowance);

        auto fee = escrowFinishFee(env, kAllowance);
        if (withFulfillment)
        {
            builder.setCondition(makeSlice(kCondition));
            builder.setFulfillment(makeSlice(kFulfillment));
            fee += env.getOpenLedger().fees().base * (32 + (kFulfillment.size() / 16));
        }

        return env.submitAndClose(builder, carol, fee);
    }

    bool
    escrowExists(std::uint32_t seq) const
    {
        return env.getOpenLedger().read(keylet::escrow(alice, SeqProxy::rawSequence(seq))) !=
            nullptr;
    }
};

// The whole point: it refuses while its predicate is false and releases once the ledger
// makes it true, with nothing resubmitted differently.
TEST_F(BytecodeRun, AContractRejectsUntilItsConditionHoldsThenReleases)
{
    auto const threshold = currentSeq() + 3;
    auto const wasm = assembleWat(gatedOnLedgerSqn(threshold));
    auto const created = createEscrow(wasm);

    ASSERT_LT(currentSeq(), threshold);
    auto const rejected = finish(created.seq);
    EXPECT_EQ(rejected.ter, tecBYTECODE_REJECTED);
    EXPECT_TRUE(escrowExists(created.seq)) << "a rejected escrow must survive";

    ASSERT_TRUE(rejected.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(rejected.meta->getAsObject().getFieldI32(sfVMReturnCode), 0);

    while (currentSeq() < threshold)
    {
        env.close();
    }

    auto const approved = finish(created.seq);
    EXPECT_EQ(approved.ter, tesSUCCESS);
    EXPECT_FALSE(escrowExists(created.seq)) << "an approved escrow must be destroyed";

    ASSERT_TRUE(approved.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    auto const meta = approved.meta->getAsObject();
    EXPECT_EQ(meta.getFieldI32(sfVMReturnCode), 5);
    EXPECT_TRUE(meta.isFieldPresent(sfGasUsed));
}

TEST_F(BytecodeRun, TheBytecodeReserveIsHeldWhileTheEscrowLivesAndReleasedWhenItGoes)
{
    EXPECT_EQ(env.getOwnerCount(alice), 0U);

    auto const threshold = currentSeq() + 2;
    auto const wasm = assembleWat(gatedOnLedgerSqn(threshold));
    auto const created = createEscrow(wasm);

    // `calculateAdditionalReserve`: one increment for the escrow, plus one per 500 bytes.
    auto const expected = 1U + static_cast<std::uint32_t>(wasm.size() / 500);
    EXPECT_EQ(env.getOwnerCount(alice), expected);

    while (currentSeq() < threshold)
    {
        env.close();
    }
    ASSERT_EQ(finish(created.seq).ter, tesSUCCESS);

    EXPECT_EQ(env.getOwnerCount(alice), 0U);
}

TEST_F(BytecodeRun, CreatingChargesTheAmountAndTheFee)
{
    auto const before = env.getXrpBalance(alice);

    auto const wasm = assembleWat(gatedOnLedgerSqn(currentSeq() + 2));
    auto const created = createEscrow(wasm);

    EXPECT_EQ(env.getXrpBalance(alice), before - XRP(1'000) - created.fee);
    EXPECT_EQ(env.getXrpBalance(carol), XRP(5'000));
}

// The condition is the outer gate: without a fulfillment the contract is never reached, even
// though it would have approved.
TEST_F(BytecodeRun, AConditionIsCheckedBeforeTheContractRuns)
{
    auto const threshold = currentSeq() + 2;
    auto const wasm = assembleWat(gatedOnLedgerSqn(threshold));
    auto const created = createEscrow(wasm, /*withCondition*/ true);

    while (currentSeq() < threshold)
    {
        env.close();
    }

    EXPECT_EQ(finish(created.seq).ter, tecCRYPTOCONDITION_ERROR);
    EXPECT_TRUE(escrowExists(created.seq));

    auto const approved = finish(created.seq, /*withFulfillment*/ true);
    EXPECT_EQ(approved.ter, tesSUCCESS);
    EXPECT_FALSE(escrowExists(created.seq));
}

}  // namespace
}  // namespace xrpl::test

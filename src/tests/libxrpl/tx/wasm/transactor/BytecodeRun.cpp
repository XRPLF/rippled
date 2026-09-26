#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

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

// A working contract padded past a chosen size, for the reserve arithmetic.
Bytes
paddedContract(std::size_t padding)
{
    auto const wat =
        std::string{
            "(module\n"
            "  (memory (export \"memory\") 1)\n"
            "  (data (i32.const 0) \""} +
        std::string(padding, 'A') +
        "\")\n"
        "  (func (export \"escrow_finish\") (result i32)\n"
        "    (i32.const 1)))";
    return assembleWat(wat);
}

// The metadata mentions the escrow at all. Without this, "the metadata does not carry the
// contract" would pass vacuously on metadata that never touched the escrow.
bool
mentionsEscrow(TxMeta const& meta)
{
    return std::ranges::any_of(meta.getNodes(), [](STObject const& node) {
        return node.getFieldU16(sfLedgerEntryType) == ltESCROW;
    });
}

// Whether the contract's bytes appear anywhere in the serialized metadata. Serialized
// rather than walked field by field: what matters is that the bytes are not persisted, in
// whatever shape a later metadata change might give them.
bool
carries(TxMeta const& meta, Bytes const& wasm)
{
    auto s = Serializer{};
    meta.getAsObject().add(s);
    auto const& blob = s.peekData();
    return !std::ranges::search(blob, wasm).empty();
}

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

    auto const held = env.getOwnerCount(alice);
    ASSERT_GT(held, 0U);
    EXPECT_EQ(held, static_cast<std::uint32_t>(calculateAdditionalReserve(std::optional{wasm})));

    while (currentSeq() < threshold)
    {
        env.close();
    }
    ASSERT_EQ(finish(created.seq).ter, tesSUCCESS);

    EXPECT_EQ(env.getOwnerCount(alice), 0U);
}

// The contract is persisted once, in the EscrowCreate blob.
TEST_F(BytecodeRun, the_contract_is_never_copied_into_transaction_metadata)
{
    auto const threshold = currentSeq() + 2;
    auto const wasm = assembleWat(gatedOnLedgerSqn(threshold));

    auto const seq = env.getAccountRoot(alice).getSequence();
    auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(1'000)}};
    builder.setBytecode(makeSlice(wasm));
    builder.setCancelAfter(closeTimeOffset(env, 1'000));
    auto const created = env.submitAndClose(builder, alice, escrowCreateFee(env, wasm));

    ASSERT_EQ(created.ter, tesSUCCESS);
    ASSERT_TRUE(created.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_TRUE(mentionsEscrow(*created.meta));
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_FALSE(carries(*created.meta, wasm)) << "creation metadata must not repeat the contract";

    // Only the metadata drops it: the escrow still holds the contract, which is what
    // `EscrowFinish` runs.
    auto const slep = env.getOpenLedger().read(keylet::escrow(alice, SeqProxy::rawSequence(seq)));
    ASSERT_NE(slep, nullptr);
    EXPECT_EQ(slep->getFieldVL(sfBytecode), wasm);

    while (currentSeq() < threshold)
    {
        env.close();
    }

    auto const finished = finish(seq);
    ASSERT_EQ(finished.ter, tesSUCCESS);
    ASSERT_FALSE(escrowExists(seq));
    ASSERT_TRUE(finished.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_TRUE(mentionsEscrow(*finished.meta));
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_FALSE(carries(*finished.meta, wasm))
        << "deletion metadata must not carry the contract into every full node";
}

TEST_F(BytecodeRun, a_lock_leaving_the_owner_short_of_the_bytecode_reserve_is_refused)
{
    auto const wasm = paddedContract(600);
    ASSERT_EQ(calculateAdditionalReserve(std::optional{wasm}), 2);

    auto const& fees = env.getOpenLedger().fees();
    auto const oneUnit = fees.accountReserve(1, 1);
    auto const twoUnits = fees.accountReserve(2, 1);
    ASSERT_LT(oneUnit, twoUnits);

    auto const amount = XRP(1'000);
    auto const fee = escrowCreateFee(env, wasm);

    auto const create = [&](Account const& owner) {
        auto builder = transactions::EscrowCreateBuilder{owner, carol, STAmount{amount}};
        builder.setBytecode(makeSlice(wasm));
        builder.setCancelAfter(closeTimeOffset(env, 1'000));
        return env.submit(builder, owner, fee).ter;
    };

    // Funded so the lock leaves exactly one reserve unit: enough for the owner count a
    // bytecode-free escrow would add, one increment short of the two this one adds.
    Account const dave{"dave"};
    env.createAccount(dave, oneUnit + amount + fee);
    EXPECT_EQ(create(dave), tecUNFUNDED);

    // One increment more and the lock clears the reserve the escrow really costs.
    Account const erin{"erin"};
    env.createAccount(erin, twoUnits + amount + fee);
    EXPECT_EQ(create(erin), tesSUCCESS);
}

TEST(BytecodeReserve, TheBytecodeReserveIsCeilingDivision)
{
    auto const reserveFor = [](std::size_t size) {
        return calculateAdditionalReserve(std::optional{Bytes(size, 0x00)});
    };

    EXPECT_EQ(calculateAdditionalReserve(std::optional<Bytes>{}), 1);
    EXPECT_EQ(reserveFor(0), 1);
    EXPECT_EQ(reserveFor(1), 1);
    EXPECT_EQ(reserveFor(499), 1);
    EXPECT_EQ(reserveFor(500), 1);
    EXPECT_EQ(reserveFor(501), 2);
    EXPECT_EQ(reserveFor(1000), 2);
    EXPECT_EQ(reserveFor(1001), 3);
    EXPECT_EQ(reserveFor(1500), 3);
    EXPECT_EQ(reserveFor(200'000), 400);  // kMaxBytecodeSizeLimit
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

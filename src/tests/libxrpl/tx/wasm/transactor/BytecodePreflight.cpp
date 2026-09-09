#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TestServiceRegistry.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <optional>
#include <string>

namespace xrpl::test {
namespace {

// What `EscrowCreate` refuses before a contract ever runs. Everything here is decided in
// preflight, so the shape of the transaction is the whole subject: no ledger state matters
// beyond the account existing.
struct BytecodePreflight : testing::Test
{
    Account const alice{"alice"};
    Account const carol{"carol"};

    // Deadlines are relative to the environment's close time, which starts at genesis.
    static std::uint32_t
    after(TxTest const& env, std::uint32_t seconds)
    {
        return static_cast<std::uint32_t>(env.getCloseTime().time_since_epoch().count()) + seconds;
    }

    // An `EscrowCreate` with everything a valid one needs, for callers to spoil one field at
    // a time. `cancelAfter` is set because without an expiry the transaction is refused for
    // that reason first, and every bytecode case would report `temBAD_EXPIRATION` instead of
    // what it meant to check.
    transactions::EscrowCreateBuilder
    escrowCreate(TxTest const& env, Bytes const& bytecode)
    {
        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        builder.setBytecode(makeSlice(bytecode));
        builder.setCancelAfter(after(env, 100));
        return builder;
    }
};

TEST_F(BytecodePreflight, BytecodeIsRefusedWhileSmartEscrowIsDisabled)
{
    TxTest env{allFeatures() - featureSmartEscrow};
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    auto const fee = escrowCreateFee(env, wasm);

    EXPECT_EQ(env.submit(escrowCreate(env, wasm), alice, fee).ter, temDISABLED);

    // Also with a data field, which is the other half of the feature's surface.
    auto builder = escrowCreate(env, wasm);
    builder.setData(makeSlice(Bytes{0x00, 0x11, 0x22, 0x33}));
    EXPECT_EQ(env.submit(builder, alice, fee).ter, temDISABLED);
}

// A zero limit is how fee voting turns the runtime off, and it has to be distinguishable
// from "your contract is too big" — `temTEMP_DISABLED` says come back later, `temMALFORMED`
// says never.
TEST_F(BytecodePreflight, AZeroSizeLimitDisablesUploadsRatherThanRejectingThem)
{
    auto fees = TestServiceRegistry::defaultFees();
    fees.bytecodeSizeLimit = 0;
    TxTest env{std::nullopt, fees};
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    EXPECT_EQ(
        env.submit(escrowCreate(env, wasm), alice, escrowCreateFee(env, wasm)).ter,
        temTEMP_DISABLED);
}

TEST_F(BytecodePreflight, AZeroGasLimitDisablesUploads)
{
    auto fees = TestServiceRegistry::defaultFees();
    fees.gasLimit = 0;
    TxTest env{std::nullopt, fees};
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    EXPECT_EQ(
        env.submit(escrowCreate(env, wasm), alice, escrowCreateFee(env, wasm)).ter,
        temTEMP_DISABLED);
}

TEST_F(BytecodePreflight, EmptyBytecodeIsRefused)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    EXPECT_EQ(
        env.submit(escrowCreate(env, Bytes{}), alice, escrowCreateFee(env, Bytes{})).ter,
        temMALFORMED);
}

// Screening reaches into the module: this one is structurally valid wasm that asks for a
// host function nobody serves.
TEST_F(BytecodePreflight, BytecodeImportingAnUnknownHostFunctionIsRefused)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kImportsUnknownHostFunction);
    EXPECT_EQ(
        env.submit(escrowCreate(env, wasm), alice, escrowCreateFee(env, wasm)).ter,
        temINVALID_BYTECODE);
}

TEST_F(BytecodePreflight, DataWithoutBytecodeIsRefused)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
    builder.setData(makeSlice(Bytes{0x41, 0x41, 0x41, 0x41}));
    builder.setCancelAfter(after(env, 100));

    EXPECT_EQ(env.submit(builder, alice, XRPAmount{100'000}).ter, temMALFORMED);
}

TEST_F(BytecodePreflight, DataPastItsMaximumIsRefused)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    auto builder = escrowCreate(env, wasm);
    builder.setData(makeSlice(Bytes(kMaxWasmDataLength + 1, 0x42)));

    EXPECT_EQ(env.submit(builder, alice, XRPAmount{100'000}).ter, temMALFORMED);
}

// A contract needs a deadline. Without `CancelAfter` the escrow could never be reclaimed if
// the contract never approves, so every combination lacking it is refused — including the
// ones that look complete because they carry a `FinishAfter` or a condition.
TEST_F(BytecodePreflight, BytecodeWithoutACancelTimeIsRefused)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    auto const fee = escrowCreateFee(env, wasm);

    auto bare = [&] {
        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(500)}};
        builder.setBytecode(makeSlice(wasm));
        return builder;
    };

    EXPECT_EQ(env.submit(bare(), alice, fee).ter, temBAD_EXPIRATION);

    auto withFinish = bare();
    withFinish.setFinishAfter(after(env, 2));
    EXPECT_EQ(env.submit(withFinish, alice, fee).ter, temBAD_EXPIRATION);
}

// The success side, and the reason this file could not exist before: these cases need a
// module that actually passes screening, which the old compiled fixtures stopped doing.
TEST_F(BytecodePreflight, BytecodeWithACancelTimeIsAccepted)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    EXPECT_EQ(
        env.submit(escrowCreate(env, wasm), alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
}

TEST_F(BytecodePreflight, BytecodeWithAFinishAndCancelTimeIsAccepted)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    auto builder = escrowCreate(env, wasm);
    builder.setFinishAfter(after(env, 2));

    EXPECT_EQ(env.submit(builder, alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
}

// The per-byte charge is enforced, not advisory. One drop short is refused — which also
// confirms the fee helper the other tests rely on is computing the real number rather than
// something merely generous.
TEST_F(BytecodePreflight, AFeeOneDropShortIsRefused)
{
    TxTest env;
    env.createAccount(alice, XRP(5'000));
    env.createAccount(carol, XRP(5'000));

    auto const wasm = assembleWat(kReadsLedgerSqn);
    auto const fee = escrowCreateFee(env, wasm);

    EXPECT_EQ(env.submit(escrowCreate(env, wasm), alice, fee - XRPAmount{1}).ter, telINSUF_FEE_P);
}

}  // namespace
}  // namespace xrpl::test

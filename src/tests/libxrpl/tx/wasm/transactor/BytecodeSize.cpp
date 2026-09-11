#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TestServiceRegistry.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/ModuleBuilder.h>

#include <optional>

namespace xrpl::test {
namespace {

TER
createEscrowWith(TxTest& env, Account const& account, Bytes const& bytecode)
{
    auto builder = transactions::EscrowCreateBuilder{account, account, STAmount{XRP(1'000)}};
    builder.setBytecode(makeSlice(bytecode));
    builder.setCancelAfter(closeTimeOffset(env, 100));

    return env.submit(builder, account, escrowCreateFee(env, bytecode)).ter;
}

// Rich enough for the owner reserve a 200 KB contract demands: 401 increments, 802 XRP.
Account
fundedAccount(TxTest& env)
{
    auto const alice = Account{"alice"};
    env.createAccount(alice, XRP(2'000'000));
    return alice;
}

}  // namespace

// The transactor screens `sfBytecode` against `bytecodeSizeLimit` before the module reaches
// the engine. Compilation is unmetered, so that limit is the only thing bounding it.

// Footing for the rest: without this, "too big" and "malformed" are indistinguishable.
TEST(BytecodeSize, TheBuildersProduceAModuleTheEngineAccepts)
{
    auto env = TxTest{};
    auto const alice = fundedAccount(env);

    EXPECT_EQ(createEscrowWith(env, alice, codeHeavyModule(1'000)), tesSUCCESS);
    EXPECT_EQ(createEscrowWith(env, alice, dataHeavyModule(1'000)), tesSUCCESS);
}

TEST(BytecodeSize, AModuleUnderTheLimitIsAccepted)
{
    auto env = TxTest{};
    auto const alice = fundedAccount(env);

    auto const wasm = codeHeavyModule(90'000);
    ASSERT_LT(wasm.size(), env.getOpenLedger().fees().bytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), tesSUCCESS);
}

TEST(BytecodeSize, AModuleOverTheLimitIsRefused)
{
    auto env = TxTest{};
    auto const alice = fundedAccount(env);

    auto const wasm = codeHeavyModule(110'000);
    ASSERT_GT(wasm.size(), env.getOpenLedger().fees().bytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), temMALFORMED);
}

// The limit is on the module, not the code section — moving the bulk into a data segment
// does not walk around it.
TEST(BytecodeSize, ADataSegmentCountsTowardTheLimit)
{
    auto env = TxTest{};
    auto const alice = fundedAccount(env);

    auto const wasm = dataHeavyModule(110'000);
    ASSERT_GT(wasm.size(), env.getOpenLedger().fees().bytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), temMALFORMED);
}

// The limit is a fee setting, so it moves. If raising it admits nothing new, some other cap
// is really in charge.
TEST(BytecodeSize, RaisingTheLimitAdmitsALargerModule)
{
    auto fees = TestServiceRegistry::defaultFees();
    fees.bytecodeSizeLimit = kMaxBytecodeSizeLimit;
    auto env = TxTest{std::nullopt, fees};
    auto const alice = fundedAccount(env);

    auto const wasm = codeHeavyModule(150'000);
    ASSERT_GT(wasm.size(), TestServiceRegistry::defaultFees().bytecodeSizeLimit);
    ASSERT_LT(wasm.size(), kMaxBytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), tesSUCCESS);
}

// No per-function limit sits below the module limit: one function may occupy the entire
// module. `wasmparser` defines `MAX_WASM_FUNCTION_SIZE` = 128 KiB, but nothing on this path
// appears to enforce it — a lone body of a million instructions is accepted.
//
// So `bytecodeSizeLimit` is not defence in depth; it is the only bound on how much there is
// to compile, and raising it raises the worst case with nothing behind it. A failure here
// means a second limit has appeared, and that reasoning needs revisiting.
TEST(BytecodeSize, ASingleFunctionBodyIsNotSeparatelyCapped)
{
    auto const wasm = codeHeavyModule(1'000'000);
    ASSERT_GT(wasm.size(), 128U * 1024U) << "the module must exceed MAX_WASM_FUNCTION_SIZE";

    EXPECT_EQ(preflightEscrowWasm(wasm, beast::Journal{beast::Journal::getNullSink()}), tesSUCCESS);
}

}  // namespace xrpl::test

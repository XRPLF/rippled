#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TestServiceRegistry.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/ModuleBuilder.h>

#include <chrono>
#include <cstdint>
#include <optional>

namespace xrpl::test {
namespace {

// What a contract of a given size costs to submit: ten base fees plus five drops a byte
// (`EscrowCreate::calculateBaseFee`). Paying it exactly keeps a size test failing on the
// size rather than on the fee.
XRPAmount
createFee(TxTest const& env, Bytes const& bytecode)
{
    return (env.getOpenLedger().fees().base * 10) +
        XRPAmount{static_cast<std::int64_t>(bytecode.size()) * 5};
}

TER
createEscrowWith(TxTest& env, Account const& account, Bytes const& bytecode)
{
    using namespace std::chrono_literals;

    auto builder = transactions::EscrowCreateBuilder{account, account, STAmount{XRP(1'000)}};
    builder.setBytecode(makeSlice(bytecode));
    builder.setCancelAfter(
        static_cast<std::uint32_t>(env.getCloseTime().time_since_epoch().count() + 100));

    return env.submit(builder, account, createFee(env, bytecode)).ter;
}

// An account rich enough for the owner reserve a large contract demands: one increment per
// 500 bytes (`calculateAdditionalReserve`), so 200 KB costs 401 increments — 802 XRP at the
// default 2 XRP increment.
Account
fundedAccount(TxTest& env)
{
    auto const alice = Account{"alice"};
    env.createAccount(alice, XRP(2'000'000));
    return alice;
}

}  // namespace

// The transactor screens `sfBytecode` against `bytecodeSizeLimit` before the module ever
// reaches the engine. These pin that boundary, which is the one limit standing between an
// attacker-chosen module size and the *unmetered* work of compiling it: nothing charges for
// compilation, so size is the only thing bounding it.

// The sweep's own footing: the builders have to produce something the engine accepts, or
// every "too big" result below would be indistinguishable from "malformed".
TEST(BytecodeSize, TheBuildersProduceAModuleTheEngineAccepts)
{
    TxTest env;
    auto const alice = fundedAccount(env);

    EXPECT_EQ(createEscrowWith(env, alice, codeHeavyModule(1'000)), tesSUCCESS);
    EXPECT_EQ(createEscrowWith(env, alice, dataHeavyModule(1'000)), tesSUCCESS);
}

TEST(BytecodeSize, AModuleUnderTheLimitIsAccepted)
{
    TxTest env;
    auto const alice = fundedAccount(env);

    auto const wasm = codeHeavyModule(90'000);
    ASSERT_LT(wasm.size(), env.getOpenLedger().fees().bytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), tesSUCCESS);
}

TEST(BytecodeSize, AModuleOverTheLimitIsRefused)
{
    TxTest env;
    auto const alice = fundedAccount(env);

    auto const wasm = codeHeavyModule(110'000);
    ASSERT_GT(wasm.size(), env.getOpenLedger().fees().bytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), temMALFORMED);
}

// The size that counts is the module's, not the code's: a module made large by a data
// segment is screened the same way, so the limit cannot be walked around by moving the
// bulk out of the code section.
TEST(BytecodeSize, ADataSegmentCountsTowardTheLimit)
{
    TxTest env;
    auto const alice = fundedAccount(env);

    auto const wasm = dataHeavyModule(110'000);
    ASSERT_GT(wasm.size(), env.getOpenLedger().fees().bytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), temMALFORMED);
}

// The limit is a fee setting, so it moves. Raising it has to actually admit the module it
// now covers — otherwise some *other* cap is really in charge and the setting is decorative.
TEST(BytecodeSize, RaisingTheLimitAdmitsALargerModule)
{
    auto fees = TestServiceRegistry::defaultFees();
    fees.bytecodeSizeLimit = kMaxBytecodeSizeLimit;
    TxTest env{std::nullopt, fees};
    auto const alice = fundedAccount(env);

    auto const wasm = codeHeavyModule(150'000);
    ASSERT_GT(wasm.size(), TestServiceRegistry::defaultFees().bytecodeSizeLimit);
    ASSERT_LT(wasm.size(), kMaxBytecodeSizeLimit);

    EXPECT_EQ(createEscrowWith(env, alice, wasm), tesSUCCESS);
}

// `bytecodeSizeLimit` is the **only** thing bounding how much there is to compile.
//
// wasmparser defines `MAX_WASM_FUNCTION_SIZE` = 128 KiB, so it would be reasonable to
// assume a single function body is separately capped and that the size limit is a
// belt-and-braces second line. It is not: nothing on this path enforces that constant, and
// a lone body of a million instructions is accepted. Since compilation is unmetered — no
// gas is charged for it, and it happens once to screen the `EscrowCreate` and again on
// every `EscrowFinish` — the size limit is load-bearing on its own.
//
// If this ever starts failing, a second cap has appeared: good news, but the sweep above
// stops being the whole story and this comment is wrong.
TEST(BytecodeSize, ASingleFunctionBodyIsNotSeparatelyCapped)
{
    TxTest const env;
    auto const wasm = codeHeavyModule(1'000'000);
    ASSERT_GT(wasm.size(), 128U * 1024U) << "the module must exceed MAX_WASM_FUNCTION_SIZE";

    EXPECT_EQ(preflightEscrowWasm(wasm, beast::Journal{beast::Journal::getNullSink()}), tesSUCCESS);
}

}  // namespace xrpl::test

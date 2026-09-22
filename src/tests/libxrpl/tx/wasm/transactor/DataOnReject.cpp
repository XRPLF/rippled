#include <xrpl/basics/Slice.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/protocol_autogen/transactions/EscrowFinish.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test {
namespace {

// Writes "Data" and then rejects. `set_data` stores through the host, and the `-256` return
// puts `EscrowFinish` on its `reValue <= 0` path — a contract-defined rejection, distinct
// from a fault. The escrow survives, so whether the write survives with it is the question.
constexpr auto kWritesThenRejects = std::string_view{R"wat(
(module
  (import "host_lib" "set_data" (func $set_data (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "Data")
  (func (export "escrow_finish") (result i32)
    (drop (call $set_data (i32.const 0) (i32.const 4)))
    (i32.const -256)))
)wat"};

constexpr std::int32_t kRejectCode = -256;

// Enough to run the contract; the test is about persistence, not budgets.
constexpr std::uint32_t kAllowance = 100'000;

struct DataOnReject : testing::Test
{
    TxTest env;
    Account const alice{"alice"};
    std::uint32_t escrowSeq{};

    void
    SetUp() override
    {
        testing::Test::SetUp();
        env.createAccount(alice, XRP(5'000));

        auto const wasm = assembleWat(kWritesThenRejects);
        escrowSeq = env.getAccountRoot(alice).getSequence();

        auto builder = transactions::EscrowCreateBuilder{alice, alice, STAmount{XRP(1'000)}};
        builder.setBytecode(makeSlice(wasm));
        builder.setCancelAfter(closeTimeOffset(env, 1'000));

        ASSERT_EQ(env.submit(builder, alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
        env.close();
    }

    [[nodiscard]] ClosedResult
    finish()
    {
        auto builder = transactions::EscrowFinishBuilder{alice, alice, escrowSeq};
        builder.setGas(kAllowance);

        return env.submitAndClose(builder, alice, escrowFinishFee(env, kAllowance));
    }
};

// The point of the whole shape: a contract that rejects can still leave a record of why.
// `EscrowFinish` writes `sfData` *before* returning `tecBYTECODE_REJECTED`, and a `tec`
// keeps its ledger changes, so the escrow survives carrying what the contract wrote.
TEST_F(DataOnReject, ARejectingContractStillPersistsItsData)
{
    auto const result = finish();
    EXPECT_EQ(result.ter, tecBYTECODE_REJECTED);

    auto const sle =
        env.getOpenLedger().read(keylet::escrow(alice, SeqProxy::rawSequence(escrowSeq)));
    ASSERT_NE(sle, nullptr) << "a rejected finish must leave the escrow in place";
    ASSERT_TRUE(sle->isFieldPresent(sfData));

    auto const data = sle->getFieldVL(sfData);
    EXPECT_EQ(std::string(data.begin(), data.end()), "Data") << strHex(data);
}

// The reject code reaches the metadata, which is the only way a client learns *which*
// rejection it was — every contract-defined reject shares one TER.
TEST_F(DataOnReject, TheRejectCodeIsReportedInTheMetadata)
{
    auto const result = finish();
    ASSERT_TRUE(result.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    auto const meta = result.meta->getAsObject();
    ASSERT_TRUE(meta.isFieldPresent(sfVMReturnCode));

    EXPECT_EQ(meta.getFieldI32(sfVMReturnCode), kRejectCode);
}

// Gas is reported even though the run ended in a rejection: the engine has a trustworthy
// number whenever the contract ran to completion, and a reject is a completed run.
TEST_F(DataOnReject, GasIsChargedAndReportedForARejectedRun)
{
    auto const result = finish();
    ASSERT_TRUE(result.meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    auto const meta = result.meta->getAsObject();
    ASSERT_TRUE(meta.isFieldPresent(sfGasUsed));

    auto const used = meta.getFieldU32(sfGasUsed);
    EXPECT_GT(used, 0U);
    EXPECT_LE(used, kAllowance) << "the engine cannot spend more than it was given";
}

}  // namespace
}  // namespace xrpl::test

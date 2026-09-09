#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/EscrowCreate.h>
#include <xrpl/protocol_autogen/transactions/EscrowFinish.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/fixtures/EscrowWasm.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>

namespace xrpl::test {
namespace {

// allowance × gasPrice is a product of two 32-bit values, so a narrow intermediate would wrap
// and let a large allowance be bought for almost nothing. These pin that it costs a big fee.

// Near the default gas limit, so the product is as large as the transactor ever computes.
constexpr std::uint32_t kBigAllowance = 996'433;

struct GasFees : testing::Test
{
    TxTest env;
    Account const alice{"alice"};
    Account const carol{"carol"};
    std::uint32_t escrowSeq{};

    void
    SetUp() override
    {
        env.createAccount(alice, XRP(5'000));
        env.createAccount(carol, XRP(5'000));

        auto const wasm = assembleWat(kReadsLedgerSqn);
        escrowSeq = env.getAccountRoot(alice).getSequence();

        auto builder = transactions::EscrowCreateBuilder{alice, carol, STAmount{XRP(1'000)}};
        builder.setBytecode(makeSlice(wasm));
        builder.setCancelAfter(
            static_cast<std::uint32_t>(env.getCloseTime().time_since_epoch().count()) + 1'000);

        ASSERT_EQ(env.submit(builder, alice, escrowCreateFee(env, wasm)).ter, tesSUCCESS);
        env.close();
    }

    [[nodiscard]] TER
    finishPaying(XRPAmount fee)
    {
        auto builder = transactions::EscrowFinishBuilder{carol, alice, escrowSeq};
        builder.setGas(kBigAllowance);
        return env.submit(builder, carol, fee).ter;
    }
};

// If the product ever wrapped, this is the test that notices: 30 drops would start looking
// sufficient.
TEST_F(GasFees, ALargeAllowanceCannotBeBoughtForAFewDrops)
{
    auto const owed = escrowFinishFee(env, kBigAllowance);
    ASSERT_GT(owed.drops(), kBigAllowance) << "the fee must scale with the allowance";

    EXPECT_EQ(finishPaying(XRPAmount{30}), telINSUF_FEE_P);
}

TEST_F(GasFees, AFeeOneDropShortOfTheAllowanceIsRefused)
{
    EXPECT_EQ(finishPaying(escrowFinishFee(env, kBigAllowance) - XRPAmount{1}), telINSUF_FEE_P);
}

// Otherwise the two refusals above prove nothing: any fee at all might be rejected.
TEST_F(GasFees, TheExactFeeIsAccepted)
{
    EXPECT_EQ(finishPaying(escrowFinishFee(env, kBigAllowance)), tesSUCCESS);
}

// Asking for a near-limit budget does not mean spending it.
TEST_F(GasFees, OnlyTheGasActuallyUsedIsReported)
{
    auto builder = transactions::EscrowFinishBuilder{carol, alice, escrowSeq};
    builder.setGas(kBigAllowance);

    auto const result = env.submit(builder, carol, escrowFinishFee(env, kBigAllowance));
    ASSERT_EQ(result.ter, tesSUCCESS);
    env.close();

    auto const meta = env.getMetadata(result.tx->getTransactionID());
    ASSERT_TRUE(meta.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    auto const obj = meta->getAsObject();
    ASSERT_TRUE(obj.isFieldPresent(sfGasUsed));

    EXPECT_LT(obj.getFieldU32(sfGasUsed), kBigAllowance);
    EXPECT_EQ(obj.getFieldI32(sfVMReturnCode), 5);
}

}  // namespace
}  // namespace xrpl::test

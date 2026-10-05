#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// base_fee — no input, one scalar written out.
struct BaseFeeGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kFeeLen = 4;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFeeLen);

    static constexpr std::uint32_t kBaseFeeDrops = 0x2a3b4c5d;

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("base_fee", {outArg}, {}, answer);
    }
};

TEST_F(BaseFeeGuest, base_fee_reaches_guest_as_four_little_endian_bytes)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(Return(kBaseFeeDrops));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), static_cast<std::int32_t>(kBaseFeeDrops));
}

TEST_F(BaseFeeGuest, status_is_the_scalar_width)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(Return(kBaseFeeDrops));

    auto const wat = watFor(kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFeeLen);
}

TEST_F(BaseFeeGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getBaseFee())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(BaseFeeGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getBaseFee())
        .WillOnce(testing::Throw(std::runtime_error{"base fee came apart"}));

    auto const outcome = run(watFor(kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getBaseFee"));
}

}  // namespace xrpl::test

#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/BytesHelpers.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::test {

using testing::Return;

// tx_inner — a locator region in, bytes out.
struct TxNestedFieldGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kLocatorAt = 16;
    static constexpr std::int32_t kLocatorLen = 12;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOutLen = 32;
    static constexpr std::int32_t kValueLen = 6;

    static constexpr Arg kLocator = Arg::region(kLocatorAt, kLocatorLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kOutLen);

    // A negative step, so a sign or byte-order mistake in the wire form shows up.
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);

    // Six bytes, the first four distinctive so the guest's `i32.load` of them cannot pass by
    // accident and the reported length cannot be mistaken for that load's width.
    Bytes const value{0x0d, 0x0c, 0x0b, 0x0a, 0xee, 0xff};

    [[nodiscard]] std::string
    watFor(Arg locatorArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "tx_inner", {locatorArg, outArg}, {{.at = kLocatorAt, .bytes = locatorBytes}}, answer);
    }
};

TEST_F(TxNestedFieldGuest, locator_steps_reach_host_and_bytes_come_back)
{
    EXPECT_CALL(host, getTxNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

TEST_F(TxNestedFieldGuest, status_is_the_values_length)
{
    EXPECT_CALL(host, getTxNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kValueLen);
}

TEST_F(TxNestedFieldGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getTxNestedField(LocatorEquals(steps)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NotLeafField)));

    auto const wat = watFor(kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::NotLeafField));
}

TEST_F(TxNestedFieldGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, getTxNestedField(LocatorEquals(steps)))
        .WillOnce(testing::Throw(std::runtime_error{"tx nested field came apart"}));

    auto const outcome = run(watFor(kLocator, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getTxNestedField"));
}

TEST_F(TxNestedFieldGuest, empty_locator_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getTxNestedField).Times(0);

    auto const wat = watFor(Arg::region(kLocatorAt, 0), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// A locator is whole `i32` steps, so a length not divisible by four is malformed however many
// bytes it has.
TEST_F(TxNestedFieldGuest, misaligned_locator_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getTxNestedField).Times(0);

    auto const wat = watFor(Arg::region(kLocatorAt, kLocatorLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

}  // namespace xrpl::test

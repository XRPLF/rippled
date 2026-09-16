#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/BytesHelpers.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::test {

using testing::Return;

// home_le_inner — a locator region in, bytes out.
struct CurrentLedgerObjNestedFieldGuest : GuestCallTest
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
            "home_le_inner",
            {locatorArg, outArg},
            {{.at = kLocatorAt, .bytes = locatorBytes}},
            answer);
    }
};

TEST_F(CurrentLedgerObjNestedFieldGuest, LocatorStepsReachHostAndBytesComeBack)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

TEST_F(CurrentLedgerObjNestedFieldGuest, StatusIsTheValuesLength)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kValueLen);
}

TEST_F(CurrentLedgerObjNestedFieldGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField(LocatorEquals(steps)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NotLeafField)));

    auto const wat = watFor(kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::NotLeafField));
}

TEST_F(CurrentLedgerObjNestedFieldGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField(LocatorEquals(steps)))
        .WillOnce(testing::Throw(std::runtime_error{"current ledger obj nested field came apart"}));

    auto const outcome = run(watFor(kLocator, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjNestedField"));
}

TEST_F(CurrentLedgerObjNestedFieldGuest, EmptyLocatorIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField).Times(0);

    auto const wat = watFor(Arg::region(kLocatorAt, 0), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// A locator is whole `i32` steps, so a length not divisible by four is malformed however many
// bytes it has.
TEST_F(CurrentLedgerObjNestedFieldGuest, MisalignedLocatorLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField).Times(0);

    auto const wat = watFor(Arg::region(kLocatorAt, kLocatorLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(CurrentLedgerObjNestedFieldGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kLocatorLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CurrentLedgerObjNestedFieldGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField).Times(0);

    auto const wat = watFor(Arg::region(-1, kLocatorLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CurrentLedgerObjNestedFieldGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, Arg::outRegion(kOutAt, kValueLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(CurrentLedgerObjNestedFieldGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, Arg::outRegion(kOnePage, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CurrentLedgerObjNestedFieldGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedField(LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kLocator, Arg::outRegion(-1, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/BytesHelpers.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::test {

using testing::Return;

// home_le_inner_arr_len — a locator region in, the count answered directly. No out region, so
// no buffer-fit axis.
struct CurrentLedgerObjNestedArrayLenGuest : HostCallTest
{
    static constexpr std::int32_t kLocatorAt = 16;
    static constexpr std::int32_t kLocatorLen = 12;
    static constexpr std::int32_t kCount = 5;

    static constexpr Arg kLocator = Arg::region(kLocatorAt, kLocatorLen);

    // A negative step, so a sign or byte-order mistake in the wire form shows up.
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);

    [[nodiscard]] std::string
    watFor(Arg locatorArg) const
    {
        return hostCallWat(
            "home_le_inner_arr_len", {locatorArg}, {{.at = kLocatorAt, .bytes = locatorBytes}});
    }
};

TEST_F(CurrentLedgerObjNestedArrayLenGuest, LocatorStepsReachHostAndCountComesBack)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(kLocator)), kCount);
}

// `NoArray` is what a field that is not an array actually answers, so it stands for the host
// error axis here rather than an arbitrary code.
TEST_F(CurrentLedgerObjNestedArrayLenGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(hostAnswer(watFor(kLocator)), hfErrorToInt(HostFunctionError::NoArray));
}

// `guarded` turns the throw into `InternalFatal`, which the engine treats as fatal rather
// than passing back: the run ends, and the guest never resumes to read it.
TEST_F(CurrentLedgerObjNestedArrayLenGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(
            testing::Throw(std::runtime_error{"current ledger obj nested array len came apart"}));

    auto const outcome = callHost(watFor(kLocator));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjNestedArrayLen"));
}

// An empty region is in bounds, so the engine passes it on and `HostContext` is the one to
// refuse it — which is why the mock, one layer below, is never reached.
TEST_F(CurrentLedgerObjNestedArrayLenGuest, EmptyLocatorIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(Arg::region(kLocatorAt, 0));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// A locator is whole `i32` steps, so a length not divisible by four is malformed however many
// bytes it has.
TEST_F(CurrentLedgerObjNestedArrayLenGuest, MisalignedLocatorLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(Arg::region(kLocatorAt, kLocatorLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(CurrentLedgerObjNestedArrayLenGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kLocatorLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CurrentLedgerObjNestedArrayLenGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(Arg::region(-1, kLocatorLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

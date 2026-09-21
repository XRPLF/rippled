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

// le_inner_arr_len — a cache slot and a locator region in, the count answered directly. No out
// region, so no buffer-fit axis.
//
// The slot and the locator's pointer and length are three bare `i32`s on the wire, so each
// carries a value none of the others could be mistaken for.
struct LedgerObjNestedArrayLenGuest : GuestCallTest
{
    static constexpr std::int32_t kLocatorAt = 16;
    static constexpr std::int32_t kLocatorLen = 12;
    static constexpr std::int32_t kCount = 5;
    static constexpr std::int32_t kSlot = 7;
    static constexpr std::int32_t kNegativeSlot = -3;

    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);
    static constexpr Arg kLocator = Arg::region(kLocatorAt, kLocatorLen);

    // A negative step, so a sign or byte-order mistake in the wire form shows up.
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);

    [[nodiscard]] std::string
    watFor(Arg cacheIdxArg, Arg locatorArg) const
    {
        return hostCallWat(
            "le_inner_arr_len",
            {cacheIdxArg, locatorArg},
            {{.at = kLocatorAt, .bytes = locatorBytes}});
    }
};

TEST_F(LedgerObjNestedArrayLenGuest, SlotAndLocatorReachHostInOrderAndCountComesBack)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(kSlot, LocatorEquals(steps)))
        .WillOnce(Return(kCount));

    EXPECT_EQ(hostAnswer(watFor(kCacheIdx, kLocator)), kCount);
}

// The slot is the one scalar the ABI carries as signed, so a negative one reaches the host as
// itself and is the host's to refuse.
TEST_F(LedgerObjNestedArrayLenGuest, NegativeSlotCrossesVerbatim)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(kNegativeSlot, LocatorEquals(steps)))
        .WillOnce(Return(std::unexpected(HostFunctionError::SlotOutRange)));

    auto const wat = watFor(Arg::scalar(kNegativeSlot), kLocator);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::SlotOutRange));
}

// `NoArray` is what a field that is not an array actually answers, so it stands for the host
// error axis here rather than an arbitrary code.
TEST_F(LedgerObjNestedArrayLenGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(kSlot, LocatorEquals(steps)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(hostAnswer(watFor(kCacheIdx, kLocator)), hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(LedgerObjNestedArrayLenGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(kSlot, LocatorEquals(steps)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj nested array len came apart"}));

    auto const outcome = run(watFor(kCacheIdx, kLocator));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjNestedArrayLen"));
}

TEST_F(LedgerObjNestedArrayLenGuest, EmptyLocatorIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(kLocatorAt, 0));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// A locator is whole `i32` steps, so a length not divisible by four is malformed however many
// bytes it has.
TEST_F(LedgerObjNestedArrayLenGuest, MisalignedLocatorLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(kLocatorAt, kLocatorLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(LedgerObjNestedArrayLenGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(kOnePage, kLocatorLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LedgerObjNestedArrayLenGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(-1, kLocatorLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

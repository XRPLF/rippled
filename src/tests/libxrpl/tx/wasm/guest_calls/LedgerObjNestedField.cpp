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

// le_inner — a cache slot and a locator region in, bytes out.
//
// The slot, the locator's pointer and length and the out region's are five bare `i32`s on the
// wire, so each carries a value none of the others could be mistaken for.
//
// `abi.rs`'s `write_buffered` serves it, so the host is asked into a scratch buffer before
// the out region is looked at: all three out-region axes reach the mock.
struct LedgerObjNestedFieldGuest : HostCallTest
{
    static constexpr std::int32_t kLocatorAt = 16;
    static constexpr std::int32_t kLocatorLen = 12;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOutLen = 32;
    static constexpr std::int32_t kValueLen = 6;
    static constexpr std::int32_t kSlot = 7;
    static constexpr std::int32_t kNegativeSlot = -3;

    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);
    static constexpr Arg kLocator = Arg::region(kLocatorAt, kLocatorLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kOutLen);

    // A negative step, so a sign or byte-order mistake in the wire form shows up.
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);

    // Six bytes, the first four distinctive so the guest's `i32.load` of them cannot pass by
    // accident and the reported length cannot be mistaken for that load's width.
    Bytes const value{0x0d, 0x0c, 0x0b, 0x0a, 0xee, 0xff};

    [[nodiscard]] std::string
    watFor(Arg cacheIdxArg, Arg locatorArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "le_inner",
            {cacheIdxArg, locatorArg, outArg},
            {{.at = kLocatorAt, .bytes = locatorBytes}},
            answer);
    }
};

TEST_F(LedgerObjNestedFieldGuest, SlotAndLocatorReachHostInOrderAndBytesComeBack)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kSlot, LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

TEST_F(LedgerObjNestedFieldGuest, StatusIsTheValuesLength)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kSlot, LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, kLocator, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kValueLen);
}

// The slot is the one scalar the ABI carries as signed, so a negative one reaches the host as
// itself and is the host's to refuse.
TEST_F(LedgerObjNestedFieldGuest, NegativeSlotCrossesVerbatim)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kNegativeSlot, LocatorEquals(steps)))
        .WillOnce(Return(std::unexpected(HostFunctionError::SlotOutRange)));

    auto const wat = watFor(Arg::scalar(kNegativeSlot), kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::SlotOutRange));
}

TEST_F(LedgerObjNestedFieldGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kSlot, LocatorEquals(steps)))
        .WillOnce(Return(std::unexpected(HostFunctionError::NotLeafField)));

    auto const wat = watFor(kCacheIdx, kLocator, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::NotLeafField));
}

// `guarded` turns the throw into `InternalFatal`, which the engine treats as fatal rather
// than passing back: the run ends, and the guest never resumes to read it.
TEST_F(LedgerObjNestedFieldGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kSlot, LocatorEquals(steps)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj nested field came apart"}));

    auto const outcome = callHost(watFor(kCacheIdx, kLocator, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjNestedField"));
}

// An empty region is in bounds, so the engine passes it on and `HostContext` is the one to
// refuse it — which is why the mock, one layer below, is never reached.
TEST_F(LedgerObjNestedFieldGuest, EmptyLocatorIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedField).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(kLocatorAt, 0), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// A locator is whole `i32` steps, so a length not divisible by four is malformed however many
// bytes it has.
TEST_F(LedgerObjNestedFieldGuest, MisalignedLocatorLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedField).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(kLocatorAt, kLocatorLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(LedgerObjNestedFieldGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedField).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(kOnePage, kLocatorLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LedgerObjNestedFieldGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedField).Times(0);

    auto const wat = watFor(kCacheIdx, Arg::region(-1, kLocatorLen), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(LedgerObjNestedFieldGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kSlot, LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, kLocator, Arg::outRegion(kOutAt, kValueLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(LedgerObjNestedFieldGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kSlot, LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, kLocator, Arg::outRegion(kOnePage, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LedgerObjNestedFieldGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedField(kSlot, LocatorEquals(steps))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, kLocator, Arg::outRegion(-1, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

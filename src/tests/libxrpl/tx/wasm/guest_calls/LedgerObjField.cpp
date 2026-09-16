#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// le_field — a cache slot and a field code in, bytes out.
//
// Both leading arguments are bare `i32`s on the wire, so the slot and the field code are
// values that cannot be confused for one another: a swap in `CxxHost`'s forward would
// otherwise be invisible.
struct LedgerObjFieldGuest : GuestCallTest
{
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOutLen = 8;
    static constexpr std::int32_t kValueLen = 6;
    static constexpr std::int32_t kSlot = 7;
    static constexpr std::int32_t kNegativeSlot = -3;

    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kOutLen);

    // Six bytes, the first four distinctive so the guest's `i32.load` of them cannot pass by
    // accident and the reported length cannot be mistaken for that load's width.
    Bytes const value{0x0d, 0x0c, 0x0b, 0x0a, 0xee, 0xff};

    // A real field code, so the shim's `SField` lookup has something to find.
    static Arg
    field()
    {
        return Arg::scalar(sfBalance.getCode());
    }

    [[nodiscard]] static std::string
    watFor(Arg cacheIdxArg, Arg fieldArg, Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("le_field", {cacheIdxArg, fieldArg, outArg}, {}, answer);
    }
};

TEST_F(LedgerObjFieldGuest, SlotAndFieldCodeReachHostInOrder)
{
    EXPECT_CALL(host, getLedgerObjField(kSlot, testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, field(), kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kValueLen) << "the length the host reported";
}

TEST_F(LedgerObjFieldGuest, FieldBytesReachTheGuestsOutRegion)
{
    EXPECT_CALL(host, getLedgerObjField(kSlot, testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

TEST_F(LedgerObjFieldGuest, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjField).Times(0);

    auto const wat =
        watFor(kCacheIdx, Arg::scalar(0x7fff'0000), kOut);  // a type nothing is registered under
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidField));
}

// The slot is the one scalar the ABI carries as signed, so a negative one reaches the host as
// itself and is the host's to refuse.
TEST_F(LedgerObjFieldGuest, NegativeSlotCrossesVerbatim)
{
    EXPECT_CALL(host, getLedgerObjField(kNegativeSlot, testing::Ref(sfBalance)))
        .WillOnce(Return(std::unexpected(HostFunctionError::SlotOutRange)));

    auto const wat = watFor(Arg::scalar(kNegativeSlot), field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::SlotOutRange));
}

TEST_F(LedgerObjFieldGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerObjField(kSlot, testing::Ref(sfBalance)))
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    auto const wat = watFor(kCacheIdx, field(), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FieldNotFound));
}

TEST_F(LedgerObjFieldGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getLedgerObjField(kSlot, testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj field came apart"}));

    auto const outcome = run(watFor(kCacheIdx, field(), kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjField"));
}

TEST_F(LedgerObjFieldGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getLedgerObjField(kSlot, testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, field(), Arg::outRegion(kOutAt, kValueLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(LedgerObjFieldGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjField).Times(0);

    auto const wat = watFor(kCacheIdx, field(), Arg::outRegion(kOnePage, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LedgerObjFieldGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjField).Times(0);

    auto const wat = watFor(kCacheIdx, field(), Arg::outRegion(-1, kOutLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

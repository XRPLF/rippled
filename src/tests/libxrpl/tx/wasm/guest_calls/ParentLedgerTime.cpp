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

// parent_ldgr_time — no input, one scalar written out.
struct ParentLedgerTimeGuest : GuestCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kTimeLen = 4;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kTimeLen);

    static constexpr std::uint32_t kCloseTime = 0x5a6b7c8d;

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("parent_ldgr_time", {outArg}, {}, answer);
    }
};

TEST_F(ParentLedgerTimeGuest, CloseTimeReachesGuestAsFourLittleEndianBytes)
{
    EXPECT_CALL(host, getParentLedgerTime()).WillOnce(Return(kCloseTime));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), static_cast<std::int32_t>(kCloseTime));
}

TEST_F(ParentLedgerTimeGuest, StatusIsTheScalarWidth)
{
    EXPECT_CALL(host, getParentLedgerTime()).WillOnce(Return(kCloseTime));

    auto const wat = watFor(kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kTimeLen);
}

TEST_F(ParentLedgerTimeGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getParentLedgerTime())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(ParentLedgerTimeGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getParentLedgerTime())
        .WillOnce(testing::Throw(std::runtime_error{"parent ledger time came apart"}));

    auto const outcome = run(watFor(kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getParentLedgerTime"));
}

TEST_F(ParentLedgerTimeGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, getParentLedgerTime()).WillOnce(Return(kCloseTime));

    auto const wat = watFor(Arg::outRegion(kOutAt, kTimeLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(ParentLedgerTimeGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getParentLedgerTime).Times(0);

    auto const wat = watFor(Arg::outRegion(kOnePage, kTimeLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(ParentLedgerTimeGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getParentLedgerTime).Times(0);

    auto const wat = watFor(Arg::outRegion(-1, kTimeLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

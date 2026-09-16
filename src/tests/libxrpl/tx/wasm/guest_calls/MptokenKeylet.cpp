#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// mptoken_id — a 24-byte MPT id and a 20-byte holder in, a keylet region out. The two lengths
// differ, so a forward that swapped them would be caught by the length check before the bytes
// were ever compared.
struct MptokenKeyletGuest : GuestCallTest
{
    static constexpr std::int32_t kMptidAt = 0;
    static constexpr std::int32_t kHolderAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kMptidLen = static_cast<std::int32_t>(MPTID::size());
    static constexpr std::int32_t kHolderLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kKeyletLen = 32;

    static constexpr Arg kMptid = Arg::region(kMptidAt, kMptidLen);
    static constexpr Arg kHolder = Arg::region(kHolderAt, kHolderLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const mptidBytes = Bytes(MPTID::size(), 0x7a);
    Bytes const holderBytes = Bytes(AccountID::size(), 0x1d);
    MPTID const mptid = MPTID::fromVoid(mptidBytes.data());
    AccountID const holder = AccountID::fromVoid(holderBytes.data());

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg mptidArg, Arg holderArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "mptoken_id",
            {mptidArg, holderArg, outArg},
            {{.at = kMptidAt, .bytes = mptidBytes}, {.at = kHolderAt, .bytes = holderBytes}},
            answer);
    }
};

TEST_F(MptokenKeyletGuest, MptidAndHolderReachHostInOrderAndKeyletComesBack)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder)).WillOnce(Return(keylet));

    auto const wat = watFor(kMptid, kHolder, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(MptokenKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder)).WillOnce(Return(keylet));

    auto const wat = watFor(kMptid, kHolder, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(MptokenKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kMptid, kHolder, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(MptokenKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder))
        .WillOnce(testing::Throw(std::runtime_error{"mptoken keylet came apart"}));

    auto const outcome = run(watFor(kMptid, kHolder, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("mptokenKeylet"));
}

TEST_F(MptokenKeyletGuest, MptidOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenKeylet).Times(0);

    auto const wat = watFor(Arg::region(kMptidAt, kMptidLen - 1), kHolder, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenKeyletGuest, HolderOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenKeylet).Times(0);

    auto const wat = watFor(kMptid, Arg::region(kHolderAt, kHolderLen + 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kMptidLen), kHolder, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(MptokenKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, mptokenKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kMptidLen), kHolder, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(MptokenKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder)).WillOnce(Return(keylet));

    auto const wat = watFor(kMptid, kHolder, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(MptokenKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder)).WillOnce(Return(keylet));

    auto const wat = watFor(kMptid, kHolder, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(MptokenKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder)).WillOnce(Return(keylet));

    auto const wat = watFor(kMptid, kHolder, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

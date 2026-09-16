#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
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

// amm_id — two serialized assets in, a keylet region out.
//
// An asset's wire length selects its kind (`parseAsset`), so the two below are an MPT id and a
// currency followed by its issuer: two assets the host can tell apart.
struct AmmKeyletGuest : GuestCallTest
{
    static constexpr std::int32_t kAsset1At = 0;
    static constexpr std::int32_t kAsset2At = 32;
    static constexpr std::int32_t kOutAt = 96;
    static constexpr std::int32_t kMptIdLen = static_cast<std::int32_t>(MPTID::size());
    static constexpr std::int32_t kIssueLen =
        static_cast<std::int32_t>(Currency::size() + AccountID::size());
    static constexpr std::int32_t kKeyletLen = 32;

    static constexpr Arg kAsset1 = Arg::region(kAsset1At, kMptIdLen);
    static constexpr Arg kAsset2 = Arg::region(kAsset2At, kIssueLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const mptWire = Bytes(MPTID::size(), 0x7a);
    Bytes const issueWire = [] {
        Bytes bytes(Currency::size(), 0x42);
        bytes.insert(bytes.end(), AccountID::size(), 0x99);
        return bytes;
    }();

    Asset const asset1{MPTID::fromVoid(mptWire.data())};
    Asset const asset2{Issue{
        Currency::fromVoid(issueWire.data()),
        AccountID::fromVoid(issueWire.data() + Currency::size())}};

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg asset1Arg, Arg asset2Arg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "amm_id",
            {asset1Arg, asset2Arg, outArg},
            {{.at = kAsset1At, .bytes = mptWire}, {.at = kAsset2At, .bytes = issueWire}},
            answer);
    }
};

TEST_F(AmmKeyletGuest, MptAndIssueAssetsReachHostAndKeyletComesBack)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2))).WillOnce(Return(keylet));

    auto const wat = watFor(kAsset1, kAsset2, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(AmmKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2))).WillOnce(Return(keylet));

    auto const wat = watFor(kAsset1, kAsset2, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(AmmKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2)))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAsset1, kAsset2, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(AmmKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2)))
        .WillOnce(testing::Throw(std::runtime_error{"amm keylet came apart"}));

    auto const outcome = run(watFor(kAsset1, kAsset2, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("ammKeylet"));
}

TEST_F(AmmKeyletGuest, Asset1OfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, ammKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAsset1At, kMptIdLen - 1), kAsset2, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AmmKeyletGuest, Asset2OfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, ammKeylet).Times(0);

    auto const wat = watFor(kAsset1, Arg::region(kAsset2At, kIssueLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AmmKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, ammKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kMptIdLen), kAsset2, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(AmmKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, ammKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kMptIdLen), kAsset2, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AmmKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2))).WillOnce(Return(keylet));

    auto const wat = watFor(kAsset1, kAsset2, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(AmmKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2))).WillOnce(Return(keylet));

    auto const wat = watFor(kAsset1, kAsset2, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(AmmKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2))).WillOnce(Return(keylet));

    auto const wat = watFor(kAsset1, kAsset2, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

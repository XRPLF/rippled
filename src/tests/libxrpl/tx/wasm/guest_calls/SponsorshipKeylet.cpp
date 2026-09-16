#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// sponsorship_id — two account regions in, a keylet region out.
struct SponsorshipKeyletGuest : HostCallTest
{
    static constexpr std::int32_t kSponsorAt = 0;
    static constexpr std::int32_t kSponseeAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kKeyletLen = 32;

    static constexpr Arg kSponsor = Arg::region(kSponsorAt, kAccountLen);
    static constexpr Arg kSponsee = Arg::region(kSponseeAt, kAccountLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const sponsorBytes = Bytes(AccountID::size(), 0x51);
    Bytes const sponseeBytes = Bytes(AccountID::size(), 0xf4);
    AccountID const sponsor = AccountID::fromVoid(sponsorBytes.data());
    AccountID const sponsee = AccountID::fromVoid(sponseeBytes.data());

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg sponsorArg, Arg sponseeArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "sponsorship_id",
            {sponsorArg, sponseeArg, outArg},
            {{.at = kSponsorAt, .bytes = sponsorBytes}, {.at = kSponseeAt, .bytes = sponseeBytes}},
            answer);
    }
};

TEST_F(SponsorshipKeyletGuest, SponsorAndSponseeReachHostInOrderAndKeyletComesBack)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(Return(keylet));

    auto const wat = watFor(kSponsor, kSponsee, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(SponsorshipKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(Return(keylet));

    auto const wat = watFor(kSponsor, kSponsee, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(SponsorshipKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kSponsor, kSponsee, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(SponsorshipKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee))
        .WillOnce(testing::Throw(std::runtime_error{"sponsorship keylet came apart"}));

    auto const outcome = callHost(watFor(kSponsor, kSponsee, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("sponsorshipKeylet"));
}

TEST_F(SponsorshipKeyletGuest, SponsorOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, sponsorshipKeylet).Times(0);

    auto const wat = watFor(Arg::region(kSponsorAt, kAccountLen - 1), kSponsee, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(SponsorshipKeyletGuest, SponseeOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, sponsorshipKeylet).Times(0);

    auto const wat = watFor(kSponsor, Arg::region(kSponseeAt, kAccountLen + 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(SponsorshipKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, sponsorshipKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kAccountLen), kSponsee, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(SponsorshipKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, sponsorshipKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kAccountLen), kSponsee, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(SponsorshipKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(Return(keylet));

    auto const wat = watFor(kSponsor, kSponsee, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(SponsorshipKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(Return(keylet));

    auto const wat = watFor(kSponsor, kSponsee, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(SponsorshipKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(Return(keylet));

    auto const wat = watFor(kSponsor, kSponsee, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

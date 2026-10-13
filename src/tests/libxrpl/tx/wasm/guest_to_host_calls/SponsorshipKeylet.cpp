#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// sponsorship_id — two account regions in, a keylet region out.
struct SponsorshipKeyletGuest : GuestToHostCallTest
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

TEST_F(SponsorshipKeyletGuest, sponsor_and_sponsee_reach_host_in_order_and_keylet_comes_back)
{
    EXPECT_CALL(host, sponsorshipKeylet(sponsor, sponsee)).WillOnce(Return(keylet));

    auto const wat = watFor(kSponsor, kSponsee, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test

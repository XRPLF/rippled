#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// amm_id — two serialized assets in, a keylet region out.
//
// An asset's wire length selects its kind (`parseAsset`), so the two below are an MPT id and a
// currency followed by its issuer: two assets the host can tell apart.
struct AmmKeyletGuest : GuestToHostCallTest
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

TEST_F(AmmKeyletGuest, mpt_and_issue_assets_reach_host_and_keylet_comes_back)
{
    EXPECT_CALL(host, ammKeylet(Eq(asset1), Eq(asset2))).WillOnce(Return(keylet));

    auto const wat = watFor(kAsset1, kAsset2, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test

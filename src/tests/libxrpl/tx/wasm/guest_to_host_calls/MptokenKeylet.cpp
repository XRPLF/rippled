#include <xrpl/protocol/AccountID.h>
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

// mptoken_id — a 24-byte MPT id and a 20-byte holder in, a keylet region out. The two lengths
// differ, so a forward that swapped them would be caught by the length check before the bytes
// were ever compared.
struct MptokenKeyletGuest : GuestToHostCallTest
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

TEST_F(MptokenKeyletGuest, mptid_and_holder_reach_host_in_order_and_keylet_comes_back)
{
    EXPECT_CALL(host, mptokenKeylet(Eq(mptid), holder)).WillOnce(Return(keylet));

    auto const wat = watFor(kMptid, kHolder, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test

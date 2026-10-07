#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// deposit_preauth_id — two account regions in, a keylet region out.
struct DepositPreauthKeyletGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kAccountAt = 0;
    static constexpr std::int32_t kAuthorizeAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kKeyletLen = 32;

    static constexpr Arg kAccount = Arg::region(kAccountAt, kAccountLen);
    static constexpr Arg kAuthorize = Arg::region(kAuthorizeAt, kAccountLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const accountBytes = Bytes(AccountID::size(), 0x21);
    Bytes const authorizeBytes = Bytes(AccountID::size(), 0xa4);
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    AccountID const authorize = AccountID::fromVoid(authorizeBytes.data());

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg accountArg, Arg authorizeArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "deposit_preauth_id",
            {accountArg, authorizeArg, outArg},
            {{.at = kAccountAt, .bytes = accountBytes},
             {.at = kAuthorizeAt, .bytes = authorizeBytes}},
            answer);
    }
};

TEST_F(DepositPreauthKeyletGuest, account_and_authorize_reach_host_in_order_and_keylet_comes_back)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test

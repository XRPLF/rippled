#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// trustline_id — two account regions and a currency in, a keylet region out.
struct TrustLineKeyletGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kAccount1At = 0;
    static constexpr std::int32_t kAccount2At = 32;
    static constexpr std::int32_t kCurrencyAt = 64;
    static constexpr std::int32_t kOutAt = 96;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kCurrencyLen = static_cast<std::int32_t>(Currency::size());
    static constexpr std::int32_t kKeyletLen = 32;

    static constexpr Arg kAccount1 = Arg::region(kAccount1At, kAccountLen);
    static constexpr Arg kAccount2 = Arg::region(kAccount2At, kAccountLen);
    static constexpr Arg kCurrency = Arg::region(kCurrencyAt, kCurrencyLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const account1Bytes = Bytes(AccountID::size(), 0x31);
    Bytes const account2Bytes = Bytes(AccountID::size(), 0x63);
    Bytes const currencyBytes = Bytes(Currency::size(), 0x9c);
    AccountID const account1 = AccountID::fromVoid(account1Bytes.data());
    AccountID const account2 = AccountID::fromVoid(account2Bytes.data());
    Currency const currency = Currency::fromVoid(currencyBytes.data());

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(
        Arg account1Arg,
        Arg account2Arg,
        Arg currencyArg,
        Arg outArg,
        Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "trustline_id",
            {account1Arg, account2Arg, currencyArg, outArg},
            {{.at = kAccount1At, .bytes = account1Bytes},
             {.at = kAccount2At, .bytes = account2Bytes},
             {.at = kCurrencyAt, .bytes = currencyBytes}},
            answer);
    }
};

TEST_F(TrustLineKeyletGuest, accounts_and_currency_reach_host_in_order_and_keylet_comes_back)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount1, kAccount2, kCurrency, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test

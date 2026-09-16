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

using testing::Return;

// trustline_id — two account regions and a currency in, a keylet region out.
struct TrustLineKeyletGuest : GuestCallTest
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

TEST_F(TrustLineKeyletGuest, AccountsAndCurrencyReachHostInOrderAndKeyletComesBack)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount1, kAccount2, kCurrency, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(TrustLineKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount1, kAccount2, kCurrency, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(TrustLineKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount1, kAccount2, kCurrency, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(TrustLineKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(testing::Throw(std::runtime_error{"trust line keylet came apart"}));

    auto const outcome = run(watFor(kAccount1, kAccount2, kCurrency, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("trustLineKeylet"));
}

TEST_F(TrustLineKeyletGuest, Account1OfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAccount1At, kAccountLen - 1), kAccount2, kCurrency, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(TrustLineKeyletGuest, Account2OfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    auto const wat = watFor(kAccount1, Arg::region(kAccount2At, kAccountLen + 1), kCurrency, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

// `HostContext::trustLineKeylet` checks the currency's length ahead of either account's, but
// all three answer the same `InvalidParams`, so which check fired is not observable here.
TEST_F(TrustLineKeyletGuest, CurrencyOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    auto const wat = watFor(kAccount1, kAccount2, Arg::region(kCurrencyAt, kCurrencyLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(TrustLineKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kAccountLen), kAccount2, kCurrency, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(TrustLineKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kAccountLen), kAccount2, kCurrency, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(TrustLineKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency)).WillOnce(Return(keylet));

    auto const wat =
        watFor(kAccount1, kAccount2, kCurrency, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(TrustLineKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount1, kAccount2, kCurrency, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(TrustLineKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount1, kAccount2, kCurrency, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

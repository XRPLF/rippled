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

// deposit_preauth_id — two account regions in, a keylet region out.
struct DepositPreauthKeyletGuest : HostCallTest
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

TEST_F(DepositPreauthKeyletGuest, AccountAndAuthorizeReachHostInOrderAndKeyletComesBack)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(DepositPreauthKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(DepositPreauthKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount, kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(DepositPreauthKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize))
        .WillOnce(testing::Throw(std::runtime_error{"deposit preauth keylet came apart"}));

    auto const outcome = callHost(watFor(kAccount, kAuthorize, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("depositPreauthKeylet"));
}

TEST_F(DepositPreauthKeyletGuest, AccountOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, depositPreauthKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAccountAt, kAccountLen - 1), kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DepositPreauthKeyletGuest, AuthorizeOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, depositPreauthKeylet).Times(0);

    auto const wat = watFor(kAccount, Arg::region(kAuthorizeAt, kAccountLen + 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DepositPreauthKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, depositPreauthKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kAccountLen), kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(DepositPreauthKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, depositPreauthKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kAccountLen), kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DepositPreauthKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(DepositPreauthKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(DepositPreauthKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// did_id — one account region in, a keylet region out.
struct DidKeyletGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kAccountAt = 0;
    static constexpr std::int32_t kOutAt = 32;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kKeyletLen = 32;

    static constexpr Arg kAccount = Arg::region(kAccountAt, kAccountLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const accountBytes = Bytes(AccountID::size(), 0xd1);
    AccountID const account = AccountID::fromVoid(accountBytes.data());

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg accountArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "did_id", {accountArg, outArg}, {{.at = kAccountAt, .bytes = accountBytes}}, answer);
    }
};

TEST_F(DidKeyletGuest, account_reaches_host_and_keylet_comes_back)
{
    EXPECT_CALL(host, didKeylet(account)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(DidKeyletGuest, status_is_the_keylets_length)
{
    EXPECT_CALL(host, didKeylet(account)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(DidKeyletGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, didKeylet(account))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(DidKeyletGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, didKeylet(account))
        .WillOnce(testing::Throw(std::runtime_error{"did keylet came apart"}));

    auto const outcome = run(watFor(kAccount, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("didKeylet"));
}

TEST_F(DidKeyletGuest, account_of_the_wrong_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, didKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAccountAt, kAccountLen - 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

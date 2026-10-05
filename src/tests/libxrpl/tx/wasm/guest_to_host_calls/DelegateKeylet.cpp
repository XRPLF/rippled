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

// delegate_id — two account regions in, a keylet region out.
//
// What only this layer can show is that `CxxHost`'s hand-written forward
// (`crates/xrpl-wasm-vm-ffi/src/lib.rs`) hands the C++ host the regions the guest wrote, in
// the guest's order. `host_context/DelegateKeylet.cpp` enters below that forward and Rust's
// `tests/host_calls.rs` stops above it, so a swap there is invisible to both.
struct DelegateKeyletGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kAccountAt = 0;
    static constexpr std::int32_t kAuthorizeAt = 32;
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kKeyletLen = 32;

    // The three regions, well formed. A test passes these and replaces the one it is about,
    // so every call site spells out what it asks of all three.
    static constexpr Arg kAccount = Arg::region(kAccountAt, kAccountLen);
    static constexpr Arg kAuthorize = Arg::region(kAuthorizeAt, kAccountLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    // Distinct byte patterns: two copies of the same account would pass even if the two
    // regions were swapped on their way to the host.
    Bytes const accountBytes = Bytes(AccountID::size(), 0x11);
    Bytes const authorizeBytes = Bytes(AccountID::size(), 0xe1);
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    AccountID const authorize = AccountID::fromVoid(authorizeBytes.data());

    // A keylet whose first four bytes are distinctive, so the guest's `i32.load` of them
    // cannot pass by accident.
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
            "delegate_id",
            {accountArg, authorizeArg, outArg},
            {{.at = kAccountAt, .bytes = accountBytes},
             {.at = kAuthorizeAt, .bytes = authorizeBytes}},
            answer);
    }
};

TEST_F(DelegateKeyletGuest, both_accounts_reach_host_in_order_and_keylet_comes_back)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(DelegateKeyletGuest, status_is_the_keylets_length)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(DelegateKeyletGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount, kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(DelegateKeyletGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize))
        .WillOnce(testing::Throw(std::runtime_error{"delegate keylet came apart"}));

    auto const outcome = run(watFor(kAccount, kAuthorize, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("delegateKeylet"));
}

TEST_F(DelegateKeyletGuest, account_of_the_wrong_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, delegateKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAccountAt, kAccountLen - 1), kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DelegateKeyletGuest, authorize_of_the_wrong_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, delegateKeylet).Times(0);

    auto const wat = watFor(kAccount, Arg::region(kAuthorizeAt, kAccountLen + 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

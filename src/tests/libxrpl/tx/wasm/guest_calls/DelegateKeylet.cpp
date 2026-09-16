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

// delegate_id — two account regions in, a keylet region out.
//
// What only this layer can show is that `CxxHost`'s hand-written forward
// (`crates/xrpl-wasm-vm-ffi/src/lib.rs`) hands the C++ host the regions the guest wrote, in
// the guest's order. `host_context/DelegateKeylet.cpp` enters below that forward and Rust's
// `tests/host_calls.rs` stops above it, so a swap there is invisible to both.
struct DelegateKeyletGuest : HostCallTest
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

TEST_F(DelegateKeyletGuest, BothAccountsReachHostInOrderAndKeyletComesBack)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(DelegateKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(DelegateKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kAccount, kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

// `guarded` turns the throw into `InternalFatal`, and the engine treats that code as fatal
// rather than passing it back: the run ends, and the guest never resumes to read it. Only
// this layer can say so — `host_context/` sees the `InternalFatal` return and stops there.
TEST_F(DelegateKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize))
        .WillOnce(testing::Throw(std::runtime_error{"delegate keylet came apart"}));

    auto const outcome = callHost(watFor(kAccount, kAuthorize, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("delegateKeylet"));
}

// A 19-byte region is in bounds, so the engine passes it on and `HostContext` is the one to
// refuse it — which is why the mock, one layer below, is never reached.
TEST_F(DelegateKeyletGuest, AccountOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, delegateKeylet).Times(0);

    auto const wat = watFor(Arg::region(kAccountAt, kAccountLen - 1), kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DelegateKeyletGuest, AuthorizeOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, delegateKeylet).Times(0);

    auto const wat = watFor(kAccount, Arg::region(kAuthorizeAt, kAccountLen + 1), kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DelegateKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, delegateKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kAccountLen), kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(DelegateKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, delegateKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kAccountLen), kAuthorize, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

// The three cases below reach the host: this call reads guest memory as well as writing it,
// so `abi.rs`'s `write_buffered` judges the inputs, calls the host into a scratch buffer,
// and only then looks at where the answer was asked to go.
TEST_F(DelegateKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(DelegateKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(DelegateKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(Return(keylet));

    auto const wat = watFor(kAccount, kAuthorize, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

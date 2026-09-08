#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
//
// `account` and `authorize` are distinct byte patterns: a happy path built from two copies of
// the same account would still pass if the two were swapped.
struct DelegateKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    Bytes const authorizeBytes{0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
                               0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    AccountID const authorize = AccountID::fromVoid(authorizeBytes.data());
};

TEST_F(DelegateKeyletCall, AccountAndAuthorizeAreForwardedInOrderKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(DelegateKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(DelegateKeyletCall, MalformedAccountIsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, delegateKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(malformedAccount), bytesOf(authorizeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DelegateKeyletCall, MalformedAuthorizeIsRefusedWithoutAskingHost)
{
    Bytes const malformedAuthorize(AccountID::size() + 1, 0xe1);
    EXPECT_CALL(host, delegateKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(accountBytes), bytesOf(malformedAuthorize), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Both ids fail one combined length check, so a call malformed in both places answers the
// same `InvalidParams` as either alone; what's observable is that the host is never asked.
TEST_F(DelegateKeyletCall, BothAccountsMalformedIsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0x01);
    Bytes const malformedAuthorize(AccountID::size() - 1, 0xe1);
    EXPECT_CALL(host, delegateKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.delegateKeylet(
            bytesOf(malformedAccount), bytesOf(malformedAuthorize), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DelegateKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize))
        .WillOnce(testing::Throw(std::runtime_error{"delegate keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("delegate keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("delegateKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(DelegateKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(DelegateKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(DelegateKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, delegateKeylet(account, authorize)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.delegateKeylet(bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

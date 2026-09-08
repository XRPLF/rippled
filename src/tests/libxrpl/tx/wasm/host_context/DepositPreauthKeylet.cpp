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
struct DepositPreauthKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    Bytes const authorizeBytes{0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
                               0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    AccountID const authorize = AccountID::fromVoid(authorizeBytes.data());
};

TEST_F(DepositPreauthKeyletCall, AccountAndAuthorizeAreForwardedInOrderKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(DepositPreauthKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(DepositPreauthKeyletCall, MalformedAccountIsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, depositPreauthKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(malformedAccount), bytesOf(authorizeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DepositPreauthKeyletCall, MalformedAuthorizeIsRefusedWithoutAskingHost)
{
    Bytes const malformedAuthorize(AccountID::size() + 1, 0x91);
    EXPECT_CALL(host, depositPreauthKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(accountBytes), bytesOf(malformedAuthorize), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Both ids fail one combined length check, so a call malformed in both places answers the
// same `InvalidParams` as either alone; what's observable is that the host is never asked.
TEST_F(DepositPreauthKeyletCall, BothAccountsMalformedIsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0x01);
    Bytes const malformedAuthorize(AccountID::size() - 1, 0x91);
    EXPECT_CALL(host, depositPreauthKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(malformedAccount), bytesOf(malformedAuthorize), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DepositPreauthKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize))
        .WillOnce(testing::Throw(std::runtime_error{"deposit preauth keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("deposit preauth keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("depositPreauthKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(DepositPreauthKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(DepositPreauthKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(DepositPreauthKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, depositPreauthKeylet(account, authorize)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.depositPreauthKeylet(
            bytesOf(accountBytes), bytesOf(authorizeBytes), out.slice()),
        0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

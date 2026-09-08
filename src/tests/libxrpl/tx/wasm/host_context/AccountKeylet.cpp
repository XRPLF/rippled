#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
struct AccountKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
                             0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
};

TEST_F(AccountKeyletCall, AccountIsForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, accountKeylet(account)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(accountBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(AccountKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, accountKeylet(account))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(accountBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(AccountKeyletCall, ShortAccountIsRefusedWithoutAskingHost)
{
    Bytes const shortAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, accountKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(shortAccount), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AccountKeyletCall, LongAccountIsRefusedWithoutAskingHost)
{
    Bytes const longAccount(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, accountKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(longAccount), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AccountKeyletCall, EmptyAccountIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, accountKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(Bytes{}), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(AccountKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, accountKeylet(account))
        .WillOnce(testing::Throw(std::runtime_error{"account keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(accountBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("account keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("accountKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(AccountKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, accountKeylet(account)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(accountBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(AccountKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, accountKeylet(account)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.accountKeylet(bytesOf(accountBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(AccountKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, accountKeylet(account)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.accountKeylet(bytesOf(accountBytes), out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

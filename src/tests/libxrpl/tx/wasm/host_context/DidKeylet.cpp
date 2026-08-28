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
struct DidKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
                             0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
};

TEST_F(DidKeyletCall, AccountIsForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, didKeylet(account)).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(accountBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(DidKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, didKeylet(account))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(accountBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(DidKeyletCall, ShortAccountIsRefusedWithoutAskingHost)
{
    Bytes const shortAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, didKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(shortAccount), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DidKeyletCall, LongAccountIsRefusedWithoutAskingHost)
{
    Bytes const longAccount(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, didKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(longAccount), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DidKeyletCall, EmptyAccountIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, didKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(Bytes{}), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(DidKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, didKeylet(account))
        .WillOnce(testing::Throw(std::runtime_error{"did keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(accountBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("did keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("didKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(DidKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, didKeylet(account)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(accountBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(DidKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, didKeylet(account)).WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.didKeylet(bytesOf(accountBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(DidKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, didKeylet(account)).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.didKeylet(bytesOf(accountBytes), out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

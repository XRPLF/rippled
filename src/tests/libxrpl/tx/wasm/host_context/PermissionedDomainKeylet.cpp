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
struct PermissionedDomainKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    std::int32_t const seq = 12345;
};

TEST_F(PermissionedDomainKeyletCall, AccountAndSeqAreForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, permissionedDomainKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(PermissionedDomainKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, permissionedDomainKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(PermissionedDomainKeyletCall, ShortAccountIsRefusedWithoutAskingHost)
{
    Bytes const shortAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, permissionedDomainKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(shortAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PermissionedDomainKeyletCall, LongAccountIsRefusedWithoutAskingHost)
{
    Bytes const longAccount(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, permissionedDomainKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(longAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PermissionedDomainKeyletCall, EmptyAccountIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, permissionedDomainKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(Bytes{}), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PermissionedDomainKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, permissionedDomainKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Throw(std::runtime_error{"permissioned domain keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("permissioned domain keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("permissionedDomainKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(PermissionedDomainKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, permissionedDomainKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(PermissionedDomainKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, permissionedDomainKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.permissionedDomainKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(PermissionedDomainKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, permissionedDomainKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.permissionedDomainKeylet(bytesOf(accountBytes), seq, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

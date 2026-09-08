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
//
// `account` and `destination` are distinct byte patterns: a happy path built from two copies of
// the same account would still pass if the two were swapped.
struct PaychannelKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    Bytes const destinationBytes{0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
                                 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    AccountID const destination = AccountID::fromVoid(destinationBytes.data());
    std::int32_t const seq = 54321;
};

TEST_F(PaychannelKeyletCall, AccountsAndSeqAreForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, paychannelKeylet(account, destination, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(accountBytes), bytesOf(destinationBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(PaychannelKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(accountBytes), bytesOf(destinationBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(PaychannelKeyletCall, MalformedAccountIsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(malformedAccount), bytesOf(destinationBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PaychannelKeyletCall, MalformedDestinationIsRefusedWithoutAskingHost)
{
    Bytes const malformedDestination(AccountID::size() + 1, 0x71);
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(accountBytes), bytesOf(malformedDestination), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Both ids fail one combined length check, so a call malformed in both places answers the
// same `InvalidParams` as either alone; what's observable is that the host is never asked.
TEST_F(PaychannelKeyletCall, BothAccountsMalformedIsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0x01);
    Bytes const malformedDestination(AccountID::size() - 1, 0x71);
    EXPECT_CALL(host, paychannelKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(malformedAccount), bytesOf(malformedDestination), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(PaychannelKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Throw(std::runtime_error{"paychannel keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(accountBytes), bytesOf(destinationBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("paychannel keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("paychannelKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(PaychannelKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, paychannelKeylet(account, destination, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(accountBytes), bytesOf(destinationBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(PaychannelKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, paychannelKeylet(account, destination, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(accountBytes), bytesOf(destinationBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(PaychannelKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, paychannelKeylet(account, destination, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.paychannelKeylet(
            bytesOf(accountBytes), bytesOf(destinationBytes), seq, out.slice()),
        0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

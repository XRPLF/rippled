#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>
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
// `account1` and `account2` are distinct byte patterns: a happy path built from two copies of
// the same account would still pass if the two were swapped.
struct TrustLineKeyletCall : HostContextTest
{
    Bytes const account1Bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                              0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    Bytes const account2Bytes{0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
                              0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44};
    Bytes const currencyBytes{0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
                              0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74};
    AccountID const account1 = AccountID::fromVoid(account1Bytes.data());
    AccountID const account2 = AccountID::fromVoid(account2Bytes.data());
    Currency const currency = Currency::fromVoid(currencyBytes.data());
};

TEST_F(TrustLineKeyletCall, AccountsAndCurrencyAreForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes), bytesOf(account2Bytes), bytesOf(currencyBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(TrustLineKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes), bytesOf(account2Bytes), bytesOf(currencyBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(TrustLineKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(testing::Throw(std::runtime_error{"trust line keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes), bytesOf(account2Bytes), bytesOf(currencyBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("trust line keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("trustLineKeylet"));
}

TEST_F(TrustLineKeyletCall, MalformedAccount1IsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount1(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(malformedAccount1),
            bytesOf(account2Bytes),
            bytesOf(currencyBytes),
            out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(TrustLineKeyletCall, MalformedAccount2IsRefusedWithoutAskingHost)
{
    Bytes const malformedAccount2(AccountID::size() + 1, 0x31);
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes),
            bytesOf(malformedAccount2),
            bytesOf(currencyBytes),
            out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(TrustLineKeyletCall, MalformedCurrencyIsRefusedWithoutAskingHost)
{
    Bytes const malformedCurrency(Currency::size() - 1, 0x61);
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes),
            bytesOf(account2Bytes),
            bytesOf(malformedCurrency),
            out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The currency length is checked before either account's, but every malformed shape answers
// the same `InvalidParams`, so a call malformed in both places cannot show which check fired.
// What's observable: the host is never asked.
TEST_F(TrustLineKeyletCall, CurrencyAndAccountBothMalformedIsRefusedWithoutAskingHost)
{
    Bytes const malformedCurrency(Currency::size() - 1, 0x61);
    Bytes const malformedAccount1(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, trustLineKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(malformedAccount1),
            bytesOf(account2Bytes),
            bytesOf(malformedCurrency),
            out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(TrustLineKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes), bytesOf(account2Bytes), bytesOf(currencyBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(TrustLineKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes), bytesOf(account2Bytes), bytesOf(currencyBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(TrustLineKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, trustLineKeylet(account1, account2, currency))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.trustLineKeylet(
            bytesOf(account1Bytes), bytesOf(account2Bytes), bytesOf(currencyBytes), out.slice()),
        0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

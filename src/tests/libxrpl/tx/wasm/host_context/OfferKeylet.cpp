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
struct OfferKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
                             0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    std::int32_t const seq = 24680;
};

TEST_F(OfferKeyletCall, AccountAndSeqAreForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, offerKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(OfferKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, offerKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(OfferKeyletCall, ShortAccountIsRefusedWithoutAskingHost)
{
    Bytes const shortAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, offerKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(shortAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OfferKeyletCall, LongAccountIsRefusedWithoutAskingHost)
{
    Bytes const longAccount(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, offerKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(longAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OfferKeyletCall, EmptyAccountIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, offerKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(Bytes{}), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(OfferKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, offerKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Throw(std::runtime_error{"offer keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("offer keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("offerKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(OfferKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, offerKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(OfferKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, offerKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.offerKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(OfferKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, offerKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.offerKeylet(bytesOf(accountBytes), seq, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

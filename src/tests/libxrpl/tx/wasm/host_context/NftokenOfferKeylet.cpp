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
struct NftokenOfferKeyletCall : HostContextTest
{
    Bytes const accountBytes{0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
                             0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    std::int32_t const seq = 13579;
};

TEST_F(NftokenOfferKeyletCall, AccountAndSeqAreForwardedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, nftokenOfferKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(NftokenOfferKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, nftokenOfferKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NftokenOfferKeyletCall, ShortAccountIsRefusedWithoutAskingHost)
{
    Bytes const shortAccount(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, nftokenOfferKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(shortAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NftokenOfferKeyletCall, LongAccountIsRefusedWithoutAskingHost)
{
    Bytes const longAccount(AccountID::size() + 1, 0x01);
    EXPECT_CALL(host, nftokenOfferKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(longAccount), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NftokenOfferKeyletCall, EmptyAccountIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, nftokenOfferKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(Bytes{}), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(NftokenOfferKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, nftokenOfferKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Throw(std::runtime_error{"nftoken offer keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(accountBytes), seq, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("nftoken offer keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("nftokenOfferKeylet"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(NftokenOfferKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, nftokenOfferKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NftokenOfferKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, nftokenOfferKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.nftokenOfferKeylet(bytesOf(accountBytes), seq, out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(NftokenOfferKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, nftokenOfferKeylet(account, static_cast<std::uint32_t>(seq)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.nftokenOfferKeylet(bytesOf(accountBytes), seq, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

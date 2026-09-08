#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
struct NFTIssuerCall : HostContextTest
{
    Bytes const nftIdBytes{0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b,
                           0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                           0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70};
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());
};

TEST_F(NFTIssuerCall, NftIdBytesBecomeTypedArgumentHostIsAskedFor)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getNFTIssuer(testing::Eq(nftId))).WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFTIssuer(bytesOf(nftIdBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(NFTIssuerCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getNFTIssuer(testing::Eq(nftId)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFTIssuer(bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTIssuerCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getNFTIssuer(testing::Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft issuer came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFTIssuer(bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("nft issuer came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTIssuer"));
}

TEST_F(NFTIssuerCall, MalformedNftIdIsRefusedWithoutAskingHost)
{
    Bytes const malformedNftId(uint256::size() - 1, 0xff);
    EXPECT_CALL(host, getNFTIssuer).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFTIssuer(bytesOf(malformedNftId), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(NFTIssuerCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getNFTIssuer(testing::Eq(nftId))).WillOnce(testing::Return(value));

    OutRegion out{value.size() - 1};
    EXPECT_EQ(
        hostContext.getNFTIssuer(bytesOf(nftIdBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTIssuerCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getNFTIssuer(testing::Eq(nftId))).WillOnce(testing::Return(value));

    OutRegion out{value.size()};
    EXPECT_EQ(
        hostContext.getNFTIssuer(bytesOf(nftIdBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(NFTIssuerCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, getNFTIssuer(testing::Eq(nftId))).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getNFTIssuer(bytesOf(nftIdBytes), out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

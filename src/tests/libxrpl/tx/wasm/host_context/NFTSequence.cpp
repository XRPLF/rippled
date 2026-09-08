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
struct NFTSequenceCall : HostContextTest
{
    Bytes const nftIdBytes{0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
                           0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
                           0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0};
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());
    static constexpr std::uint32_t kSequence = 0x89abcdef;
    Bytes const expectedBytes = bytesOfScalar(kSequence);
};

TEST_F(NFTSequenceCall, NftIdBytesBecomeTypedArgumentHostIsAskedFor)
{
    EXPECT_CALL(host, getNFTSequence(testing::Eq(nftId))).WillOnce(testing::Return(kSequence));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getNFTSequence(bytesOf(nftIdBytes), out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

TEST_F(NFTSequenceCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getNFTSequence(testing::Eq(nftId)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getNFTSequence(bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTSequenceCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getNFTSequence(testing::Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft sequence came apart"}));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getNFTSequence(bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("nft sequence came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTSequence"));
}

TEST_F(NFTSequenceCall, MalformedNftIdIsRefusedWithoutAskingHost)
{
    Bytes const malformedNftId(uint256::size() - 1, 0xff);
    EXPECT_CALL(host, getNFTSequence).Times(0);

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getNFTSequence(bytesOf(malformedNftId), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(NFTSequenceCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, getNFTSequence(testing::Eq(nftId))).WillOnce(testing::Return(kSequence));

    OutRegion out{3};
    EXPECT_EQ(hostContext.getNFTSequence(bytesOf(nftIdBytes), out.slice()), 4);
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTSequenceCall, OutRegionOfExactSizeIsWritten)
{
    EXPECT_CALL(host, getNFTSequence(testing::Eq(nftId))).WillOnce(testing::Return(kSequence));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getNFTSequence(bytesOf(nftIdBytes), out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

}  // namespace xrpl::test

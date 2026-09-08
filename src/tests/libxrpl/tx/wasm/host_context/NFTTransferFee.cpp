#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
// `getNFTTransferFee` answers its value directly rather than through `answer`, so there is no
// out region and no axis E.
struct NFTTransferFeeCall : HostContextTest
{
    Bytes const nftIdBytes{0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
                           0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
                           0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0};
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());
};

TEST_F(NFTTransferFeeCall, NftIdBytesBecomeTypedArgumentHostIsAskedFor)
{
    static constexpr std::int32_t kTransferFee = 314;
    EXPECT_CALL(host, getNFTTransferFee(testing::Eq(nftId)))
        .WillOnce(testing::Return(kTransferFee));

    EXPECT_EQ(hostContext.getNFTTransferFee(bytesOf(nftIdBytes)), kTransferFee);
}

TEST_F(NFTTransferFeeCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getNFTTransferFee(testing::Eq(nftId)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    EXPECT_EQ(
        hostContext.getNFTTransferFee(bytesOf(nftIdBytes)),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTTransferFeeCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getNFTTransferFee(testing::Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft transfer fee came apart"}));

    EXPECT_EQ(
        hostContext.getNFTTransferFee(bytesOf(nftIdBytes)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("nft transfer fee came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTTransferFee"));
}

TEST_F(NFTTransferFeeCall, MalformedNftIdIsRefusedWithoutAskingHost)
{
    Bytes const malformedNftId(uint256::size() - 1, 0xff);
    EXPECT_CALL(host, getNFTTransferFee).Times(0);

    EXPECT_EQ(
        hostContext.getNFTTransferFee(bytesOf(malformedNftId)),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

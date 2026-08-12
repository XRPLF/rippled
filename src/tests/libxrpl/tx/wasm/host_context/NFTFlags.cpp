#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
// `getNFTFlags` answers its value directly rather than through `answer`, so there is no out
// region and no axis E.
struct NFTFlagsCall : HostContextTest
{
    Bytes const nftIdBytes{0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb,
                           0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6,
                           0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0};
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());
};

TEST_F(NFTFlagsCall, NftIdBytesBecomeTypedArgumentHostIsAskedFor)
{
    static constexpr std::int32_t kFlags = 0x0b;
    EXPECT_CALL(host, getNFTFlags(testing::Eq(nftId))).WillOnce(testing::Return(kFlags));

    EXPECT_EQ(hostContext.getNFTFlags(bytesOf(nftIdBytes)), kFlags);
}

TEST_F(NFTFlagsCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getNFTFlags(testing::Eq(nftId)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    EXPECT_EQ(
        hostContext.getNFTFlags(bytesOf(nftIdBytes)),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(NFTFlagsCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getNFTFlags(testing::Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft flags came apart"}));

    EXPECT_EQ(
        hostContext.getNFTFlags(bytesOf(nftIdBytes)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("nft flags came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTFlags"));
}

TEST_F(NFTFlagsCall, MalformedNftIdIsRefusedWithoutAskingHost)
{
    Bytes const malformedNftId(uint256::size() - 1, 0xff);
    EXPECT_CALL(host, getNFTFlags).Times(0);

    EXPECT_EQ(
        hostContext.getNFTFlags(bytesOf(malformedNftId)),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// `getNFTFlags` answers its value directly rather than through `answer`, so a legitimate
// flags word with the high bit set is bit-for-bit the same value as
// `HostFunctionError::InternalFatal` (`INT32_MIN`) - the code `guarded` supplies for a thrown
// exception. The ABI at this layer has no way to tell the two apart; this is a property of
// the shape, not a bug to fix.
TEST_F(NFTFlagsCall, HighBitFlagsAreIndistinguishableFromInternalFatal)
{
    EXPECT_CALL(host, getNFTFlags(testing::Eq(nftId)))
        .WillOnce(testing::Return(std::numeric_limits<std::int32_t>::min()));

    EXPECT_EQ(
        hostContext.getNFTFlags(bytesOf(nftIdBytes)),
        hfErrorToInt(HostFunctionError::InternalFatal));
}

}  // namespace xrpl::test

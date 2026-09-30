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
struct NFTTaxonCall : HostContextTest
{
    Bytes const nftIdBytes{0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
                           0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86,
                           0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90};
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());
    static constexpr std::uint32_t kTaxon = 0x12345678;
    Bytes const expectedBytes = bytesOfScalar(kTaxon);
};

TEST_F(NFTTaxonCall, nft_id_bytes_become_typed_argument_host_is_asked_for)
{
    EXPECT_CALL(host, getNFTTaxon(testing::Eq(nftId))).WillOnce(testing::Return(kTaxon));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getNFTTaxon(bytesOf(nftIdBytes), out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

TEST_F(NFTTaxonCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getNFTTaxon(testing::Eq(nftId)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getNFTTaxon(bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTTaxonCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, getNFTTaxon(testing::Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft taxon came apart"}));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getNFTTaxon(bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("nft taxon came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getNFTTaxon"));
}

TEST_F(NFTTaxonCall, malformed_nft_id_is_refused_without_asking_host)
{
    Bytes const malformedNftId(uint256::size() - 1, 0xff);
    EXPECT_CALL(host, getNFTTaxon).Times(0);

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getNFTTaxon(bytesOf(malformedNftId), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(NFTTaxonCall, short_out_region_writes_nothing_and_returns_true_length)
{
    EXPECT_CALL(host, getNFTTaxon(testing::Eq(nftId))).WillOnce(testing::Return(kTaxon));

    OutRegion out{3};
    EXPECT_EQ(hostContext.getNFTTaxon(bytesOf(nftIdBytes), out.slice()), 4);
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTTaxonCall, out_region_of_exact_size_is_written)
{
    EXPECT_CALL(host, getNFTTaxon(testing::Eq(nftId))).WillOnce(testing::Return(kTaxon));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getNFTTaxon(bytesOf(nftIdBytes), out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

}  // namespace xrpl::test

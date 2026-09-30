#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, the field cap, guest memory - are tested on the Rust
// side, not here.
struct NFTCall : HostContextTest
{
    Bytes const accountBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    Bytes const nftIdBytes{0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
                           0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
                           0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40};
    AccountID const account = AccountID::fromVoid(accountBytes.data());
    uint256 const nftId = uint256::fromVoid(nftIdBytes.data());
};

TEST_F(NFTCall, account_and_nft_id_become_typed_arguments_host_is_asked_for)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getNFT(testing::Eq(account), testing::Eq(nftId)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(accountBytes), bytesOf(nftIdBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(NFTCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getNFT(testing::Eq(account), testing::Eq(nftId)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(accountBytes), bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, getNFT(testing::Eq(account), testing::Eq(nftId)))
        .WillOnce(testing::Throw(std::runtime_error{"nft came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(accountBytes), bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("nft came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getNFT"));
}

TEST_F(NFTCall, malformed_account_is_refused_without_asking_host)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0xff);
    EXPECT_CALL(host, getNFT).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(malformedAccount), bytesOf(nftIdBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Distinct from a malformed account: the account is well-formed here, so this exercises the
// nft id's own check rather than the account's.
TEST_F(NFTCall, malformed_nft_id_is_refused_without_asking_host)
{
    Bytes const malformedNftId(uint256::size() - 1, 0xff);
    EXPECT_CALL(host, getNFT).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(accountBytes), bytesOf(malformedNftId), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The account's length is checked before the nft id's, but both checks answer `InvalidParams`,
// so which one fired is not observable here. What is: neither argument reaches the host.
TEST_F(NFTCall, both_arguments_malformed_is_refused_without_asking_host)
{
    Bytes const malformedAccount(AccountID::size() - 1, 0xff);
    Bytes const malformedNftId(uint256::size() - 1, 0xff);
    EXPECT_CALL(host, getNFT).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(malformedAccount), bytesOf(malformedNftId), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(NFTCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getNFT(testing::Eq(account), testing::Eq(nftId)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size() - 1};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(accountBytes), bytesOf(nftIdBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(NFTCall, out_region_of_exact_size_is_written)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getNFT(testing::Eq(account), testing::Eq(nftId)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size()};
    EXPECT_EQ(
        hostContext.getNFT(bytesOf(accountBytes), bytesOf(nftIdBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(NFTCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, getNFT(testing::Eq(account), testing::Eq(nftId)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getNFT(bytesOf(accountBytes), bytesOf(nftIdBytes), out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

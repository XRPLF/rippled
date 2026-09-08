#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>
#include <tx/wasm/MockHostFunctions.h>

#include <expected>
#include <stdexcept>

namespace xrpl::test {

// `checkSignature` validates nothing: message, signature and pubkey reach the host exactly as
// given, with no length check on any of them - deliberately, not by oversight.
struct CheckSignatureCall : HostContextTest
{
    Bytes const message{'m', 's', 'g'};
    Bytes const signature{'s', 'i', 'g'};
    Bytes const pubkey{'k', 'e', 'y'};
};

TEST_F(CheckSignatureCall, MessageSignatureAndPubkeyForwardedVerbatim)
{
    EXPECT_CALL(host, checkSignature(BytesAre("msg"), BytesAre("sig"), BytesAre("key")))
        .WillOnce(testing::Return(1));

    EXPECT_EQ(hostContext.checkSignature(bytesOf(message), bytesOf(signature), bytesOf(pubkey)), 1);
}

// The absence of any length check is a decision, not an oversight: empty slices are not a
// malformed shape here, they reach the host like any other.
TEST_F(CheckSignatureCall, EmptySlicesReachHostUnvalidated)
{
    auto const isEmpty = testing::Property(&Slice::empty, true);
    EXPECT_CALL(host, checkSignature(isEmpty, isEmpty, isEmpty)).WillOnce(testing::Return(0));

    EXPECT_EQ(hostContext.checkSignature(bytesOf(Bytes{}), bytesOf(Bytes{}), bytesOf(Bytes{})), 0);
}

TEST_F(CheckSignatureCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, checkSignature(BytesAre("msg"), BytesAre("sig"), BytesAre("key")))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::InvalidParams)));

    EXPECT_EQ(
        hostContext.checkSignature(bytesOf(message), bytesOf(signature), bytesOf(pubkey)),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CheckSignatureCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, checkSignature(BytesAre("msg"), BytesAre("sig"), BytesAre("key")))
        .WillOnce(testing::Throw(std::runtime_error{"signature check came apart"}));

    EXPECT_EQ(
        hostContext.checkSignature(bytesOf(message), bytesOf(signature), bytesOf(pubkey)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("signature check came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("checkSignature"));
}

}  // namespace xrpl::test

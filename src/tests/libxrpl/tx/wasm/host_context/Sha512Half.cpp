#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>
#include <tx/wasm/MockHostFunctions.h>

#include <expected>
#include <stdexcept>

namespace xrpl::test {

// `host_calls/Sha512Half.cpp` runs the digest through the engine; what is left at this layer is
// its own contract - the out-region rule, `guarded`, and an empty input.
struct Sha512HalfDirectCall : HostContextTest
{
    Bytes const data{'a', 'b', 'c'};
    Bytes const digestBytes = Bytes(32, 0x0a);
    Hash const digest = uint256::fromVoid(digestBytes.data());
};

TEST_F(Sha512HalfDirectCall, DataForwardedAndDigestWritten)
{
    EXPECT_CALL(host, computeSha512HalfHash(BytesAre("abc"))).WillOnce(testing::Return(digest));

    OutRegion out{32};
    EXPECT_EQ(hostContext.sha512Half(bytesOf(data), out.slice()), 32);
    EXPECT_TRUE(out.holds(bytesOf(digestBytes)));
}

TEST_F(Sha512HalfDirectCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, computeSha512HalfHash)
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::InvalidParams)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sha512Half(bytesOf(data), out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(Sha512HalfDirectCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, computeSha512HalfHash)
        .WillOnce(testing::Throw(std::runtime_error{"sha512 half came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.sha512Half(bytesOf(data), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("sha512 half came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("sha512Half"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(Sha512HalfDirectCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, computeSha512HalfHash(BytesAre("abc"))).WillOnce(testing::Return(digest));

    OutRegion out{31};
    EXPECT_EQ(hostContext.sha512Half(bytesOf(data), out.slice()), 32);
    EXPECT_FALSE(out.wasWritten());
}

// Nothing in the hash requires a non-empty input, so an empty slice is hashed like any other,
// not refused.
TEST_F(Sha512HalfDirectCall, EmptyInputIsHashedLikeAnyOther)
{
    EXPECT_CALL(host, computeSha512HalfHash(testing::Property(&Slice::empty, true)))
        .WillOnce(testing::Return(digest));

    OutRegion out{32};
    EXPECT_EQ(hostContext.sha512Half(bytesOf(Bytes{}), out.slice()), 32);
    EXPECT_TRUE(out.holds(bytesOf(digestBytes)));
}

}  // namespace xrpl::test

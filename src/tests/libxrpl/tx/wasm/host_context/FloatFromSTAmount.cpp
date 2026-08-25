#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

namespace {

Bytes
serialized(STAmount const& amount)
{
    Serializer s;
    amount.add(s);
    return s.getData();
}

}  // namespace

// The only file exercising `parseST<STAmount>`. A malformed buffer throws inside `STAmount`'s
// deserializing constructor; `parseST` catches that itself, so the host is never asked - unlike
// a `guarded`-caught throw from the host's own body.
struct FloatFromSTAmountCall : HostContextTest
{
    STAmount const amount{XRPAmount{1000}};
    Bytes const wireBytes = serialized(amount);
    std::int32_t const mode = 1;
};

TEST_F(FloatFromSTAmountCall, SerializedAmountDecodesToValueHostIsAskedFor)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatFromSTAmountCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatFromSTAmountCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Throw(std::runtime_error{"float from st amount came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float from st amount came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromSTAmount"));
}

// `parseST` catches its own failure: a malformed buffer never reaches the host at all.
TEST_F(FloatFromSTAmountCall, MalformedBytesAreRefusedWithoutAskingHost)
{
    Bytes const malformedBytes{0xff, 0xff, 0xff};
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(malformedBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatFromSTAmountCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

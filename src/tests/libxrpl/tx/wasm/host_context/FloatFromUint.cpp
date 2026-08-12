#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The only file exercising `parseUint64`: exactly eight bytes, little-endian.
struct FloatFromUintCall : HostContextTest
{
    // Every byte distinct, so a byte-order mistake in `parseUint64` would decode to a
    // different value rather than the same one by coincidence.
    std::uint64_t const value = 0x0102'0304'0506'0708ULL;
    Bytes const wireBytes = bytesOfScalar(value);
    std::int32_t const mode = 1;
};

TEST_F(FloatFromUintCall, LittleEndianWireBytesDecodeToValueHostIsAskedFor)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromUint(value, mode)).WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatFromUintCall, ModeIsForwardedVerbatim)
{
    std::int32_t const nonsenseMode = -12345;
    Bytes const result{1};
    EXPECT_CALL(host, floatFromUint(value, nonsenseMode)).WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(wireBytes), nonsenseMode, out.slice()),
        static_cast<std::int32_t>(result.size()));
}

TEST_F(FloatFromUintCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromUint(value, mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatInputMalformed)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatInputMalformed));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatFromUintCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatFromUint(value, mode))
        .WillOnce(testing::Throw(std::runtime_error{"uint came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("uint came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromUint"));
}

TEST_F(FloatFromUintCall, SevenByteRegionIsRefusedWithoutAskingHost)
{
    Bytes const shortBytes(7, 0);
    EXPECT_CALL(host, floatFromUint).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(shortBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromUintCall, NineByteRegionIsRefusedWithoutAskingHost)
{
    Bytes const longBytes(9, 0);
    EXPECT_CALL(host, floatFromUint).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(longBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromUintCall, EmptyRegionIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromUint).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(Bytes{}), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatFromUintCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromUint(value, mode)).WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatFromUintCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromUint(value, mode)).WillOnce(testing::Return(result));

    OutRegion out{result.size()};
    EXPECT_EQ(
        hostContext.floatFromUint(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

}  // namespace xrpl::test

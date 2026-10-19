#include <xrpl/tx/wasm/HostConstants.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/BytesHelpers.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// No D or F axis: `getBaseFee` takes no argument, so there is nothing to decode wrong and
// nothing whose forwarded identity to check.
struct BaseFeeCall : HostContextTest
{
    static constexpr std::uint32_t kBaseFee = 0x12345678;
    Bytes const expectedBytes = bytesOfScalar(kBaseFee);
};

TEST_F(BaseFeeCall, host_value_is_written_as_little_endian_bytes)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(testing::Return(kBaseFee));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

TEST_F(BaseFeeCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getBaseFee())
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FieldNotFound)));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getBaseFee(out.slice()), HostFailure{HostFunctionError::FieldNotFound}.toInt());
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(BaseFeeCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, getBaseFee())
        .WillOnce(testing::Throw(std::runtime_error{"base fee came apart"}));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getBaseFee(out.slice()), HostFailure{HostTrapError::InternalFatal}.toInt());
    EXPECT_THAT(logged(), testing::HasSubstr("base fee came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getBaseFee"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(BaseFeeCall, short_out_region_writes_nothing_and_returns_true_length)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(testing::Return(kBaseFee));

    OutRegion out{3};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), 4);
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(BaseFeeCall, out_region_of_exact_size_is_written)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(testing::Return(kBaseFee));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

// Cross-cutting: every `HostFailure` code crosses unchanged at this layer.
// `HostContext` does not distinguish a soft code from a fatal one, so `InternalFatal` crosses
// the same as any other, whether the host returns it directly or `guarded` supplies it for a
// throw.
TEST_F(BaseFeeCall, every_host_failure_code_crosses_unchanged)
{
    static HostFailure const kAllErrors[] = {
        HostFunctionError::FieldNotFound,
        HostFunctionError::BufferTooSmall,
        HostFunctionError::NoArray,
        HostFunctionError::NotLeafField,
        HostFunctionError::LocatorMalformed,
        HostFunctionError::SlotOutRange,
        HostFunctionError::SlotsFull,
        HostFunctionError::EmptySlot,
        HostFunctionError::LedgerObjNotFound,
        HostFunctionError::OutOfTransferLimit,
        HostFunctionError::DataFieldTooLarge,
        HostFunctionError::PointerOutOfBounds,
        HostFunctionError::InvalidParams,
        HostFunctionError::InvalidAccount,
        HostFunctionError::InvalidField,
        HostFunctionError::IndexOutOfBounds,
        HostFunctionError::FloatInputMalformed,
        HostFunctionError::FloatComputationError,
        HostTrapError::InternalFatal,
    };

    HostFailure refused = HostFunctionError::FieldNotFound;
    EXPECT_CALL(host, getBaseFee())
        .WillRepeatedly([&refused] -> std::expected<std::uint32_t, HostFailure> {
            return std::unexpected(refused);
        });

    for (auto const error : kAllErrors)
    {
        refused = error;

        OutRegion out{4};
        EXPECT_EQ(hostContext.getBaseFee(out.slice()), error.toInt());
        EXPECT_FALSE(out.wasWritten());
    }
}

}  // namespace xrpl::test

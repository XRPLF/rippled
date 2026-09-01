#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
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

TEST_F(BaseFeeCall, HostValueIsWrittenAsLittleEndianBytes)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(testing::Return(kBaseFee));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

TEST_F(BaseFeeCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getBaseFee())
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::Unimplemented)));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), hfErrorToInt(HostFunctionError::Unimplemented));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(BaseFeeCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getBaseFee())
        .WillOnce(testing::Throw(std::runtime_error{"base fee came apart"}));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("base fee came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getBaseFee"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(BaseFeeCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(testing::Return(kBaseFee));

    OutRegion out{3};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), 4);
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(BaseFeeCall, OutRegionOfExactSizeIsWritten)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(testing::Return(kBaseFee));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getBaseFee(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

// Cross-cutting: every `HostFunctionError` code crosses `hfErrorToInt` unchanged at this layer.
// Unlike the engine-side `WasmVMTest.SoftHostErrorCodesCrossUnchanged`, nothing is excluded
// here - `HostContext` does not distinguish a soft code from a fatal one, so `Unimplemented`
// and `NoMemExported` cross the same as any other. `InternalFatal` sits outside the -1..-20
// run other codes occupy (it is `INT32_MIN`), and crosses the same whether the host returns it
// directly or `guarded` supplies it for a throw.
TEST_F(BaseFeeCall, EveryHostFunctionErrorCodeCrossesHfErrorToIntUnchanged)
{
    static constexpr HostFunctionError kAllErrors[] = {
        HostFunctionError::Unimplemented,       HostFunctionError::FieldNotFound,
        HostFunctionError::BufferTooSmall,      HostFunctionError::NoArray,
        HostFunctionError::NotLeafField,        HostFunctionError::LocatorMalformed,
        HostFunctionError::SlotOutRange,        HostFunctionError::SlotsFull,
        HostFunctionError::EmptySlot,           HostFunctionError::LedgerObjNotFound,
        HostFunctionError::OutOfTransferLimit,  HostFunctionError::DataFieldTooLarge,
        HostFunctionError::PointerOutOfBounds,  HostFunctionError::NoMemExported,
        HostFunctionError::InvalidParams,       HostFunctionError::InvalidAccount,
        HostFunctionError::InvalidField,        HostFunctionError::IndexOutOfBounds,
        HostFunctionError::FloatInputMalformed, HostFunctionError::FloatComputationError,
        HostFunctionError::InternalFatal,
    };

    auto refused = HostFunctionError::Unimplemented;
    EXPECT_CALL(host, getBaseFee())
        .WillRepeatedly([&refused]() -> std::expected<std::uint32_t, HostFunctionError> {
            return std::unexpected(refused);
        });

    for (auto const error : kAllErrors)
    {
        refused = error;

        OutRegion out{4};
        EXPECT_EQ(hostContext.getBaseFee(out.slice()), hfErrorToInt(error));
        EXPECT_FALSE(out.wasWritten());
    }
}

}  // namespace xrpl::test

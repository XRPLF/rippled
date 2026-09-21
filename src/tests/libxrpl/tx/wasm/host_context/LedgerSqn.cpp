#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/BytesHelpers.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// No D or F axis: `getLedgerSqn` takes no argument, so there is nothing to decode wrong and
// nothing whose forwarded identity to check.
//
struct LedgerSqnCall : HostContextTest
{
    static constexpr std::uint32_t kLedgerSqn = 0x12345678;
    Bytes const expectedBytes = bytesOfScalar(kLedgerSqn);
};

TEST_F(LedgerSqnCall, HostValueIsWrittenAsLittleEndianBytes)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(testing::Return(kLedgerSqn));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getLedgerSqn(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

TEST_F(LedgerSqnCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::Unimplemented)));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getLedgerSqn(out.slice()), hfErrorToInt(HostFunctionError::Unimplemented));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerSqnCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(testing::Throw(std::runtime_error{"ledger sqn came apart"}));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getLedgerSqn(out.slice()), hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("ledger sqn came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerSqn"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(LedgerSqnCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(testing::Return(kLedgerSqn));

    OutRegion out{3};
    EXPECT_EQ(hostContext.getLedgerSqn(out.slice()), 4);
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerSqnCall, OutRegionOfExactSizeIsWritten)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(testing::Return(kLedgerSqn));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getLedgerSqn(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

}  // namespace xrpl::test

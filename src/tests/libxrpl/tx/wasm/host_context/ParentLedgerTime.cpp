#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// No D or F axis: `getParentLedgerTime` takes no argument, so there is nothing to decode wrong
// and nothing whose forwarded identity to check.
struct ParentLedgerTimeCall : HostContextTest
{
    static constexpr std::uint32_t kParentLedgerTime = 0x12345678;
    Bytes const expectedBytes = bytesOfScalar(kParentLedgerTime);
};

TEST_F(ParentLedgerTimeCall, HostValueIsWrittenAsLittleEndianBytes)
{
    EXPECT_CALL(host, getParentLedgerTime()).WillOnce(testing::Return(kParentLedgerTime));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getParentLedgerTime(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

TEST_F(ParentLedgerTimeCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getParentLedgerTime())
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::Unimplemented)));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getParentLedgerTime(out.slice()),
        hfErrorToInt(HostFunctionError::Unimplemented));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(ParentLedgerTimeCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getParentLedgerTime())
        .WillOnce(testing::Throw(std::runtime_error{"parent ledger time came apart"}));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getParentLedgerTime(out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("parent ledger time came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getParentLedgerTime"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(ParentLedgerTimeCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, getParentLedgerTime()).WillOnce(testing::Return(kParentLedgerTime));

    OutRegion out{3};
    EXPECT_EQ(hostContext.getParentLedgerTime(out.slice()), 4);
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(ParentLedgerTimeCall, OutRegionOfExactSizeIsWritten)
{
    EXPECT_CALL(host, getParentLedgerTime()).WillOnce(testing::Return(kParentLedgerTime));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getParentLedgerTime(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

}  // namespace xrpl::test

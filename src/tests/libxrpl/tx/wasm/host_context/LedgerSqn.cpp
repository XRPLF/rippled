#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// No D or F axis: `getLedgerSqn` takes no argument, so there is nothing to decode wrong and
// nothing whose forwarded identity to check.
//
// Named `LedgerSqnDirectCall`, not `LedgerSqnCall`: `host_calls/LedgerSqn.cpp` already owns
// that name in the same gtest binary.
struct LedgerSqnDirectCall : HostContextTest
{
    static constexpr std::uint32_t kLedgerSqn = 0x12345678;
    Bytes const expectedBytes = bytesOfScalar(kLedgerSqn);
};

TEST_F(LedgerSqnDirectCall, host_value_is_written_as_little_endian_bytes)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(testing::Return(kLedgerSqn));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getLedgerSqn(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

TEST_F(LedgerSqnDirectCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::Unimplemented)));

    OutRegion out{4};
    EXPECT_EQ(
        hostContext.getLedgerSqn(out.slice()), hfErrorToInt(HostFunctionError::Unimplemented));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerSqnDirectCall, host_exception_becomes_internal_fatal_and_is_logged)
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
TEST_F(LedgerSqnDirectCall, short_out_region_writes_nothing_and_returns_true_length)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(testing::Return(kLedgerSqn));

    OutRegion out{3};
    EXPECT_EQ(hostContext.getLedgerSqn(out.slice()), 4);
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerSqnDirectCall, out_region_of_exact_size_is_written)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(testing::Return(kLedgerSqn));

    OutRegion out{4};
    EXPECT_EQ(hostContext.getLedgerSqn(out.slice()), 4);
    EXPECT_TRUE(out.holds(bytesOf(expectedBytes)));
}

}  // namespace xrpl::test

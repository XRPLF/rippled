#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// No D or F axis: `getParentLedgerHash` takes no argument, so there is nothing to decode wrong
// and nothing whose forwarded identity to check.
//
// Unlike `getLedgerSqn`/`getParentLedgerTime`, the result is a `Hash` (a `uint256`) written
// whole through `answer` (`invoke<false>`), not a scalar through `answerScalar` - so it is
// asserted as bytes, the way `TxField.cpp` asserts its `Bytes` result, rather than as a
// little-endian scalar.
struct ParentLedgerHashCall : HostContextTest
{
    Bytes const hashBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                          0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                          0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};
    Hash const hash = uint256::fromVoid(hashBytes.data());
};

TEST_F(ParentLedgerHashCall, HostValueIsWrittenAsBytes)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(testing::Return(hash));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getParentLedgerHash(out.slice()), static_cast<std::int32_t>(hashBytes.size()));
    EXPECT_TRUE(out.holds(bytesOf(hashBytes)));
}

TEST_F(ParentLedgerHashCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getParentLedgerHash())
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getParentLedgerHash(out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(ParentLedgerHashCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getParentLedgerHash())
        .WillOnce(testing::Throw(std::runtime_error{"parent ledger hash came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getParentLedgerHash(out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("parent ledger hash came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getParentLedgerHash"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(ParentLedgerHashCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(testing::Return(hash));

    OutRegion out{hashBytes.size() - 1};
    EXPECT_EQ(
        hostContext.getParentLedgerHash(out.slice()), static_cast<std::int32_t>(hashBytes.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(ParentLedgerHashCall, OutRegionOfExactSizeIsWritten)
{
    EXPECT_CALL(host, getParentLedgerHash()).WillOnce(testing::Return(hash));

    OutRegion out{hashBytes.size()};
    EXPECT_EQ(
        hostContext.getParentLedgerHash(out.slice()), static_cast<std::int32_t>(hashBytes.size()));
    EXPECT_TRUE(out.holds(bytesOf(hashBytes)));
}

}  // namespace xrpl::test

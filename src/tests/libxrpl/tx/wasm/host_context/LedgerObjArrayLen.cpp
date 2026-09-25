#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// `getLedgerObjArrayLen` answers its count directly rather than through an out region: no axis
// E, no `OutRegion`, and the happy path asserts the returned count.
struct LedgerObjArrayLenCall : HostContextTest
{
    std::int32_t fieldCode = sfBalance.getCode();
    std::int32_t cacheIdx = 7;
};

TEST_F(LedgerObjArrayLenCall, field_code_becomes_sfield_host_is_asked_for)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(5));

    EXPECT_EQ(hostContext.getLedgerObjArrayLen(cacheIdx, fieldCode), 5);
}

// `NoArray` is what a field that is not an array actually answers, so it stands in for axis B
// here rather than an arbitrary code.
TEST_F(LedgerObjArrayLenCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(
        hostContext.getLedgerObjArrayLen(cacheIdx, fieldCode),
        hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(LedgerObjArrayLenCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, getLedgerObjArrayLen(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj array len came apart"}));

    EXPECT_EQ(
        hostContext.getLedgerObjArrayLen(cacheIdx, fieldCode),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("ledger obj array len came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjArrayLen"));
}

TEST_F(LedgerObjArrayLenCall, unknown_field_code_is_refused_without_asking_host)
{
    fieldCode = 0x7fff'0000;  // a code nothing is registered under
    EXPECT_CALL(host, getLedgerObjArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getLedgerObjArrayLen(cacheIdx, fieldCode),
        hfErrorToInt(HostFunctionError::InvalidField));
}

// `cacheIdx` is forwarded verbatim, including the two values a guest is likeliest to send: 0
// (pick a free slot) and a negative one.
TEST_F(LedgerObjArrayLenCall, cache_idx_of_zero_is_forwarded_verbatim)
{
    cacheIdx = 0;
    EXPECT_CALL(host, getLedgerObjArrayLen(0, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(5));

    EXPECT_EQ(hostContext.getLedgerObjArrayLen(cacheIdx, fieldCode), 5);
}

TEST_F(LedgerObjArrayLenCall, negative_cache_idx_is_forwarded_verbatim)
{
    cacheIdx = -7;
    EXPECT_CALL(host, getLedgerObjArrayLen(-7, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(5));

    EXPECT_EQ(hostContext.getLedgerObjArrayLen(cacheIdx, fieldCode), 5);
}

}  // namespace xrpl::test

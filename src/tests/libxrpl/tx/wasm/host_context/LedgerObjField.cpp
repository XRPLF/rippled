#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, the field cap, guest memory - are tested on the Rust
// side, not here. The cross-cutting cases over this shape already live in `TxField.cpp`.
struct LedgerObjFieldCall : HostContextTest
{
    std::int32_t fieldCode = sfBalance.getCode();
    std::int32_t cacheIdx = 7;
};

TEST_F(LedgerObjFieldCall, field_code_becomes_sfield_host_is_asked_for)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjField(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(LedgerObjFieldCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getLedgerObjField(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FieldNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::FieldNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerObjFieldCall, unknown_field_code_is_refused_without_asking_host)
{
    fieldCode = 0x7fff'0000;  // a code nothing is registered under
    EXPECT_CALL(host, getLedgerObjField).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(LedgerObjFieldCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, getLedgerObjField(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj field came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("ledger obj field came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjField"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(LedgerObjFieldCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjField(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size() - 1};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerObjFieldCall, out_region_of_exact_size_is_written)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjField(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size()};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(LedgerObjFieldCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, getLedgerObjField(cacheIdx, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

// `cacheIdx` is forwarded verbatim, including the two values a guest is likeliest to send: 0
// (pick a free slot) and a negative one.
TEST_F(LedgerObjFieldCall, cache_idx_of_zero_is_forwarded_verbatim)
{
    cacheIdx = 0;
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjField(0, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
}

TEST_F(LedgerObjFieldCall, negative_cache_idx_is_forwarded_verbatim)
{
    cacheIdx = -7;
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjField(-7, testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjField(cacheIdx, fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
}

}  // namespace xrpl::test

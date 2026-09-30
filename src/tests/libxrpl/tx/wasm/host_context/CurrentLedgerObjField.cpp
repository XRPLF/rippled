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
// side, not here. The cross-cutting cases over this shape - a non-`std::exception` throw, and
// a length past `kMaxWasmDataLength` - already live in `TxField.cpp`.
//
// Named `CurrentLedgerObjFieldDirectCall`, not `CurrentLedgerObjFieldCall`:
// `host_calls/CurrentLedgerObjField.cpp` already owns that name in the same gtest binary.
struct CurrentLedgerObjFieldDirectCall : HostContextTest
{
    std::int32_t fieldCode = sfBalance.getCode();
};

TEST_F(CurrentLedgerObjFieldDirectCall, field_code_becomes_sfield_host_is_asked_for)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getCurrentLedgerObjField(fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(CurrentLedgerObjFieldDirectCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FieldNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getCurrentLedgerObjField(fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::FieldNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(CurrentLedgerObjFieldDirectCall, unknown_field_code_is_refused_without_asking_host)
{
    fieldCode = 0x7fff'0000;  // a code nothing is registered under
    EXPECT_CALL(host, getCurrentLedgerObjField).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getCurrentLedgerObjField(fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(CurrentLedgerObjFieldDirectCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"current ledger obj field came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getCurrentLedgerObjField(fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("current ledger obj field came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjField"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(CurrentLedgerObjFieldDirectCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size() - 1};
    EXPECT_EQ(
        hostContext.getCurrentLedgerObjField(fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(CurrentLedgerObjFieldDirectCall, out_region_of_exact_size_is_written)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size()};
    EXPECT_EQ(
        hostContext.getCurrentLedgerObjField(fieldCode, out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(CurrentLedgerObjFieldDirectCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getCurrentLedgerObjField(fieldCode, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

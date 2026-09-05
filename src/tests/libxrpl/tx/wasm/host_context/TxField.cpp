#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, the field cap, guest memory - are tested on the Rust
// side, not here.
struct TxFieldCall : HostContextTest
{
    std::int32_t fieldCode = sfBalance.getCode();
};

TEST_F(TxFieldCall, FieldCodeBecomesSFieldHostIsAskedFor)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()), static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(TxFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FieldNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::FieldNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(TxFieldCall, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    fieldCode = 0x7fff'0000;  // a code nothing is registered under
    EXPECT_CALL(host, getTxField).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(TxFieldCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"balance field came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("balance field came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getTxField"));
}

// `guarded`'s `catch (...)` arm, for a thrown value that is not a `std::exception`.
TEST_F(TxFieldCall, NonStandardThrowBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(testing::Throw(42));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("getTxField"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(TxFieldCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(testing::Return(value));

    OutRegion out{value.size() - 1};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()), static_cast<std::int32_t>(value.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(TxFieldCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(testing::Return(value));

    OutRegion out{value.size()};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()), static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

// `kMaxWasmDataLength` is the engine's cap, not `HostContext`'s: a length past it crosses
// unchanged here, where the sibling engine test sees `DataFieldTooLarge` instead.
TEST_F(TxFieldCall, LengthPastProtocolCapCrossesUnchanged)
{
    Bytes const value(kMaxWasmDataLength + 1, 0xab);
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(testing::Return(value));

    OutRegion out{value.size()};
    EXPECT_EQ(
        hostContext.getTxField(fieldCode, out.slice()), static_cast<std::int32_t>(value.size()));
}

TEST_F(TxFieldCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, getTxField(testing::Ref(sfBalance))).WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getTxField(fieldCode, out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

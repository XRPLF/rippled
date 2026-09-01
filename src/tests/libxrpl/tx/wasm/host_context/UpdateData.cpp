#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The other non-`const` host method; it answers the byte count stored directly, with no out
// region.
struct UpdateDataCall : HostContextTest
{
    Bytes const data{'h', 'e', 'l', 'l', 'o'};
};

TEST_F(UpdateDataCall, DataForwardedByteCountReturned)
{
    EXPECT_CALL(host, updateData(BytesAre("hello"))).WillOnce(testing::Return(5));

    EXPECT_EQ(hostContext.updateData(bytesOf(data)), 5);
}

TEST_F(UpdateDataCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, updateData(BytesAre("hello")))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::DataFieldTooLarge)));

    EXPECT_EQ(
        hostContext.updateData(bytesOf(data)), hfErrorToInt(HostFunctionError::DataFieldTooLarge));
}

TEST_F(UpdateDataCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, updateData(BytesAre("hello")))
        .WillOnce(testing::Throw(std::runtime_error{"update data came apart"}));

    EXPECT_EQ(
        hostContext.updateData(bytesOf(data)), hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("update data came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("updateData"));
}

// An empty `rust::Slice` has a null `data()`; `updateData` forwards it as an empty `Slice`
// rather than treating it as malformed.
TEST_F(UpdateDataCall, EmptyInputRegionForwardsAsEmptySlice)
{
    EXPECT_CALL(host, updateData(testing::Property(&Slice::empty, true)))
        .WillOnce(testing::Return(0));

    EXPECT_EQ(hostContext.updateData(bytesOf(Bytes{})), 0);
}

}  // namespace xrpl::test

#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <expected>
#include <stdexcept>

namespace xrpl::test {

// Every input slice passes straight through to the host, unlike `invokeWithAccount`'s
// twenty-byte check or `parseUint64`'s eight: nothing here is validated, so there is no D axis.
// `x` and `y` carry different content, so a call that swapped them would fail to match.
// `floatCompare` answers its comparison directly rather than through `answer`, so there is no
// out region and no axis E.
struct FloatCompareCall : HostContextTest
{
    Bytes const x{'c', 'm', 'p', '-', 'x'};
    Bytes const y{'c', 'm', 'p', '-', 'y', 'y'};
};

TEST_F(FloatCompareCall, XAndYAreForwardedResultReturnedDirectly)
{
    EXPECT_CALL(host, floatCompare(BytesAre("cmp-x"), BytesAre("cmp-yy")))
        .WillOnce(testing::Return(1));

    EXPECT_EQ(hostContext.floatCompare(bytesOf(x), bytesOf(y)), 1);
}

TEST_F(FloatCompareCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatCompare(BytesAre("cmp-x"), BytesAre("cmp-yy")))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    EXPECT_EQ(
        hostContext.floatCompare(bytesOf(x), bytesOf(y)),
        hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatCompareCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatCompare(BytesAre("cmp-x"), BytesAre("cmp-yy")))
        .WillOnce(testing::Throw(std::runtime_error{"float compare came apart"}));

    EXPECT_EQ(
        hostContext.floatCompare(bytesOf(x), bytesOf(y)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float compare came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatCompare"));
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatCompareCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const oddX{0x2a};
    EXPECT_CALL(host, floatCompare(testing::_, BytesAre("cmp-yy"))).WillOnce(testing::Return(0));

    EXPECT_EQ(hostContext.floatCompare(bytesOf(oddX), bytesOf(y)), 0);
}

}  // namespace xrpl::test

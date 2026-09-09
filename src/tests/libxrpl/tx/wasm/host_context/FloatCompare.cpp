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
        .WillOnce(testing::Return(FloatOrdering::Greater));

    EXPECT_EQ(
        hostContext.floatCompare(bytesOf(x), bytesOf(y)),
        floatOrderingToInt(FloatOrdering::Greater));
}

// The verdict the host names is lowered to its wire code and nothing else — this layer is
// where `FloatOrdering` stops being a type and becomes the `i32` a contract reads. Every
// variant, so a mis-lowered one cannot hide behind a sibling that happens to be right.
TEST_F(FloatCompareCall, EveryVerdictIsLoweredToItsWireCode)
{
    for (auto const verdict : {FloatOrdering::Equal, FloatOrdering::Greater, FloatOrdering::Less})
    {
        EXPECT_CALL(host, floatCompare(BytesAre("cmp-x"), BytesAre("cmp-yy")))
            .WillOnce(testing::Return(verdict));

        EXPECT_EQ(hostContext.floatCompare(bytesOf(x), bytesOf(y)), floatOrderingToInt(verdict));
    }
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
    EXPECT_CALL(host, floatCompare(testing::_, BytesAre("cmp-yy")))
        .WillOnce(testing::Return(FloatOrdering::Equal));

    EXPECT_EQ(
        hostContext.floatCompare(bytesOf(oddX), bytesOf(y)),
        floatOrderingToInt(FloatOrdering::Equal));
}

}  // namespace xrpl::test

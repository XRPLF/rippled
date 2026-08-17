#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct FloatCompareImpl : FloatTest
{
};

TEST_F(FloatCompareImpl, MalformedInputs)
{
    // A wrong-size (here empty) buffer is malformed; the impl normalizes any well-formed
    // 12-byte buffer, so size is the only rejection.
    expectError(makeHost()->floatCompare(Slice{}, Slice{}), HostFunctionError::FloatInputMalformed);
    expectError(
        makeHost()->floatCompare(slice(floats::kOne), Slice{}),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatCompareImpl, Less)
{
    expectValue(makeHost()->floatCompare(slice(floats::kIntMin), slice(floats::kIntZero)), 2);
}

TEST_F(FloatCompareImpl, Greater)
{
    expectValue(makeHost()->floatCompare(slice(floats::kIntMax), slice(floats::kIntZero)), 1);
}

TEST_F(FloatCompareImpl, Equal)
{
    expectValue(makeHost()->floatCompare(slice(floats::kOne), slice(floats::kOne)), 0);
}

// A non-canonical encoding of 10 (mantissa 100000, exponent -4) is normalized on decode, so
// it compares equal to the canonical 10 — the impl accepts any well-formed 12-byte buffer.
TEST_F(FloatCompareImpl, NonCanonicalNormalizes)
{
    Bytes const nonCanonicalTen{
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x86, 0xA0, 0xFF, 0xFF, 0xFF, 0xFC};
    expectValue(makeHost()->floatCompare(slice(nonCanonicalTen), slice(floats::kTen)), 0);
}

}  // namespace xrpl::test

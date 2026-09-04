#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatCompareImpl : FloatTest
{
};

TEST_F(FloatCompareImpl, MalformedInputs)
{
    // A wrong-size (here empty) buffer is malformed; the impl normalizes any well-formed
    // 12-byte buffer, so size is the only rejection.
    auto h = makeHost();
    expectError(h->floatCompare(Slice{}, Slice{}), HostFunctionError::FloatInputMalformed);
    expectError(
        h->floatCompare(slice(FloatTest::kOne), Slice{}), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatCompareImpl, Less)
{
    expectValue(makeHost()->floatCompare(slice(FloatTest::kIntMin), slice(FloatTest::kIntZero)), 2);
}

TEST_F(FloatCompareImpl, Greater)
{
    expectValue(makeHost()->floatCompare(slice(FloatTest::kIntMax), slice(FloatTest::kIntZero)), 1);
}

TEST_F(FloatCompareImpl, Equal)
{
    expectValue(makeHost()->floatCompare(slice(FloatTest::kOne), slice(FloatTest::kOne)), 0);
}

// A non-canonical encoding of 10 (mantissa 100000, exponent -4) is normalized on decode, so
// it compares equal to the canonical 10 — the impl accepts any well-formed 12-byte buffer.
TEST_F(FloatCompareImpl, NonCanonicalNormalizes)
{
    Bytes const nonCanonicalTen{
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x86, 0xA0, 0xFF, 0xFF, 0xFF, 0xFC};
    expectValue(makeHost()->floatCompare(slice(nonCanonicalTen), slice(FloatTest::kTen)), 0);
}

}  // namespace xrpl::test

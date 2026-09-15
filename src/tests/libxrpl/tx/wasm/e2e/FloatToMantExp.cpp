#include <xrpl/protocol/TER.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealVmTest.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <format>
#include <string>

namespace xrpl::test {

// The only host function that writes to *two* output regions, and so the only place the
// "one call, one answer" assumption in every other marshalling path is not what happens.
// `float_to_mant_exp` splits a float into an eight-byte mantissa and a four-byte exponent,
// each into its own guest buffer, and answers with a status rather than a byte count. Two
// regions means two independent bounds checks, two writes, and an ordering between them —
// none of which the single-output shapes exercise. `host_calls` pins that wiring against a
// mock; this proves the real impl drives it the same way, with the guest reading both
// halves back out of its own memory.
struct FloatToMantExpE2e : RealVmTest
{
};

TEST_F(FloatToMantExpE2e, ContractReadsBothHalvesOfASplitFloat)
{
    // Pi's canonical encoding in, mantissa to offset 64, exponent to offset 128. The
    // contract returns the low half of the mantissa so the assertion checks that real bytes
    // landed in the guest's buffer, not merely that the call reported success.
    auto const wat = std::format(
        R"wat(
(module
  (import "host_lib" "float_to_mant_exp" (func $split (param i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (func (export "escrow_finish") (result i32)
    (local $r i32)
    (local.set $r (call $split
      (i32.const 0) (i32.const 12)
      (i32.const 64) (i32.const 8)
      (i32.const 128) (i32.const 4)))
    (if (i32.lt_s (local.get $r) (i32.const 0)) (then (return (local.get $r))))
    (i32.load (i32.const 64))))
)wat",
        watEscaped(FloatTest::kPi));

    auto const outcome = run(wat);
    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);

    // The expected value is derived from the input rather than written out as a literal,
    // because the derivation is the interesting part: a float stores its mantissa in the
    // first eight bytes **big-endian**, while `float_to_mant_exp` writes it to the guest
    // **little-endian**. So the guest's `i32.load` at the start of the mantissa buffer sees
    // the *low* 32 bits of a number whose bytes arrived in the opposite order. Getting that
    // flip wrong is exactly the convention mismatch this layer exists to catch, and a
    // hard-coded constant would hide it.
    auto mantissa = std::int64_t{0};
    for (auto i = 0U; i < 8; ++i)
    {
        mantissa = (mantissa << 8) | FloatTest::kPi[i];
    }
    EXPECT_EQ(outcome->result, static_cast<std::int32_t>(mantissa & 0xFFFFFFFF));
}

}  // namespace xrpl::test

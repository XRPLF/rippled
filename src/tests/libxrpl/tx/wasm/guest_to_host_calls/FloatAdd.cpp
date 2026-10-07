#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// float_add — two operand regions and a rounding mode in, a float region out.
//
// What only this layer can show is that `CxxHost`'s hand-written forward
// (`crates/xrpl-wasm-vm-ffi/src/lib.rs`) hands the C++ host the operands in the guest's
// order, which is why the two carry distinct bytes. `host_context/FloatAdd.cpp` enters below
// that forward and Rust's `tests/host_calls.rs` stops above it, so a swap there is invisible
// to both. `float_sub`, `float_mult`, `float_div` and `float_pow` are built the same way.
struct FloatAddGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kYAt = 16;
    static constexpr std::int32_t kOutAt = 32;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 7;
    static constexpr std::string_view kXText = "float-add-x0";
    static constexpr std::string_view kYText = "float-add-y0";

    static constexpr Arg kX = Arg::region(kXAt, kFloatLen);
    static constexpr Arg kY = Arg::region(kYAt, kFloatLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    Bytes const xBytes{kXText.begin(), kXText.end()};
    Bytes const yBytes{kYText.begin(), kYText.end()};

    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg xArg, Arg yArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "float_add",
            {xArg, yArg, outArg, modeArg},
            {{.at = kXAt, .bytes = xBytes}, {.at = kYAt, .bytes = yBytes}},
            answer);
    }
};

TEST_F(FloatAddGuest, operands_and_mode_reach_host_in_order)
{
    EXPECT_CALL(host, floatAdd(BytesAre(kXText), BytesAre(kYText), kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

}  // namespace xrpl::test

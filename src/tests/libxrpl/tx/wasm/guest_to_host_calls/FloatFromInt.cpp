#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// float_from_int — an `i64` and a rounding mode in, a float region out.
//
// What only this layer can show is that `CxxHost`'s hand-written forward
// (`crates/xrpl-wasm-vm-ffi/src/lib.rs`) reaches the C++ host with the guest's arguments.
// Declaration order is wasm parameter order, so `mode` is the *last* parameter, after the
// out region's pointer and length — an engine reading it as a length would still typecheck.
struct FloatFromIntGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 1;

    // Wider than 32 bits and not a palindrome, so a crossing that truncated or byte-swapped
    // it would reach the host as a different number. `float_from_int` and
    // `float_from_mant_exp` are the ABI's only genuine `i64` parameters.
    static constexpr std::int64_t kX = 0x0123'4567'89ab'cdefLL;

    static constexpr Arg kInt = Arg::scalar64(kX);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    // A float whose first four bytes are distinctive, so the guest's `i32.load` of them
    // cannot pass by accident.
    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] static std::string
    watFor(Arg intArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("float_from_int", {intArg, outArg, modeArg}, {}, answer);
    }
};

TEST_F(FloatFromIntGuest, int_and_mode_reach_host_in_order_and_float_comes_back)
{
    EXPECT_CALL(host, floatFromInt(kX, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kInt, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

}  // namespace xrpl::test

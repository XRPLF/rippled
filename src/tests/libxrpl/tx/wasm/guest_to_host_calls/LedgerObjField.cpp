#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// le_field — a cache slot and a field code in, bytes out.
//
// Both leading arguments are bare `i32`s on the wire, so the slot and the field code are
// values that cannot be confused for one another: a swap in `CxxHost`'s forward would
// otherwise be invisible.
struct LedgerObjFieldGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 64;
    static constexpr std::int32_t kOutLen = 8;
    static constexpr std::int32_t kValueLen = 6;
    static constexpr std::int32_t kSlot = 7;

    static constexpr Arg kCacheIdx = Arg::scalar(kSlot);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kOutLen);

    // Six bytes, the first four distinctive so the guest's `i32.load` of them cannot pass by
    // accident and the reported length cannot be mistaken for that load's width.
    Bytes const value{0x0d, 0x0c, 0x0b, 0x0a, 0xee, 0xff};

    // A real field code, so the shim's `SField` lookup has something to find.
    static Arg
    field()
    {
        return Arg::scalar(sfBalance.getCode());
    }

    [[nodiscard]] static std::string
    watFor(Arg cacheIdxArg, Arg fieldArg, Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("le_field", {cacheIdxArg, fieldArg, outArg}, {}, answer);
    }
};

TEST_F(LedgerObjFieldGuest, slot_and_field_code_reach_host_in_order)
{
    EXPECT_CALL(host, getLedgerObjField(kSlot, testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, field(), kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kValueLen) << "the length the host reported";
}

TEST_F(LedgerObjFieldGuest, field_bytes_reach_the_guests_out_region)
{
    EXPECT_CALL(host, getLedgerObjField(kSlot, testing::Ref(sfBalance))).WillOnce(Return(value));

    auto const wat = watFor(kCacheIdx, field(), kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the first four bytes, little-endian";
}

}  // namespace xrpl::test

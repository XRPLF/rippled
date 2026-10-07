#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// ldgr_index — no input, one scalar output.
struct LedgerSqnGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kSeqLen = 4;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kSeqLen);

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("ldgr_index", {outArg}, {}, answer);
    }
};

TEST_F(LedgerSqnGuest, sequence_reaches_guest_as_four_little_endian_bytes)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Read back with `i32.load`, which is little-endian by the wasm spec — so the value
    // arriving intact is the byte order being right.
    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), 0x01020304);
}

}  // namespace xrpl::test

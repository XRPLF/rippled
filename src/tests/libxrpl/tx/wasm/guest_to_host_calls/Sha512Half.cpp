#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// sha512_half — bytes in and bytes out, the shape that needs the engine's output buffer.
struct Sha512HalfGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kInputAt = 64;
    static constexpr std::int32_t kInputLen = 3;
    static constexpr std::int32_t kDigestLen = 32;

    static constexpr Arg kInput = Arg::region(kInputAt, kInputLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kDigestLen);

    Bytes const input{'a', 'b', 'c'};

    // A digest whose first four bytes are distinctive, so the load below cannot pass by
    // accident.
    static Hash
    digest()
    {
        Hash value;
        value.begin()[0] = 0x0d;
        value.begin()[1] = 0x0c;
        value.begin()[2] = 0x0b;
        value.begin()[3] = 0x0a;
        return value;
    }

    [[nodiscard]] std::string
    watFor(Arg inputArg, Arg outArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "sha512_half", {inputArg, outArg}, {{.at = kInputAt, .bytes = input}}, answer);
    }
};

// Both directions in one call: the guest's bytes reach the host borrowed from its memory, and
// the answer comes back into the same memory through the engine's buffer.
TEST_F(Sha512HalfGuest, guest_bytes_reach_host_and_digest_comes_back)
{
    EXPECT_CALL(host, computeSha512HalfHash(BytesAre("abc"))).WillOnce(Return(digest()));

    auto const wat = watFor(kInput, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the digest's first four bytes, little-endian";
}

TEST_F(Sha512HalfGuest, digest_is_thirty_two_bytes)
{
    EXPECT_CALL(host, computeSha512HalfHash).WillOnce(Return(digest()));

    auto const wat = watFor(kInput, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kDigestLen);
}

TEST_F(Sha512HalfGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, computeSha512HalfHash)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidParams)));

    auto const wat = watFor(kInput, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(Sha512HalfGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, computeSha512HalfHash)
        .WillOnce(testing::Throw(std::runtime_error{"digest came apart"}));

    auto const outcome = run(watFor(kInput, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("sha512Half"));
}

}  // namespace xrpl::test

#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// check_sig — three byte regions in, the verdict as the return value.
struct CheckSignatureGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kMessageAt = 0;
    static constexpr std::int32_t kSignatureAt = 32;
    static constexpr std::int32_t kPubkeyAt = 64;
    static constexpr std::int32_t kSliceLen = 3;

    static constexpr Arg kMessage = Arg::region(kMessageAt, kSliceLen);
    static constexpr Arg kSignature = Arg::region(kSignatureAt, kSliceLen);
    static constexpr Arg kPubkey = Arg::region(kPubkeyAt, kSliceLen);

    Bytes const messageBytes{'m', 's', 'g'};
    Bytes const signatureBytes{'s', 'i', 'g'};
    Bytes const pubkeyBytes{'k', 'e', 'y'};

    [[nodiscard]] std::string
    watFor(Arg messageArg, Arg signatureArg, Arg pubkeyArg) const
    {
        return hostCallWat(
            "check_sig",
            {messageArg, signatureArg, pubkeyArg},
            {{.at = kMessageAt, .bytes = messageBytes},
             {.at = kSignatureAt, .bytes = signatureBytes},
             {.at = kPubkeyAt, .bytes = pubkeyBytes}});
    }
};

TEST_F(CheckSignatureGuest, message_signature_and_pubkey_reach_host_in_order)
{
    EXPECT_CALL(host, checkSignature(BytesAre("msg"), BytesAre("sig"), BytesAre("key")))
        .WillOnce(Return(1));

    auto const wat = watFor(kMessage, kSignature, kPubkey);
    EXPECT_EQ(hostAnswer(wat), 1);
}

}  // namespace xrpl::test

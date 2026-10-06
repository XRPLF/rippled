#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// credential_id — two account regions and a credential type in, a keylet region out.
//
// The credential type has no length rule anywhere in the path, so the happy path names its
// bytes rather than its size.
struct CredentialKeyletGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kSubjectAt = 0;
    static constexpr std::int32_t kIssuerAt = 32;
    static constexpr std::int32_t kTypeAt = 64;
    static constexpr std::int32_t kOutAt = 96;
    static constexpr std::int32_t kAccountLen = static_cast<std::int32_t>(AccountID::size());
    static constexpr std::int32_t kTypeLen = 5;
    static constexpr std::int32_t kKeyletLen = 32;

    static constexpr Arg kSubject = Arg::region(kSubjectAt, kAccountLen);
    static constexpr Arg kIssuer = Arg::region(kIssuerAt, kAccountLen);
    static constexpr Arg kType = Arg::region(kTypeAt, kTypeLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kKeyletLen);

    Bytes const subjectBytes = Bytes(AccountID::size(), 0x14);
    Bytes const issuerBytes = Bytes(AccountID::size(), 0x54);
    Bytes const typeBytes{'t', 'e', 'r', 'm', 's'};
    AccountID const subject = AccountID::fromVoid(subjectBytes.data());
    AccountID const issuer = AccountID::fromVoid(issuerBytes.data());

    Bytes const keylet = [] {
        Bytes bytes(kKeyletLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(
        Arg subjectArg,
        Arg issuerArg,
        Arg typeArg,
        Arg outArg,
        Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "credential_id",
            {subjectArg, issuerArg, typeArg, outArg},
            {{.at = kSubjectAt, .bytes = subjectBytes},
             {.at = kIssuerAt, .bytes = issuerBytes},
             {.at = kTypeAt, .bytes = typeBytes}},
            answer);
    }
};

TEST_F(CredentialKeyletGuest, subject_issuer_and_type_reach_host_in_order_and_keylet_comes_back)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(Return(keylet));

    auto const wat = watFor(kSubject, kIssuer, kType, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

}  // namespace xrpl::test

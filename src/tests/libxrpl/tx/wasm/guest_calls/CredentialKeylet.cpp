#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// credential_id — two account regions and a credential type in, a keylet region out.
//
// The credential type has no length rule anywhere in the path, so the happy path names its
// bytes rather than its size.
struct CredentialKeyletGuest : HostCallTest
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

TEST_F(CredentialKeyletGuest, SubjectIssuerAndTypeReachHostInOrderAndKeyletComesBack)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(Return(keylet));

    auto const wat = watFor(kSubject, kIssuer, kType, kOut);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the keylet's first four bytes, little-endian";
}

TEST_F(CredentialKeyletGuest, StatusIsTheKeyletsLength)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(Return(keylet));

    auto const wat = watFor(kSubject, kIssuer, kType, kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kKeyletLen);
}

TEST_F(CredentialKeyletGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kSubject, kIssuer, kType, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

TEST_F(CredentialKeyletGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(testing::Throw(std::runtime_error{"credential keylet came apart"}));

    auto const outcome = callHost(watFor(kSubject, kIssuer, kType, kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("credentialKeylet"));
}

TEST_F(CredentialKeyletGuest, SubjectOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, credentialKeylet).Times(0);

    auto const wat = watFor(Arg::region(kSubjectAt, kAccountLen - 1), kIssuer, kType, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CredentialKeyletGuest, IssuerOfTheWrongLengthIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, credentialKeylet).Times(0);

    auto const wat = watFor(kSubject, Arg::region(kIssuerAt, kAccountLen + 1), kType, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CredentialKeyletGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, credentialKeylet).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kAccountLen), kIssuer, kType, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CredentialKeyletGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, credentialKeylet).Times(0);

    auto const wat = watFor(Arg::region(-1, kAccountLen), kIssuer, kType, kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CredentialKeyletGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(Return(keylet));

    auto const wat = watFor(kSubject, kIssuer, kType, Arg::outRegion(kOutAt, kKeyletLen - 1));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(CredentialKeyletGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(Return(keylet));

    auto const wat = watFor(kSubject, kIssuer, kType, Arg::outRegion(kOnePage, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(CredentialKeyletGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, BytesAre("terms")))
        .WillOnce(Return(keylet));

    auto const wat = watFor(kSubject, kIssuer, kType, Arg::outRegion(-1, kKeyletLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test

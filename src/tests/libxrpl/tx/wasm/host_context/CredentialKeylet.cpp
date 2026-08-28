#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The engine's own rules - buffer-fit, guest memory - are tested on the Rust side, not here.
//
// `subject` and `issuer` are distinct byte patterns: a happy path built from two copies of the
// same account would still pass if the two were swapped.
struct CredentialKeyletCall : HostContextTest
{
    Bytes const subjectBytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    Bytes const issuerBytes{0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
                            0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54};
    Bytes const credentialTypeBytes{0x74, 0x65, 0x72, 0x6d, 0x73};
    AccountID const subject = AccountID::fromVoid(subjectBytes.data());
    AccountID const issuer = AccountID::fromVoid(issuerBytes.data());
    Slice const credentialType{credentialTypeBytes.data(), credentialTypeBytes.size()};
};

// `credentialType` crosses unvalidated: whatever bytes the guest gives reach the host as-is.
TEST_F(CredentialKeyletCall, SubjectAndIssuerAreForwardedCredentialTypeUnvalidatedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, credentialKeylet(subject, issuer, credentialType))
        .WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes), bytesOf(issuerBytes), bytesOf(credentialTypeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

// The deliberate edge of "unvalidated": an empty `credentialType` is not a length the ABI
// rejects, so it reaches the host as an empty `Slice` and the call still succeeds.
TEST_F(CredentialKeyletCall, EmptyCredentialTypeIsForwardedUnvalidatedKeyletIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, credentialKeylet(subject, issuer, Slice{})).WillOnce(testing::Return(keylet));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes), bytesOf(issuerBytes), bytesOf(Bytes{}), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(CredentialKeyletCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, credentialType))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes), bytesOf(issuerBytes), bytesOf(credentialTypeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::LedgerObjNotFound));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(CredentialKeyletCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, credentialType))
        .WillOnce(testing::Throw(std::runtime_error{"credential keylet came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes), bytesOf(issuerBytes), bytesOf(credentialTypeBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("credential keylet came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("credentialKeylet"));
}

TEST_F(CredentialKeyletCall, MalformedSubjectIsRefusedWithoutAskingHost)
{
    Bytes const malformedSubject(AccountID::size() - 1, 0x01);
    EXPECT_CALL(host, credentialKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(malformedSubject),
            bytesOf(issuerBytes),
            bytesOf(credentialTypeBytes),
            out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(CredentialKeyletCall, MalformedIssuerIsRefusedWithoutAskingHost)
{
    Bytes const malformedIssuer(AccountID::size() + 1, 0x41);
    EXPECT_CALL(host, credentialKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes),
            bytesOf(malformedIssuer),
            bytesOf(credentialTypeBytes),
            out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Both ids fail one combined length check, so a call malformed in both places answers the
// same `InvalidParams` as either alone; what's observable is that the host is never asked.
TEST_F(CredentialKeyletCall, BothAccountsMalformedIsRefusedWithoutAskingHost)
{
    Bytes const malformedSubject(AccountID::size() - 1, 0x01);
    Bytes const malformedIssuer(AccountID::size() - 1, 0x41);
    EXPECT_CALL(host, credentialKeylet).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(malformedSubject),
            bytesOf(malformedIssuer),
            bytesOf(credentialTypeBytes),
            out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(CredentialKeyletCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, credentialKeylet(subject, issuer, credentialType))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size() - 1};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes), bytesOf(issuerBytes), bytesOf(credentialTypeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(CredentialKeyletCall, OutRegionOfExactSizeIsWritten)
{
    Bytes const keylet(32, 0xab);
    EXPECT_CALL(host, credentialKeylet(subject, issuer, credentialType))
        .WillOnce(testing::Return(keylet));

    OutRegion out{keylet.size()};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes), bytesOf(issuerBytes), bytesOf(credentialTypeBytes), out.slice()),
        static_cast<std::int32_t>(keylet.size()));
    EXPECT_TRUE(out.holds(bytesOf(keylet)));
}

TEST_F(CredentialKeyletCall, EmptyResultAnswersZeroAndWritesNothing)
{
    EXPECT_CALL(host, credentialKeylet(subject, issuer, credentialType))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.credentialKeylet(
            bytesOf(subjectBytes), bytesOf(issuerBytes), bytesOf(credentialTypeBytes), out.slice()),
        0);
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test

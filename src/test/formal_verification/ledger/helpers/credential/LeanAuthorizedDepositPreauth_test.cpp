#include <test/formal_verification/ffi/ledger/helpers/CredentialHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/credentials.h>
#include <test/jtx/deposit.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STVector256.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl::test {

using namespace formal_verification;

class LeanAuthorizedDepositPreauth_test : public LedgerSuite
{
    static constexpr std::string_view kCredType = "credType1";

    void
    runAuthorizedDepositPreauth(
        Sandbox& sb,
        std::vector<uint256> const& ids,
        AccountID const& dst,
        TER expected,
        char const* label)
    {
        runLedgerTest(sb, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = credentials::authorizedDepositPreauth(sb, STVector256(ids), dst);
            LeanTerResult const leanRes =
                formal_verification::authorizedDepositPreauth(ledger, ids, dst);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testAuthorizedDepositPreauth()
    {
        using namespace jtx;
        Account const subject("subject");
        Account const subject2("subject2");
        Account const issuer("issuer");
        Account const dst("dst");
        Account const other("other");
        Env env(*this);
        env.fund(XRP(1000), subject, subject2, issuer, dst, other);
        env(credentials::create(subject, issuer, kCredType));
        env(credentials::create(subject2, issuer, kCredType));
        env.close();
        env(deposit::authCredentials(dst, {{issuer, std::string(kCredType)}}));
        env.close();
        uint256 const credKey = credentials::keylet(subject, issuer, kCredType).key;
        uint256 const credKey2 = credentials::keylet(subject2, issuer, kCredType).key;

        Sandbox sb(&*env.current(), TapNone);
        runAuthorizedDepositPreauth(
            sb, {uint256{42}}, dst.id(), tefINTERNAL, "authorizedDepositPreauth.cred_absent");
        runAuthorizedDepositPreauth(
            sb,
            {credKey, credKey2},
            dst.id(),
            tefINTERNAL,
            "authorizedDepositPreauth.duplicate_pair");
        runAuthorizedDepositPreauth(
            sb, {credKey}, other.id(), tecNO_PERMISSION, "authorizedDepositPreauth.no_preauth");
        runAuthorizedDepositPreauth(
            sb, {credKey}, dst.id(), tesSUCCESS, "authorizedDepositPreauth.authorized");
    }

    void
    runTests() override
    {
        testAuthorizedDepositPreauth();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAuthorizedDepositPreauth, formal_verification, xrpl);

}  // namespace xrpl::test

#include <test/formal_verification/ffi/ledger/helpers/CredentialHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/credentials.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace xrpl::test {

using namespace formal_verification;

class LeanDeleteSLE_test : public LedgerSuite
{
    static constexpr std::string_view kCredType = "credType1";

    static uint256
    insertCredentialMissingIssuer(
        Sandbox& sb,
        AccountID const& subject,
        std::optional<uint32_t> expiration)
    {
        AccountID const ghost(jtx::Account("ghost").id());
        Slice const type(kCredType.data(), kCredType.size());
        auto sle = std::make_shared<SLE>(keylet::credential(subject, ghost, type));
        sle->setAccountID(sfSubject, subject);
        sle->setAccountID(sfIssuer, ghost);
        sle->setFieldVL(sfCredentialType, type);
        if (expiration)
            sle->setFieldU32(sfExpiration, *expiration);
        sb.insert(sle);
        return sle->key();
    }

    void
    runDeleteSLE(Sandbox& sb, uint256 const& credKey, TER expected, char const* label)
    {
        auto sle = sb.peek(keylet::credential(credKey));
        std::optional<CredentialFFI> credFFI;
        if (sle)
            credFFI =
                CredentialFFIBuilder().fromCpp(ledger_entries::Credential(sle)).build(sle->key());
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer =
                credentials::deleteSLE(sb, sle, beast::Journal{beast::Journal::getNullSink()});
            LeanTerResult const leanRes =
                formal_verification::deleteSLE(ledger, credFFI ? &*credFFI : nullptr);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testDeleteSLE()
    {
        using namespace jtx;
        Account const subject("subject");
        Account const issuer("issuer");
        Env env(*this);
        env.fund(XRP(1000), subject, issuer);
        env.close();

        {
            Sandbox sb(&*env.current(), TapNone);
            runDeleteSLE(sb, uint256{99}, tecNO_ENTRY, "deleteSLE.null");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            uint256 const key = insertCredentialMissingIssuer(sb, subject.id(), std::nullopt);
            runDeleteSLE(sb, key, tecINTERNAL, "deleteSLE.issuer_absent");
        }

        {
            Env env2(*this);
            env2.fund(XRP(1000), subject, issuer);
            env2(credentials::create(subject, issuer, kCredType));
            env2(credentials::accept(subject, issuer, kCredType));
            env2.close();
            Sandbox sb(&*env2.current(), TapNone);
            sb.erase(sb.peek(keylet::account(subject.id())));
            runDeleteSLE(
                sb,
                credentials::keylet(subject, issuer, kCredType).key,
                tecINTERNAL,
                "deleteSLE.subject_absent");
        }

        {
            env(credentials::create(subject, issuer, kCredType));
            env(credentials::accept(subject, issuer, kCredType));
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            runDeleteSLE(
                sb,
                credentials::keylet(subject, issuer, kCredType).key,
                tesSUCCESS,
                "deleteSLE.accepted");
        }

        {
            Env env3(*this);
            env3.fund(XRP(1000), subject, issuer);
            env3(credentials::create(subject, issuer, kCredType));
            env3.close();
            Sandbox sb(&*env3.current(), TapNone);
            runDeleteSLE(
                sb,
                credentials::keylet(subject, issuer, kCredType).key,
                tesSUCCESS,
                "deleteSLE.not_accepted");
        }

        {
            Env env4(*this);
            env4.fund(XRP(1000), issuer);
            env4(credentials::create(issuer, issuer, kCredType));
            env4.close();
            Sandbox sb(&*env4.current(), TapNone);
            runDeleteSLE(
                sb,
                credentials::keylet(issuer, issuer, kCredType).key,
                tesSUCCESS,
                "deleteSLE.self_issued");
        }
    }

    void
    runTests() override
    {
        testDeleteSLE();
    }
};

BEAST_DEFINE_TESTSUITE(LeanDeleteSLE, formal_verification, xrpl);

}  // namespace xrpl::test

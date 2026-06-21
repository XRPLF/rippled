#include <test/formal_verification/ffi/ledger/helpers/CredentialHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/credentials.h>
#include <test/jtx/deposit.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace xrpl::test {

using namespace formal_verification;

class LeanVerifyDepositPreauth_test : public LedgerSuite
{
    static constexpr std::string_view kCredType = "credType1";

    static STTx
    credTx(std::optional<std::vector<uint256>> const& ids)
    {
        return STTx(ttPAYMENT, [&](STObject& obj) {
            if (ids)
                obj.setFieldV256(sfCredentialIDs, STVector256(*ids));
        });
    }

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

    static void
    expireCredential(Sandbox& sb, uint256 const& credKey)
    {
        auto sle = sb.peek(keylet::credential(credKey));
        sle->setFieldU32(sfExpiration, 1);
        sb.update(sle);
    }

    void
    runVerifyDepositPreauth(
        Sandbox& sb,
        std::optional<std::vector<uint256>> const& ids,
        AccountID const& src,
        AccountID const& dst,
        bool passSleDst,
        TER expected,
        char const* label)
    {
        auto const sleDst = passSleDst ? sb.read(keylet::account(dst)) : nullptr;
        std::optional<AccountRootFFI> dstFFI;
        if (sleDst)
            dstFFI = AccountRootFFIBuilder()
                         .fromCpp(ledger_entries::AccountRoot(sleDst))
                         .build(sleDst->key());
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer = verifyDepositPreauth(
                credTx(ids), sb, src, dst, sleDst, beast::Journal{beast::Journal::getNullSink()});
            LeanTerResult const leanRes = formal_verification::verifyDepositPreauth(
                ledger, ids, src, dst, dstFFI ? &*dstFFI : nullptr);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testVerifyDepositPreauth()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");      // requires deposit auth, preauthorizes alice
        Account const carol("carol");  // not preauthorized
        Account const dave("dave");    // no deposit auth
        Account const issuer("issuer");
        Env env(*this);
        env.fund(XRP(1000), alice, bob, carol, dave, issuer);
        env(fset(bob, asfDepositAuth));
        env(deposit::auth(bob, alice));
        env(credentials::create(carol, issuer, kCredType));
        env(credentials::create(carol, issuer, "credType2"));
        env.close();
        env(deposit::authCredentials(bob, {{issuer, std::string(kCredType)}}));
        env.close();
        uint256 const carolCred = credentials::keylet(carol, issuer, kCredType).key;
        uint256 const carolCred2 = credentials::keylet(carol, issuer, "credType2").key;

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{},
                carol.id(),
                bob.id(),
                false,
                tesSUCCESS,
                "verifyDepositPreauth.empty");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{uint256{42}},
                carol.id(),
                bob.id(),
                false,
                tesSUCCESS,
                "verifyDepositPreauth.absent");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{carolCred},
                carol.id(),
                bob.id(),
                false,
                tesSUCCESS,
                "verifyDepositPreauth.not_expired");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            expireCredential(sb, carolCred);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{carolCred},
                carol.id(),
                bob.id(),
                false,
                tecEXPIRED,
                "verifyDepositPreauth.expired");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            expireCredential(sb, carolCred);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{carolCred, carolCred2},
                carol.id(),
                bob.id(),
                false,
                tecEXPIRED,
                "verifyDepositPreauth.mixed");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            uint256 const key = insertCredentialMissingIssuer(sb, carol.id(), 1);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{key},
                carol.id(),
                bob.id(),
                false,
                tecINTERNAL,
                "verifyDepositPreauth.delete_fails");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::nullopt,
                alice.id(),
                bob.id(),
                false,
                tesSUCCESS,
                "verifyDepositPreauth.no_sle_dst");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::nullopt,
                carol.id(),
                dave.id(),
                true,
                tesSUCCESS,
                "verifyDepositPreauth.no_deposit_auth");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::nullopt,
                bob.id(),
                bob.id(),
                true,
                tesSUCCESS,
                "verifyDepositPreauth.self");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::nullopt,
                alice.id(),
                bob.id(),
                true,
                tesSUCCESS,
                "verifyDepositPreauth.account_preauth");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::nullopt,
                carol.id(),
                bob.id(),
                true,
                tecNO_PERMISSION,
                "verifyDepositPreauth.no_permission");
        }

        {
            Sandbox sb(&*env.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{carolCred},
                carol.id(),
                bob.id(),
                true,
                tesSUCCESS,
                "verifyDepositPreauth.credential_preauth");
        }

        {
            Env env2(*this);
            env2.fund(XRP(1000), carol, dave, issuer);
            env2(fset(dave, asfDepositAuth));
            env2(credentials::create(carol, issuer, kCredType));
            env2.close();
            uint256 const cred = credentials::keylet(carol, issuer, kCredType).key;
            Sandbox sb(&*env2.current(), TapNone);
            runVerifyDepositPreauth(
                sb,
                std::vector<uint256>{cred},
                carol.id(),
                dave.id(),
                true,
                tecNO_PERMISSION,
                "verifyDepositPreauth.credential_not_authorized");
        }
    }

    void
    runTests() override
    {
        testVerifyDepositPreauth();
    }
};

BEAST_DEFINE_TESTSUITE(LeanVerifyDepositPreauth, formal_verification, xrpl);

}  // namespace xrpl::test

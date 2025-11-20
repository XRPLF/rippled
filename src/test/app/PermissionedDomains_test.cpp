#include <test/jtx.h>

#include <xrpld/app/tx/detail/PermissionedDomainSet.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

#include <exception>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ripple {
namespace test {

using namespace jtx;

static std::string
exceptionExpected(Env& env, Json::Value const& jv)
{
    try
    {
        env(jv, ter(temMALFORMED));
    }
    catch (std::exception const& ex)
    {
        return ex.what();
    }
    return {};
}

class PermissionedDomains_test : public beast::unit_test::suite
{
    FeatureBitset withoutFeature_{
        testable_amendments() - featurePermissionedDomains};
    FeatureBitset withFeature_{
        testable_amendments()  //
        | featurePermissionedDomains | featureCredentials};

    // Verify that each tx type can execute if the feature is enabled.
    void
    testEnabled()
    {
        testcase("Enabled");
        Account const alice("alice");
        Env env(*this, withFeature_);
        env.fund(XRP(1000), alice);
        pdomain::Credentials credentials{{alice, "first credential"}};
        env(pdomain::setTx(alice, credentials));
        BEAST_EXPECT(env.ownerCount(alice) == 1);
        auto objects = pdomain::getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
        // Test that account_objects is correct without passing it the type
        BEAST_EXPECT(objects == pdomain::getObjects(alice, env, false));
        auto const domain = objects.begin()->first;
        env(pdomain::deleteTx(alice, domain));
    }

    // Verify that PD cannot be created or updated if credentials are disabled
    void
    testCredentialsDisabled()
    {
        auto amendments = testable_amendments();
        amendments.set(featurePermissionedDomains);
        amendments.reset(featureCredentials);
        testcase("Credentials disabled");
        Account const alice("alice");
        Env env(*this, amendments);
        env.fund(XRP(1000), alice);
        pdomain::Credentials credentials{{alice, "first credential"}};
        env(pdomain::setTx(alice, credentials), ter(temDISABLED));
    }

    // Verify that each tx does not execute if feature is disabled
    void
    testDisabled()
    {
        testcase("Disabled");
        Account const alice("alice");
        Env env(*this, withoutFeature_);
        env.fund(XRP(1000), alice);
        pdomain::Credentials credentials{{alice, "first credential"}};
        env(pdomain::setTx(alice, credentials), ter(temDISABLED));
        env(pdomain::deleteTx(alice, uint256(75)), ter(temDISABLED));
    }

    // Verify that bad inputs fail for each of create new and update
    // behaviors of PermissionedDomainSet
    void
    testBadData(
        Account const& account,
        Env& env,
        std::optional<uint256> domain = std::nullopt)
    {
        Account const alice2("alice2");
        Account const alice3("alice3");
        Account const alice4("alice4");
        Account const alice5("alice5");
        Account const alice6("alice6");
        Account const alice7("alice7");
        Account const alice8("alice8");
        Account const alice9("alice9");
        Account const alice10("alice10");
        Account const alice11("alice11");
        Account const alice12("alice12");
        auto const setFee(drops(env.current()->fees().increment));

        // Test empty credentials.
        env(pdomain::setTx(account, pdomain::Credentials(), domain),
            ter(temARRAY_EMPTY));

        // Test 11 credentials.
        pdomain::Credentials const credentials11{
            {alice2, "credential1"},
            {alice3, "credential2"},
            {alice4, "credential3"},
            {alice5, "credential4"},
            {alice6, "credential5"},
            {alice7, "credential6"},
            {alice8, "credential7"},
            {alice9, "credential8"},
            {alice10, "credential9"},
            {alice11, "credential10"},
            {alice12, "credential11"}};
        BEAST_EXPECT(
            credentials11.size() ==
            maxPermissionedDomainCredentialsArraySize + 1);
        env(pdomain::setTx(account, credentials11, domain),
            ter(temARRAY_TOO_LARGE));

        // Test credentials including non-existent issuer.
        Account const nobody("nobody");
        pdomain::Credentials const credentialsNon{
            {alice2, "credential1"},
            {alice3, "credential2"},
            {alice4, "credential3"},
            {nobody, "credential4"},
            {alice5, "credential5"},
            {alice6, "credential6"},
            {alice7, "credential7"}};
        env(pdomain::setTx(account, credentialsNon, domain), ter(tecNO_ISSUER));

        // Test bad fee
        env(pdomain::setTx(account, credentials11, domain),
            fee(1, true),
            ter(temBAD_FEE));

        pdomain::Credentials const credentials4{
            {alice2, "credential1"},
            {alice3, "credential2"},
            {alice4, "credential3"},
            {alice5, "credential4"},
        };
        auto txJsonMutable = pdomain::setTx(account, credentials4, domain);
        auto const credentialOrig = txJsonMutable["AcceptedCredentials"][2u];

        // Remove Issuer from a credential and apply.
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential].removeMember(
            jss::Issuer);
        BEAST_EXPECT(
            exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Make an empty CredentialType.
        txJsonMutable["AcceptedCredentials"][2u] = credentialOrig;
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential]
                     ["CredentialType"] = "";
        env(txJsonMutable, ter(temMALFORMED));

        // Make too long CredentialType.
        constexpr std::string_view longCredentialType =
            "Cred0123456789012345678901234567890123456789012345678901234567890";
        static_assert(longCredentialType.size() == maxCredentialTypeLength + 1);
        txJsonMutable["AcceptedCredentials"][2u] = credentialOrig;
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential]
                     ["CredentialType"] = std::string(longCredentialType);
        BEAST_EXPECT(
            exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Remove Credentialtype from a credential and apply.
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential].removeMember(
            "CredentialType");
        BEAST_EXPECT(
            exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Remove both
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential].removeMember(
            jss::Issuer);
        BEAST_EXPECT(
            exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Make 2 identical credentials. Duplicates are not supported by
        // permissioned domains, so transactions should return errors
        {
            pdomain::Credentials const credentialsDup{
                {alice7, "credential6"},
                {alice2, "credential1"},
                {alice3, "credential2"},
                {alice2, "credential1"},
                {alice5, "credential4"},
            };

            std::unordered_map<std::string, Account> human2Acc;
            for (auto const& c : credentialsDup)
                human2Acc.emplace(c.issuer.human(), c.issuer);

            auto const sorted = pdomain::sortCredentials(credentialsDup);
            BEAST_EXPECT(sorted.size() == 4);
            env(pdomain::setTx(account, credentialsDup, domain),
                ter(temMALFORMED));

            env.close();
            env(pdomain::setTx(account, sorted, domain));

            uint256 d;
            if (domain)
                d = *domain;
            else
                d = pdomain::getNewDomain(env.meta());
            env.close();
            auto objects = pdomain::getObjects(account, env);
            auto const fromObject =
                pdomain::credentialsFromJson(objects[d], human2Acc);
            auto const sortedCreds = pdomain::sortCredentials(credentialsDup);
            BEAST_EXPECT(fromObject == sortedCreds);
        }

        // Have equal issuers but different credentials and make sure they
        // sort correctly.
        {
            pdomain::Credentials const credentialsSame{
                {alice2, "credential3"},
                {alice3, "credential2"},
                {alice2, "credential9"},
                {alice5, "credential4"},
                {alice2, "credential6"},
            };
            std::unordered_map<std::string, Account> human2Acc;
            for (auto const& c : credentialsSame)
                human2Acc.emplace(c.issuer.human(), c.issuer);

            BEAST_EXPECT(
                credentialsSame != pdomain::sortCredentials(credentialsSame));
            env(pdomain::setTx(account, credentialsSame, domain));

            uint256 d;
            if (domain)
                d = *domain;
            else
                d = pdomain::getNewDomain(env.meta());
            env.close();
            auto objects = pdomain::getObjects(account, env);
            auto const fromObject =
                pdomain::credentialsFromJson(objects[d], human2Acc);
            auto const sortedCreds = pdomain::sortCredentials(credentialsSame);
            BEAST_EXPECT(fromObject == sortedCreds);
        }
    }

    // Test PermissionedDomainSet
    void
    testSet()
    {
        testcase("Set");
        Env env(*this, withFeature_);
        env.set_parse_failure_expected(true);

        int const accNum = 12;
        Account const alice[accNum] = {
            "alice",
            "alice2",
            "alice3",
            "alice4",
            "alice5",
            "alice6",
            "alice7",
            "alice8",
            "alice9",
            "alice10",
            "alice11",
            "alice12"};
        std::unordered_map<std::string, Account> human2Acc;
        for (auto const& c : alice)
            human2Acc.emplace(c.human(), c);

        for (int i = 0; i < accNum; ++i)
            env.fund(XRP(1000), alice[i]);

        // Create new from existing account with a single credential.
        pdomain::Credentials const credentials1{{alice[2], "credential1"}};
        {
            env(pdomain::setTx(alice[0], credentials1));
            BEAST_EXPECT(env.ownerCount(alice[0]) == 1);
            auto tx = env.tx()->getJson(JsonOptions::none);
            BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainSet");
            BEAST_EXPECT(tx["Account"] == alice[0].human());
            auto objects = pdomain::getObjects(alice[0], env);
            auto domain = objects.begin()->first;
            BEAST_EXPECT(domain.isNonZero());
            auto object = objects.begin()->second;
            BEAST_EXPECT(object["LedgerEntryType"] == "PermissionedDomain");
            BEAST_EXPECT(object["Owner"] == alice[0].human());
            BEAST_EXPECT(object["Sequence"] == tx["Sequence"]);
            BEAST_EXPECT(
                pdomain::credentialsFromJson(object, human2Acc) ==
                credentials1);
        }

        // Make longest possible CredentialType.
        {
            constexpr std::string_view longCredentialType =
                "Cred0123456789012345678901234567890123456789012345678901234567"
                "89";
            static_assert(longCredentialType.size() == maxCredentialTypeLength);
            pdomain::Credentials const longCredentials{
                {alice[1], std::string(longCredentialType)}};

            env(pdomain::setTx(alice[0], longCredentials));

            // One account can create multiple domains
            BEAST_EXPECT(env.ownerCount(alice[0]) == 2);

            auto tx = env.tx()->getJson(JsonOptions::none);
            BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainSet");
            BEAST_EXPECT(tx["Account"] == alice[0].human());

            bool findSeq = false;
            for (auto const& [domain, object] :
                 pdomain::getObjects(alice[0], env))
            {
                findSeq = object["Sequence"] == tx["Sequence"];
                if (findSeq)
                {
                    BEAST_EXPECT(domain.isNonZero());
                    BEAST_EXPECT(
                        object["LedgerEntryType"] == "PermissionedDomain");
                    BEAST_EXPECT(object["Owner"] == alice[0].human());
                    BEAST_EXPECT(
                        pdomain::credentialsFromJson(object, human2Acc) ==
                        longCredentials);
                    break;
                }
            }
            BEAST_EXPECT(findSeq);
        }

        // Create new from existing account with 10 credentials.
        // Last credential describe domain owner itself
        pdomain::Credentials const credentials10{
            {alice[2], "credential1"},
            {alice[3], "credential2"},
            {alice[4], "credential3"},
            {alice[5], "credential4"},
            {alice[6], "credential5"},
            {alice[7], "credential6"},
            {alice[8], "credential7"},
            {alice[9], "credential8"},
            {alice[10], "credential9"},
            {alice[0], "credential10"},
        };
        uint256 domain2;
        {
            BEAST_EXPECT(
                credentials10.size() ==
                maxPermissionedDomainCredentialsArraySize);
            BEAST_EXPECT(
                credentials10 != pdomain::sortCredentials(credentials10));
            env(pdomain::setTx(alice[0], credentials10));
            auto tx = env.tx()->getJson(JsonOptions::none);
            domain2 = pdomain::getNewDomain(env.meta());
            auto objects = pdomain::getObjects(alice[0], env);
            auto object = objects[domain2];
            BEAST_EXPECT(
                pdomain::credentialsFromJson(object, human2Acc) ==
                pdomain::sortCredentials(credentials10));
        }

        // Update with 1 credential.
        env(pdomain::setTx(alice[0], credentials1, domain2));
        BEAST_EXPECT(
            pdomain::credentialsFromJson(
                pdomain::getObjects(alice[0], env)[domain2], human2Acc) ==
            credentials1);

        // Update with 10 credentials.
        env(pdomain::setTx(alice[0], credentials10, domain2));
        env.close();
        BEAST_EXPECT(
            pdomain::credentialsFromJson(
                pdomain::getObjects(alice[0], env)[domain2], human2Acc) ==
            pdomain::sortCredentials(credentials10));

        // Update from the wrong owner.
        env(pdomain::setTx(alice[2], credentials1, domain2),
            ter(tecNO_PERMISSION));

        // Update a uint256(0) domain
        env(pdomain::setTx(alice[0], credentials1, uint256(0)),
            ter(temMALFORMED));

        // Update non-existent domain
        env(pdomain::setTx(alice[0], credentials1, uint256(75)),
            ter(tecNO_ENTRY));

        // Wrong flag
        env(pdomain::setTx(alice[0], credentials1),
            txflags(tfClawTwoAssets),
            ter(temINVALID_FLAG));

        // Test bad data when creating a domain.
        testBadData(alice[0], env);
        // Test bad data when updating a domain.
        testBadData(alice[0], env, domain2);

        // Try to delete the account with domains.
        auto const acctDelFee(drops(env.current()->fees().increment));
        constexpr std::size_t deleteDelta = 255;
        {
            // Close enough ledgers to make it potentially deletable if empty.
            std::size_t ownerSeq = env.seq(alice[0]);
            while (deleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice[0], alice[2]),
                fee(acctDelFee),
                ter(tecHAS_OBLIGATIONS));
        }

        {
            // Delete the domains and then the owner account.
            for (auto const& objs : pdomain::getObjects(alice[0], env))
                env(pdomain::deleteTx(alice[0], objs.first));
            env.close();
            std::size_t ownerSeq = env.seq(alice[0]);
            while (deleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice[0], alice[2]), fee(acctDelFee));
        }
    }

    // Test PermissionedDomainDelete
    void
    testDelete()
    {
        testcase("Delete");
        Env env(*this, withFeature_);
        Account const alice("alice");

        env.fund(XRP(1000), alice);
        auto const setFee(drops(env.current()->fees().increment));

        pdomain::Credentials credentials{{alice, "first credential"}};
        env(pdomain::setTx(alice, credentials));
        env.close();

        auto objects = pdomain::getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
        auto const domain = objects.begin()->first;

        // Delete a domain that doesn't belong to the account.
        Account const bob("bob");
        env.fund(XRP(1000), bob);
        env(pdomain::deleteTx(bob, domain), ter(tecNO_PERMISSION));

        // Delete a non-existent domain.
        env(pdomain::deleteTx(alice, uint256(75)), ter(tecNO_ENTRY));

        // Test bad fee
        env(pdomain::deleteTx(alice, uint256(75)),
            ter(temBAD_FEE),
            fee(1, true));

        // Wrong flag
        env(pdomain::deleteTx(alice, domain),
            ter(temINVALID_FLAG),
            txflags(tfClawTwoAssets));

        // Delete a zero domain.
        env(pdomain::deleteTx(alice, uint256(0)), ter(temMALFORMED));

        // Make sure owner count reflects the existing domain.
        BEAST_EXPECT(env.ownerCount(alice) == 1);
        auto const objID = pdomain::getObjects(alice, env).begin()->first;
        BEAST_EXPECT(pdomain::objectExists(objID, env));

        // Delete domain that belongs to user.
        env(pdomain::deleteTx(alice, domain));
        auto const tx = env.tx()->getJson(JsonOptions::none);
        BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainDelete");

        // Make sure the owner count goes back to 0.
        BEAST_EXPECT(env.ownerCount(alice) == 0);

        // The object needs to be gone.
        BEAST_EXPECT(pdomain::getObjects(alice, env).empty());
        BEAST_EXPECT(!pdomain::objectExists(objID, env));
    }

    void
    testAccountReserve()
    {
        // Verify that the reserve behaves as expected for creating.
        testcase("Account Reserve");

        using namespace test::jtx;

        Env env(*this, withFeature_);
        Account const alice("alice");

        // Fund alice enough to exist, but not enough to meet
        // the reserve.
        auto const acctReserve = env.current()->fees().reserve;
        auto const incReserve = env.current()->fees().increment;
        env.fund(acctReserve, alice);
        env.close();
        BEAST_EXPECT(env.balance(alice) == acctReserve);
        BEAST_EXPECT(env.ownerCount(alice) == 0);

        // alice does not have enough XRP to cover the reserve.
        pdomain::Credentials credentials{{alice, "first credential"}};
        env(pdomain::setTx(alice, credentials), ter(tecINSUFFICIENT_RESERVE));
        BEAST_EXPECT(env.ownerCount(alice) == 0);
        BEAST_EXPECT(pdomain::getObjects(alice, env).size() == 0);
        env.close();

        auto const baseFee = env.current()->fees().base.drops();

        // Pay alice almost enough to make the reserve.
        env(pay(env.master, alice, incReserve + drops(2 * baseFee) - drops(1)));
        BEAST_EXPECT(
            env.balance(alice) ==
            acctReserve + incReserve + drops(baseFee) - drops(1));
        env.close();

        // alice still does not have enough XRP for the reserve.
        env(pdomain::setTx(alice, credentials), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(env.ownerCount(alice) == 0);

        // Pay alice enough to make the reserve.
        env(pay(env.master, alice, drops(baseFee) + drops(1)));
        env.close();

        // Now alice can create a PermissionedDomain.
        env(pdomain::setTx(alice, credentials));
        env.close();
        BEAST_EXPECT(env.ownerCount(alice) == 1);
    }

public:
    void
    run() override
    {
        testEnabled();
        testCredentialsDisabled();
        testDisabled();
        testSet();
        testDelete();
        testAccountReserve();
    }
};

BEAST_DEFINE_TESTSUITE(PermissionedDomains, app, ripple);

}  // namespace test
}  // namespace ripple

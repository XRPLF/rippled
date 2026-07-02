
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl::test {

using namespace jtx;

static std::string
exceptionExpected(Env& env, json::Value const& jv)
{
    try
    {
        env(jv, Ter(temMALFORMED));
    }
    catch (std::exception const& ex)
    {
        return ex.what();
    }
    return {};
}

class PermissionedDomains_test : public beast::unit_test::Suite
{
    FeatureBitset withFeature_{
        (testableAmendments() | featurePermissionedDomains | featureCredentials) - fixCleanup3_1_3};
    FeatureBitset withFix_{
        testableAmendments() | featurePermissionedDomains | featureCredentials | fixCleanup3_1_3};

    // Verify that each tx type can execute if the feature is enabled.
    void
    testEnabled(FeatureBitset features)
    {
        testcase("Enabled");
        Account const alice("alice");
        Env env(*this, features);
        env.fund(XRP(1000), alice);
        pdomain::Credentials const credentials{{.issuer = alice, .credType = "first credential"}};
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
        auto amendments = testableAmendments();
        amendments.set(featurePermissionedDomains);
        amendments.reset(featureCredentials);
        testcase("Credentials disabled");
        Account const alice("alice");
        Env env(*this, amendments);
        env.fund(XRP(1000), alice);
        pdomain::Credentials const credentials{{.issuer = alice, .credType = "first credential"}};
        env(pdomain::setTx(alice, credentials), Ter(temDISABLED));
    }

    // Verify that each tx does not execute if feature is disabled
    void
    testDisabled()
    {
        testcase("Disabled");
        Account const alice("alice");
        Env env(*this, testableAmendments() - featurePermissionedDomains);
        env.fund(XRP(1000), alice);
        pdomain::Credentials const credentials{{.issuer = alice, .credType = "first credential"}};
        env(pdomain::setTx(alice, credentials), Ter(temDISABLED));
        env(pdomain::deleteTx(alice, uint256(75)), Ter(temDISABLED));
    }

    // Verify that bad inputs fail for each of create new and update
    // behaviors of PermissionedDomainSet
    void
    testBadData(Account const& account, Env& env, std::optional<uint256> domain = std::nullopt)
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
        env(pdomain::setTx(account, pdomain::Credentials(), domain), Ter(temARRAY_EMPTY));

        // Test 11 credentials.
        pdomain::Credentials const credentials11{
            {.issuer = alice2, .credType = "credential1"},
            {.issuer = alice3, .credType = "credential2"},
            {.issuer = alice4, .credType = "credential3"},
            {.issuer = alice5, .credType = "credential4"},
            {.issuer = alice6, .credType = "credential5"},
            {.issuer = alice7, .credType = "credential6"},
            {.issuer = alice8, .credType = "credential7"},
            {.issuer = alice9, .credType = "credential8"},
            {.issuer = alice10, .credType = "credential9"},
            {.issuer = alice11, .credType = "credential10"},
            {.issuer = alice12, .credType = "credential11"}};
        BEAST_EXPECT(credentials11.size() == kMaxPermissionedDomainCredentialsArraySize + 1);
        env(pdomain::setTx(account, credentials11, domain), Ter(temARRAY_TOO_LARGE));

        // Test credentials including non-existent issuer.
        Account const nobody("nobody");
        pdomain::Credentials const credentialsNon{
            {.issuer = alice2, .credType = "credential1"},
            {.issuer = alice3, .credType = "credential2"},
            {.issuer = alice4, .credType = "credential3"},
            {.issuer = nobody, .credType = "credential4"},
            {.issuer = alice5, .credType = "credential5"},
            {.issuer = alice6, .credType = "credential6"},
            {.issuer = alice7, .credType = "credential7"}};
        env(pdomain::setTx(account, credentialsNon, domain), Ter(tecNO_ISSUER));

        // Test bad fee
        env(pdomain::setTx(account, credentials11, domain), Fee(1, true), Ter(temBAD_FEE));

        pdomain::Credentials const credentials4{
            {.issuer = alice2, .credType = "credential1"},
            {.issuer = alice3, .credType = "credential2"},
            {.issuer = alice4, .credType = "credential3"},
            {.issuer = alice5, .credType = "credential4"},
        };
        auto txJsonMutable = pdomain::setTx(account, credentials4, domain);
        auto const credentialOrig = txJsonMutable["AcceptedCredentials"][2u];

        // Remove Issuer from a credential and apply.
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential].removeMember(jss::Issuer);
        BEAST_EXPECT(exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Make an empty CredentialType.
        txJsonMutable["AcceptedCredentials"][2u] = credentialOrig;
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential]["CredentialType"] = "";
        env(txJsonMutable, Ter(temMALFORMED));

        // Make too long CredentialType.
        static constexpr std::string_view kLongCredentialType =
            "Cred0123456789012345678901234567890123456789012345678901234567890";
        static_assert(kLongCredentialType.size() == kMaxCredentialTypeLength + 1);
        txJsonMutable["AcceptedCredentials"][2u] = credentialOrig;
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential]["CredentialType"] =
            std::string(kLongCredentialType);
        BEAST_EXPECT(exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Remove Credentialtype from a credential and apply.
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential].removeMember("CredentialType");
        BEAST_EXPECT(exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Remove both
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential].removeMember(jss::Issuer);
        BEAST_EXPECT(exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        // Make 2 identical credentials. Duplicates are not supported by
        // permissioned domains, so transactions should return errors
        {
            pdomain::Credentials const credentialsDup{
                {.issuer = alice7, .credType = "credential6"},
                {.issuer = alice2, .credType = "credential1"},
                {.issuer = alice3, .credType = "credential2"},
                {.issuer = alice2, .credType = "credential1"},
                {.issuer = alice5, .credType = "credential4"},
            };

            std::unordered_map<std::string, Account> human2Acc;
            for (auto const& c : credentialsDup)
                human2Acc.emplace(c.issuer.human(), c.issuer);

            auto const sorted = pdomain::sortCredentials(credentialsDup);
            BEAST_EXPECT(sorted.size() == 4);
            env(pdomain::setTx(account, credentialsDup, domain), Ter(temMALFORMED));

            env.close();
            env(pdomain::setTx(account, sorted, domain));

            uint256 d;
            if (domain)
            {
                d = *domain;
            }
            else
            {
                d = pdomain::getNewDomain(env.meta());
            }
            env.close();
            auto objects = pdomain::getObjects(account, env);
            auto const fromObject = pdomain::credentialsFromJson(objects[d], human2Acc);
            auto const sortedCreds = pdomain::sortCredentials(credentialsDup);
            BEAST_EXPECT(fromObject == sortedCreds);
        }

        // Have equal issuers but different credentials and make sure they
        // sort correctly.
        {
            pdomain::Credentials const credentialsSame{
                {.issuer = alice2, .credType = "credential3"},
                {.issuer = alice3, .credType = "credential2"},
                {.issuer = alice2, .credType = "credential9"},
                {.issuer = alice5, .credType = "credential4"},
                {.issuer = alice2, .credType = "credential6"},
            };
            std::unordered_map<std::string, Account> human2Acc;
            for (auto const& c : credentialsSame)
                human2Acc.emplace(c.issuer.human(), c.issuer);

            BEAST_EXPECT(credentialsSame != pdomain::sortCredentials(credentialsSame));
            env(pdomain::setTx(account, credentialsSame, domain));

            uint256 d;
            if (domain)
            {
                d = *domain;
            }
            else
            {
                d = pdomain::getNewDomain(env.meta());
            }
            env.close();
            auto objects = pdomain::getObjects(account, env);
            auto const fromObject = pdomain::credentialsFromJson(objects[d], human2Acc);
            auto const sortedCreds = pdomain::sortCredentials(credentialsSame);
            BEAST_EXPECT(fromObject == sortedCreds);
        }
    }

    // Test PermissionedDomainSet
    void
    testSet(FeatureBitset features)
    {
        testcase("Set");
        Env env(*this, features);
        env.setParseFailureExpected(true);

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
        pdomain::Credentials const credentials1{{.issuer = alice[2], .credType = "credential1"}};
        {
            env(pdomain::setTx(alice[0], credentials1));
            BEAST_EXPECT(env.ownerCount(alice[0]) == 1);
            auto tx = env.tx()->getJson(JsonOptions::Values::None);
            BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainSet");
            BEAST_EXPECT(tx["Account"] == alice[0].human());
            auto objects = pdomain::getObjects(alice[0], env);
            auto domain = objects.begin()->first;
            BEAST_EXPECT(domain.isNonZero());
            auto object = objects.begin()->second;
            BEAST_EXPECT(object["LedgerEntryType"] == "PermissionedDomain");
            BEAST_EXPECT(object["Owner"] == alice[0].human());
            BEAST_EXPECT(object["Sequence"] == tx["Sequence"]);
            BEAST_EXPECT(pdomain::credentialsFromJson(object, human2Acc) == credentials1);
        }

        // Make longest possible CredentialType.
        {
            static constexpr std::string_view kLongCredentialType =
                "Cred0123456789012345678901234567890123456789012345678901234567"
                "89";
            static_assert(kLongCredentialType.size() == kMaxCredentialTypeLength);
            pdomain::Credentials const longCredentials{
                {.issuer = alice[1], .credType = std::string(kLongCredentialType)}};

            env(pdomain::setTx(alice[0], longCredentials));

            // One account can create multiple domains
            BEAST_EXPECT(env.ownerCount(alice[0]) == 2);

            auto tx = env.tx()->getJson(JsonOptions::Values::None);
            BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainSet");
            BEAST_EXPECT(tx["Account"] == alice[0].human());

            bool findSeq = false;
            for (auto const& [domain, object] : pdomain::getObjects(alice[0], env))
            {
                findSeq = object["Sequence"] == tx["Sequence"];
                if (findSeq)
                {
                    BEAST_EXPECT(domain.isNonZero());
                    BEAST_EXPECT(object["LedgerEntryType"] == "PermissionedDomain");
                    BEAST_EXPECT(object["Owner"] == alice[0].human());
                    BEAST_EXPECT(
                        pdomain::credentialsFromJson(object, human2Acc) == longCredentials);
                    break;
                }
            }
            BEAST_EXPECT(findSeq);
        }

        // Create new from existing account with 10 credentials.
        // Last credential describe domain owner itself
        pdomain::Credentials const credentials10{
            {.issuer = alice[2], .credType = "credential1"},
            {.issuer = alice[3], .credType = "credential2"},
            {.issuer = alice[4], .credType = "credential3"},
            {.issuer = alice[5], .credType = "credential4"},
            {.issuer = alice[6], .credType = "credential5"},
            {.issuer = alice[7], .credType = "credential6"},
            {.issuer = alice[8], .credType = "credential7"},
            {.issuer = alice[9], .credType = "credential8"},
            {.issuer = alice[10], .credType = "credential9"},
            {.issuer = alice[0], .credType = "credential10"},
        };
        uint256 domain2;
        {
            BEAST_EXPECT(credentials10.size() == kMaxPermissionedDomainCredentialsArraySize);
            BEAST_EXPECT(credentials10 != pdomain::sortCredentials(credentials10));
            env(pdomain::setTx(alice[0], credentials10));
            auto tx = env.tx()->getJson(JsonOptions::Values::None);
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
            pdomain::credentialsFromJson(pdomain::getObjects(alice[0], env)[domain2], human2Acc) ==
            credentials1);

        // Update with 10 credentials.
        env(pdomain::setTx(alice[0], credentials10, domain2));
        env.close();
        BEAST_EXPECT(
            pdomain::credentialsFromJson(pdomain::getObjects(alice[0], env)[domain2], human2Acc) ==
            pdomain::sortCredentials(credentials10));

        // Update from the wrong owner.
        env(pdomain::setTx(alice[2], credentials1, domain2), Ter(tecNO_PERMISSION));

        // Update a uint256(0) domain
        env(pdomain::setTx(alice[0], credentials1, uint256(0)), Ter(temMALFORMED));

        // Update non-existent domain
        env(pdomain::setTx(alice[0], credentials1, uint256(75)), Ter(tecNO_ENTRY));

        // Wrong flag
        env(pdomain::setTx(alice[0], credentials1), Txflags(tfClawTwoAssets), Ter(temINVALID_FLAG));

        // Test bad data when creating a domain.
        testBadData(alice[0], env);
        // Test bad data when updating a domain.
        testBadData(alice[0], env, domain2);

        // Try to delete the account with domains.
        auto const acctDelFee(drops(env.current()->fees().increment));
        static constexpr std::size_t kDeleteDelta = 255;
        {
            // Close enough ledgers to make it potentially deletable if empty.
            std::size_t const ownerSeq = env.seq(alice[0]);
            while (kDeleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice[0], alice[2]), Fee(acctDelFee), Ter(tecHAS_OBLIGATIONS));
        }

        {
            // Delete the domains and then the owner account.
            for (auto const& objs : pdomain::getObjects(alice[0], env))
                env(pdomain::deleteTx(alice[0], objs.first));
            env.close();
            std::size_t const ownerSeq = env.seq(alice[0]);
            while (kDeleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice[0], alice[2]), Fee(acctDelFee));
        }
    }

    // Test PermissionedDomainDelete
    void
    testDelete(FeatureBitset features)
    {
        testcase("Delete");
        Env env(*this, features);
        Account const alice("alice");

        env.fund(XRP(1000), alice);
        auto const setFee(drops(env.current()->fees().increment));

        pdomain::Credentials const credentials{{.issuer = alice, .credType = "first credential"}};
        env(pdomain::setTx(alice, credentials));
        env.close();

        auto objects = pdomain::getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
        auto const domain = objects.begin()->first;

        // Delete a domain that doesn't belong to the account.
        Account const bob("bob");
        env.fund(XRP(1000), bob);
        env(pdomain::deleteTx(bob, domain), Ter(tecNO_PERMISSION));

        // Delete a non-existent domain.
        env(pdomain::deleteTx(alice, uint256(75)), Ter(tecNO_ENTRY));

        // Test bad fee
        env(pdomain::deleteTx(alice, uint256(75)), Ter(temBAD_FEE), Fee(1, true));

        // Wrong flag
        env(pdomain::deleteTx(alice, domain), Ter(temINVALID_FLAG), Txflags(tfClawTwoAssets));

        // Delete a zero domain.
        env(pdomain::deleteTx(alice, uint256(0)), Ter(temMALFORMED));

        // Make sure owner count reflects the existing domain.
        BEAST_EXPECT(env.ownerCount(alice) == 1);
        auto const objID = pdomain::getObjects(alice, env).begin()->first;
        BEAST_EXPECT(pdomain::objectExists(objID, env));

        // Delete domain that belongs to user.
        env(pdomain::deleteTx(alice, domain));
        auto const tx = env.tx()->getJson(JsonOptions::Values::None);
        BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainDelete");

        // Make sure the owner count goes back to 0.
        BEAST_EXPECT(env.ownerCount(alice) == 0);

        // The object needs to be gone.
        BEAST_EXPECT(pdomain::getObjects(alice, env).empty());
        BEAST_EXPECT(!pdomain::objectExists(objID, env));
    }

    void
    testAccountReserve(FeatureBitset features)
    {
        // Verify that the reserve behaves as expected for creating.
        testcase("Account Reserve");

        using namespace test::jtx;

        Env env(*this, features);
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
        pdomain::Credentials const credentials{{.issuer = alice, .credType = "first credential"}};
        env(pdomain::setTx(alice, credentials), Ter(tecINSUFFICIENT_RESERVE));
        BEAST_EXPECT(env.ownerCount(alice) == 0);
        BEAST_EXPECT(pdomain::getObjects(alice, env).empty());
        env.close();

        auto const baseFee = env.current()->fees().base.drops();

        // Pay alice almost enough to make the reserve.
        env(pay(env.master, alice, incReserve + drops(2 * baseFee) - drops(1)));
        BEAST_EXPECT(env.balance(alice) == acctReserve + incReserve + drops(baseFee) - drops(1));
        env.close();

        // alice still does not have enough XRP for the reserve.
        env(pdomain::setTx(alice, credentials), Ter(tecINSUFFICIENT_RESERVE));
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

    void
    testTicket(FeatureBitset features)
    {
        testcase("Tickets");

        using namespace test::jtx;

        Env env(*this, features);
        Account const alice("alice");
        env.fund(XRP(1000), alice);

        pdomain::Credentials const credentials{
            {.issuer = alice, .credType = "credential1"},
        };

        std::uint32_t seq{env.seq(alice)};
        env(ticket::create(alice, 2));

        {
            env(pdomain::setTx(alice, credentials), ticket::Use(++seq));
            auto domain = pdomain::getNewDomain(env.meta());
            if (features[fixCleanup3_1_3])
            {
                BEAST_EXPECT(domain == keylet::permissionedDomain(alice.id(), seq).key);
            }
            else
            {
                BEAST_EXPECT(domain == keylet::permissionedDomain(alice.id(), 0).key);
            }
        }

        if (features[fixCleanup3_1_3])
        {
            env(pdomain::setTx(alice, credentials), ticket::Use(++seq));
        }
        else
        {
            env(pdomain::setTx(alice, credentials), ticket::Use(++seq), Ter(tefEXCEPTION));
        }
    }

public:
    void
    run() override
    {
        testEnabled(withFeature_);
        testEnabled(withFix_);
        testCredentialsDisabled();
        testDisabled();
        testSet(withFeature_);
        testSet(withFix_);
        testDelete(withFeature_);
        testDelete(withFix_);
        testAccountReserve(withFeature_);
        testAccountReserve(withFix_);
        testTicket(withFeature_);
        testTicket(withFix_);
    }
};

BEAST_DEFINE_TESTSUITE(PermissionedDomains, app, xrpl);

}  // namespace xrpl::test

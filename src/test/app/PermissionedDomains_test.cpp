//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <xrpld/app/tx/detail/PermissionedDomainSet.h>
#include <xrpld/ledger/ApplyViewImpl.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ripple {
namespace test {
namespace jtx {

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
    FeatureBitset withFeature_{
        supported_amendments() | featurePermissionedDomains};
    FeatureBitset withoutFeature_{supported_amendments()};

    // Verify that each tx type can execute if the feature is enabled.
    void
    testEnabled()
    {
        testcase("Enabled");
        Account const alice("alice");
        Env env(*this, withFeature_);
        env.fund(XRP(1000), alice);
        pd::Credentials credentials{{alice, "first credential"}};
        env(pd::setTx(alice, credentials));
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
        auto objects = pd::getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
        // Test that account_objects is correct without passing it the type
        BEAST_EXPECT(objects == pd::getObjects(alice, env, false));
        auto const domain = objects.begin()->first;
        env(pd::deleteTx(alice, domain));
    }

    // Verify that each tx does not execute if feature is disabled
    void
    testDisabled()
    {
        testcase("Disabled");
        Account const alice("alice");
        Env env(*this, withoutFeature_);
        env.fund(XRP(1000), alice);
        pd::Credentials credentials{{alice, "first credential"}};
        env(pd::setTx(alice, credentials), ter(temDISABLED));
        env(pd::deleteTx(alice, uint256(75)), ter(temDISABLED));
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
        env(pd::setTx(account, pd::Credentials(), domain), ter(temMALFORMED));

        // Test 11 credentials.
        pd::Credentials const credentials11{
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
            credentials11.size() == PermissionedDomainSet::PD_ARRAY_MAX + 1);
        env(pd::setTx(account, credentials11, domain), ter(temMALFORMED));

        // Test credentials including non-existent issuer.
        Account const nobody("nobody");
        pd::Credentials const credentialsNon{
            {alice2, "credential1"},
            {alice3, "credential2"},
            {alice4, "credential3"},
            {nobody, "credential4"},
            {alice5, "credential5"},
            {alice6, "credential6"},
            {alice7, "credential7"}};
        env(pd::setTx(account, credentialsNon, domain), ter(temBAD_ISSUER));

        pd::Credentials const credentials4{
            {alice2, "credential1"},
            {alice3, "credential2"},
            {alice4, "credential3"},
            {alice5, "credential4"},
        };
        auto txJsonMutable = pd::setTx(account, credentials4, domain);
        auto const credentialOrig = txJsonMutable["AcceptedCredentials"][2u];

        // Remove Issuer from a credential and apply.
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential].removeMember(
            jss::Issuer);
        BEAST_EXPECT(
            exceptionExpected(env, txJsonMutable).starts_with("invalidParams"));

        txJsonMutable["AcceptedCredentials"][2u] = credentialOrig;
        // Make an empty CredentialType.
        txJsonMutable["AcceptedCredentials"][2u][jss::Credential]
                     ["CredentialType"] = "";
        env(txJsonMutable, ter(temMALFORMED));

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

        // Make 2 identical credentials. The duplicate should be silently
        // removed.
        {
            pd::Credentials const credentialsDup{
                {alice7, "credential6"},
                {alice2, "credential1"},
                {alice3, "credential2"},
                {alice2, "credential1"},
                {alice5, "credential4"},
            };

            std::unordered_map<std::string, Account> pubKey2Acc;
            for (auto const& c : credentialsDup)
                pubKey2Acc.emplace(c.issuer.human(), c.issuer);

            BEAST_EXPECT(pd::sortCredentials(credentialsDup).size() == 4);
            env(pd::setTx(account, credentialsDup, domain));

            uint256 d;
            if (domain)
                d = *domain;
            else
                d = pd::getNewDomain(env.meta());
            env.close();
            auto objects = pd::getObjects(account, env);
            auto const fromObject =
                pd::credentialsFromJson(objects[d], pubKey2Acc);
            auto const sortedCreds = pd::sortCredentials(credentialsDup);
            BEAST_EXPECT(fromObject == sortedCreds);
        }

        // Have equal issuers but different credentials and make sure they
        // sort correctly.
        {
            pd::Credentials const credentialsSame{
                {alice2, "credential3"},
                {alice3, "credential2"},
                {alice2, "credential9"},
                {alice5, "credential4"},
                {alice2, "credential6"},
            };
            std::unordered_map<std::string, Account> pubKey2Acc;
            for (auto const& c : credentialsSame)
                pubKey2Acc.emplace(c.issuer.human(), c.issuer);

            BEAST_EXPECT(
                credentialsSame != pd::sortCredentials(credentialsSame));
            env(pd::setTx(account, credentialsSame, domain));

            uint256 d;
            if (domain)
                d = *domain;
            else
                d = pd::getNewDomain(env.meta());
            env.close();
            auto objects = pd::getObjects(account, env);
            auto const fromObject =
                pd::credentialsFromJson(objects[d], pubKey2Acc);
            auto const sortedCreds = pd::sortCredentials(credentialsSame);
            BEAST_EXPECT(fromObject == sortedCreds);
        }
    }

    // Test PermissionedDomainSet
    void
    testSet()
    {
        testcase("Set");
        Env env(*this, withFeature_);

        const int accNum = 12;
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
        std::unordered_map<std::string, Account> pubKey2Acc;
        for (auto const& c : alice)
            pubKey2Acc.emplace(c.human(), c);

        for (int i = 0; i < accNum; ++i)
            env.fund(XRP(1000), alice[i]);

        // Create new from existing account with a single credential.
        pd::Credentials const credentials1{{alice[2], "credential1"}};
        {
            env(pd::setTx(alice[0], credentials1));
            BEAST_EXPECT(
                pd::ownerInfo(alice[0], env)["OwnerCount"].asUInt() == 1);
            auto tx = env.tx()->getJson(JsonOptions::none);
            BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainSet");
            BEAST_EXPECT(tx["Account"] == alice[0].human());
            auto objects = pd::getObjects(alice[0], env);
            auto domain = objects.begin()->first;
            auto object = objects.begin()->second;
            BEAST_EXPECT(object["LedgerEntryType"] == "PermissionedDomain");
            BEAST_EXPECT(object["Owner"] == alice[0].human());
            BEAST_EXPECT(object["Sequence"] == tx["Sequence"]);
            BEAST_EXPECT(
                pd::credentialsFromJson(object, pubKey2Acc) == credentials1);
        }

        // Create new from existing account with 10 credentials.
        pd::Credentials const credentials10{
            {alice[2], "credential1"},
            {alice[3], "credential2"},
            {alice[4], "credential3"},
            {alice[5], "credential4"},
            {alice[6], "credential5"},
            {alice[7], "credential6"},
            {alice[8], "credential7"},
            {alice[9], "credential8"},
            {alice[10], "credential9"},
            {alice[11], "credential10"},
        };
        uint256 domain2;
        {
            BEAST_EXPECT(
                credentials10.size() == PermissionedDomainSet::PD_ARRAY_MAX);
            BEAST_EXPECT(credentials10 != pd::sortCredentials(credentials10));
            env(pd::setTx(alice[0], credentials10));
            auto tx = env.tx()->getJson(JsonOptions::none);
            domain2 = pd::getNewDomain(env.meta());
            auto objects = pd::getObjects(alice[0], env);
            auto object = objects[domain2];
            BEAST_EXPECT(
                pd::credentialsFromJson(object, pubKey2Acc) ==
                pd::sortCredentials(credentials10));
        }

        // Update with 1 credential.
        env(pd::setTx(alice[0], credentials1, domain2));
        BEAST_EXPECT(
            pd::credentialsFromJson(
                pd::getObjects(alice[0], env)[domain2], pubKey2Acc) ==
            credentials1);

        // Update with 10 credentials.
        env(pd::setTx(alice[0], credentials10, domain2));
        env.close();
        BEAST_EXPECT(
            pd::credentialsFromJson(
                pd::getObjects(alice[0], env)[domain2], pubKey2Acc) ==
            pd::sortCredentials(credentials10));

        // Update from the wrong owner.
        env(pd::setTx(alice[2], credentials1, domain2),
            ter(temINVALID_ACCOUNT_ID));

        // Update a uint256(0) domain
        env(pd::setTx(alice[0], credentials1, uint256(0)), ter(temMALFORMED));

        // Update non-existent domain
        env(pd::setTx(alice[0], credentials1, uint256(75)), ter(tecNO_ENTRY));

        // Test bad data when creating a domain.
        testBadData(alice[0], env);
        // Test bad data when updating a domain.
        testBadData(alice[0], env, domain2);

        // Try to delete the account with domains.
        auto const acctDelFee(drops(env.current()->fees().increment));
        constexpr std::size_t deleteDelta = 255;
        {
            // Close enough ledgers to make it potentially deletable if empty.
            std::size_t ownerSeq =
                pd::ownerInfo(alice[0], env)["Sequence"].asUInt();
            while (deleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice[0], alice[2]),
                fee(acctDelFee),
                ter(tecHAS_OBLIGATIONS));
        }

        {
            // Delete the domains and then the owner account.
            for (auto const& objs : pd::getObjects(alice[0], env))
                env(pd::deleteTx(alice[0], objs.first));
            env.close();
            std::size_t ownerSeq =
                pd::ownerInfo(alice[0], env)["Sequence"].asUInt();
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
        pd::Credentials credentials{{alice, "first credential"}};
        env(pd::setTx(alice, credentials));
        env.close();
        auto objects = pd::getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
        auto const domain = objects.begin()->first;

        // Delete a domain that doesn't belong to the account.
        Account const bob("bob");
        env.fund(XRP(1000), bob);
        env(pd::deleteTx(bob, domain), ter(temINVALID_ACCOUNT_ID));

        // Delete a non-existent domain.
        env(pd::deleteTx(alice, uint256(75)), ter(tecNO_ENTRY));

        // Delete a zero domain.
        env(pd::deleteTx(alice, uint256(0)), ter(temMALFORMED));

        // Make sure owner count reflects the existing domain.
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
        auto const objID = pd::getObjects(alice, env).begin()->first;
        BEAST_EXPECT(pd::objectExists(objID, env));
        // Delete domain that belongs to user.
        env(pd::deleteTx(alice, domain), ter(tesSUCCESS));
        auto const tx = env.tx()->getJson(JsonOptions::none);
        BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainDelete");
        // Make sure the owner count goes back to 0.
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 0);
        // The object needs to be gone.
        BEAST_EXPECT(pd::getObjects(alice, env).empty());
        BEAST_EXPECT(!pd::objectExists(objID, env));
    }

    void
    testAccountReserve()
    {
        // Verify that the reserve behaves as expected for minting.
        testcase("Account Reserve");

        using namespace test::jtx;

        Env env(*this, withFeature_);
        Account const alice("alice");

        // Fund alice enough to exist, but not enough to meet
        // the reserve.
        auto const acctReserve = env.current()->fees().accountReserve(0);
        auto const incReserve = env.current()->fees().increment;
        env.fund(acctReserve, alice);
        env.close();
        BEAST_EXPECT(env.balance(alice) == acctReserve);
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 0);

        // alice does not have enough XRP to cover the reserve.
        pd::Credentials credentials{{alice, "first credential"}};
        env(pd::setTx(alice, credentials), ter(tecINSUFFICIENT_RESERVE));
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 0);
        BEAST_EXPECT(pd::getObjects(alice, env).size() == 0);
        env.close();

        auto const baseFee = env.current()->fees().base.drops();

        // Pay alice almost enough to make the reserve.
        env(pay(env.master, alice, incReserve + drops(2 * baseFee) - drops(1)));
        BEAST_EXPECT(
            env.balance(alice) ==
            acctReserve + incReserve + drops(baseFee) - drops(1));
        env.close();

        // alice still does not have enough XRP for the reserve.
        env(pd::setTx(alice, credentials), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 0);

        // Pay alice enough to make the reserve.
        env(pay(env.master, alice, drops(baseFee) + drops(1)));
        env.close();

        // Now alice can create a PermissionedDomain.
        env(pd::setTx(alice, credentials));
        env.close();
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
    }

public:
    void
    run() override
    {
        testEnabled();
        testDisabled();
        testSet();
        testDelete();
        testAccountReserve();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(PermissionedDomains, app, ripple, 2);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

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
#include <test/jtx/PermissionedDomains.h>
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
        auto const setFee(drops(env.current()->fees().increment));
        pd::Credentials credentials{{alice, pd::toBlob("first credential")}};
        env(pd::setTx(alice, credentials), fee(setFee));
        BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
        auto objects = pd::getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
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
        auto const setFee(drops(env.current()->fees().increment));
        pd::Credentials credentials{{alice, pd::toBlob("first credential")}};
        env(pd::setTx(alice, credentials), fee(setFee), ter(temDISABLED));
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
        env(pd::setTx(account, pd::Credentials(), domain),
            fee(setFee),
            ter(temMALFORMED));

        // Test 11 credentials.
        pd::Credentials const credentials11{
            {alice2, pd::toBlob("credential1")},
            {alice3, pd::toBlob("credential2")},
            {alice4, pd::toBlob("credential3")},
            {alice5, pd::toBlob("credential4")},
            {alice6, pd::toBlob("credential5")},
            {alice7, pd::toBlob("credential6")},
            {alice8, pd::toBlob("credential7")},
            {alice9, pd::toBlob("credential8")},
            {alice10, pd::toBlob("credential9")},
            {alice11, pd::toBlob("credential10")},
            {alice12, pd::toBlob("credential11")}};
        BEAST_EXPECT(
            credentials11.size() == PermissionedDomainSet::PD_ARRAY_MAX + 1);
        env(pd::setTx(account, credentials11, domain),
            fee(setFee),
            ter(temMALFORMED));

        // Test credentials including non-existent issuer.
        Account const nobody("nobody");
        pd::Credentials const credentialsNon{
            {alice2, pd::toBlob("credential1")},
            {alice3, pd::toBlob("credential2")},
            {alice4, pd::toBlob("credential3")},
            {nobody, pd::toBlob("credential4")},
            {alice5, pd::toBlob("credential5")},
            {alice6, pd::toBlob("credential6")},
            {alice7, pd::toBlob("credential7")}};
        env(pd::setTx(account, credentialsNon, domain),
            fee(setFee),
            ter(temBAD_ISSUER));

        pd::Credentials const credentials4{
            {alice2, pd::toBlob("credential1")},
            {alice3, pd::toBlob("credential2")},
            {alice4, pd::toBlob("credential3")},
            {alice5, pd::toBlob("credential4")},
        };
        auto txJsonMutable = pd::setTx(account, credentials4, domain);
        auto const credentialOrig = txJsonMutable["AcceptedCredentials"][2u];

        // Remove Issuer from a credential and apply.
        txJsonMutable["AcceptedCredentials"][2u]["AcceptedCredential"]
            .removeMember("Issuer");
        env(txJsonMutable, fee(setFee), ter(temMALFORMED));

        txJsonMutable["AcceptedCredentials"][2u] = credentialOrig;
        // Make an empty CredentialType.
        txJsonMutable["AcceptedCredentials"][2u]["AcceptedCredential"]
                     ["CredentialType"] = "";
        env(txJsonMutable, fee(setFee), ter(temMALFORMED));

        // Remove Credentialtype from a credential and apply.
        txJsonMutable["AcceptedCredentials"][2u]["AcceptedCredential"]
            .removeMember("CredentialType");
        env(txJsonMutable, fee(setFee), ter(temMALFORMED));

        // Remove both
        txJsonMutable["AcceptedCredentials"][2u]["AcceptedCredential"]
            .removeMember("Issuer");
        env(txJsonMutable, fee(setFee), ter(temMALFORMED));

        // Make 2 identical credentials. The duplicate should be silently
        // removed.
        {
            pd::Credentials const credentialsDup{
                {alice7, pd::toBlob("credential6")},
                {alice2, pd::toBlob("credential1")},
                {alice3, pd::toBlob("credential2")},
                {alice2, pd::toBlob("credential1")},
                {alice5, pd::toBlob("credential4")},
            };
            BEAST_EXPECT(pd::sortCredentials(credentialsDup).size() == 4);
            env(pd::setTx(account, credentialsDup, domain), fee(setFee));

            uint256 d;
            if (domain)
                d = *domain;
            else
                d = pd::getNewDomain(env.meta());
            env.close();
            auto objects = pd::getObjects(account, env);
            auto const fromObject = pd::credentialsFromJson(objects[d]);
            auto const sortedCreds = pd::sortCredentials(credentialsDup);
            BEAST_EXPECT(
                pd::credentialsFromJson(objects[d]) ==
                pd::sortCredentials(credentialsDup));
        }

        // Have equal issuers but different credentials and make sure they
        // sort correctly.
        {
            pd::Credentials const credentialsSame{
                {alice2, pd::toBlob("credential3")},
                {alice3, pd::toBlob("credential2")},
                {alice2, pd::toBlob("credential9")},
                {alice5, pd::toBlob("credential4")},
                {alice2, pd::toBlob("credential6")},
            };
            BEAST_EXPECT(
                credentialsSame != pd::sortCredentials(credentialsSame));
            env(pd::setTx(account, credentialsSame, domain), fee(setFee));

            uint256 d;
            if (domain)
                d = *domain;
            else
                d = pd::getNewDomain(env.meta());
            env.close();
            auto objects = pd::getObjects(account, env);
            auto const fromObject = pd::credentialsFromJson(objects[d]);
            auto const sortedCreds = pd::sortCredentials(credentialsSame);
            BEAST_EXPECT(
                pd::credentialsFromJson(objects[d]) ==
                pd::sortCredentials(credentialsSame));
        }
    }

    // Test PermissionedDomainSet
    void
    testSet()
    {
        testcase("Set");
        Env env(*this, withFeature_);
        Account const alice("alice"), alice2("alice2"), alice3("alice3"),
            alice4("alice4"), alice5("alice5"), alice6("alice6"),
            alice7("alice7"), alice8("alice8"), alice9("alice9"),
            alice10("alice10"), alice11("alice11"), alice12("alice12");
        env.fund(
            XRP(1000),
            alice,
            alice2,
            alice3,
            alice4,
            alice5,
            alice6,
            alice7,
            alice8,
            alice9,
            alice10,
            alice11,
            alice12);
        auto const dropsFee = env.current()->fees().increment;
        auto const setFee(drops(dropsFee));

        // Create new from existing account with a single credential.
        pd::Credentials const credentials1{{alice2, pd::toBlob("credential1")}};
        {
            env(pd::setTx(alice, credentials1), fee(setFee));
            BEAST_EXPECT(pd::ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
            auto tx = env.tx()->getJson(JsonOptions::none);
            BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainSet");
            BEAST_EXPECT(tx["Account"] == alice.human());
            auto objects = pd::getObjects(alice, env);
            auto domain = objects.begin()->first;
            auto object = objects.begin()->second;
            BEAST_EXPECT(object["LedgerEntryType"] == "PermissionedDomain");
            BEAST_EXPECT(object["Owner"] == alice.human());
            BEAST_EXPECT(object["Sequence"] == tx["Sequence"]);
            BEAST_EXPECT(pd::credentialsFromJson(object) == credentials1);
        }

        // Create new from existing account with 10 credentials.
        pd::Credentials const credentials10{
            {alice2, pd::toBlob("credential1")},
            {alice3, pd::toBlob("credential2")},
            {alice4, pd::toBlob("credential3")},
            {alice5, pd::toBlob("credential4")},
            {alice6, pd::toBlob("credential5")},
            {alice7, pd::toBlob("credential6")},
            {alice8, pd::toBlob("credential7")},
            {alice9, pd::toBlob("credential8")},
            {alice10, pd::toBlob("credential9")},
            {alice11, pd::toBlob("credential10")},
        };
        uint256 domain2;
        {
            BEAST_EXPECT(
                credentials10.size() == PermissionedDomainSet::PD_ARRAY_MAX);
            BEAST_EXPECT(credentials10 != pd::sortCredentials(credentials10));
            env(pd::setTx(alice, credentials10), fee(setFee));
            auto tx = env.tx()->getJson(JsonOptions::none);
            domain2 = pd::getNewDomain(env.meta());
            auto objects = pd::getObjects(alice, env);
            auto object = objects[domain2];
            BEAST_EXPECT(
                pd::credentialsFromJson(object) ==
                pd::sortCredentials(credentials10));
        }

        // Make a new domain with insufficient fee.
        env(pd::setTx(alice, credentials10),
            fee(drops(dropsFee - 1)),
            ter(telINSUF_FEE_P));

        // Update with 1 credential.
        env(pd::setTx(alice, credentials1, domain2));
        BEAST_EXPECT(
            pd::credentialsFromJson(pd::getObjects(alice, env)[domain2]) ==
            credentials1);

        // Update with 10 credentials.
        env(pd::setTx(alice, credentials10, domain2));
        env.close();
        BEAST_EXPECT(
            pd::credentialsFromJson(pd::getObjects(alice, env)[domain2]) ==
            pd::sortCredentials(credentials10));

        // Update from the wrong owner.
        env(pd::setTx(alice2, credentials1, domain2),
            ter(temINVALID_ACCOUNT_ID));

        // Update a uint256(0) domain
        env(pd::setTx(alice, credentials1, uint256(0)), ter(temMALFORMED));

        // Update non-existent domain
        env(pd::setTx(alice, credentials1, uint256(75)), ter(tecNO_ENTRY));

        // Test bad data when creating a domain.
        testBadData(alice, env);
        // Test bad data when updating a domain.
        testBadData(alice, env, domain2);

        // Try to delete the account with domains.
        auto const acctDelFee(drops(env.current()->fees().increment));
        constexpr std::size_t deleteDelta = 255;
        {
            // Close enough ledgers to make it potentially deletable if empty.
            std::size_t ownerSeq =
                pd::ownerInfo(alice, env)["Sequence"].asUInt();
            while (deleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice, alice2),
                fee(acctDelFee),
                ter(tecHAS_OBLIGATIONS));
        }

        {
            // Delete the domains and then the owner account.
            for (auto const& objs : pd::getObjects(alice, env))
                env(pd::deleteTx(alice, objs.first));
            env.close();
            std::size_t ownerSeq =
                pd::ownerInfo(alice, env)["Sequence"].asUInt();
            while (deleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice, alice2), fee(acctDelFee));
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
        pd::Credentials credentials{{alice, pd::toBlob("first credential")}};
        env(pd::setTx(alice, credentials), fee(setFee));
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

public:
    void
    run() override
    {
        testEnabled();
        testDisabled();
        testSet();
        testDelete();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(PermissionedDomains, app, ripple, 2);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

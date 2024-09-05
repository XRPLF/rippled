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

    using Credential = std::pair<AccountID, Blob>;
    using Credentials = std::vector<Credential>;

    // helpers
    // Make json for PermissionedDomainSet transaction
    static Json::Value
    setTx(
        AccountID const& account,
        Credentials const& credentials,
        std::optional<uint256> domain = std::nullopt)
    {
        Json::Value jv;
        jv[sfTransactionType.jsonName] = jss::PermissionedDomainSet;
        jv[sfAccount.jsonName] = to_string(account);
        if (domain)
            jv[sfDomainID.jsonName] = to_string(*domain);
        Json::Value a(Json::arrayValue);
        for (auto const& credential : credentials)
        {
            Json::Value obj(Json::objectValue);
            obj[sfIssuer.jsonName] = to_string(credential.first);
            obj[sfCredentialType.jsonName] = strHex(
                Slice{credential.second.data(), credential.second.size()});
            Json::Value o2(Json::objectValue);
            o2[sfAcceptedCredential.jsonName] = obj;
            a.append(o2);
        }
        jv[sfAcceptedCredentials.jsonName] = a;
        return jv;
    }

    // Make json for PermissionedDomainDelete transaction
    static Json::Value
    deleteTx(AccountID const& account, uint256 const& domain)
    {
        Json::Value jv{Json::objectValue};
        jv[sfTransactionType.jsonName] = jss::PermissionedDomainDelete;
        jv[sfAccount.jsonName] = to_string(account);
        jv[sfDomainID.jsonName] = to_string(domain);
        return jv;
    }

    // Get PermissionedDomain objects from account_objects rpc call
    static std::map<uint256, Json::Value>
    getObjects(Account const& account, Env& env)
    {
        std::map<uint256, Json::Value> ret;
        Json::Value params;
        params[jss::account] = account.human();
        auto const& resp =
            env.rpc("json", "account_objects", to_string(params));
        Json::Value a(Json::arrayValue);
        a = resp[jss::result][jss::account_objects];
        for (auto const& object : a)
        {
            if (object["LedgerEntryType"] != "PermissionedDomain")
                continue;
            uint256 index;
            std::ignore = index.parseHex(object[jss::index].asString());
            ret[index] = object;
        }
        return ret;
    }

    // Convert string to Blob
    static Blob
    toBlob(std::string const& input)
    {
        Blob ret;
        for (auto const& c : input)
            ret.push_back(c);
        return ret;
    }

    // Extract credentials from account_object object
    static Credentials
    credentialsFromJson(Json::Value const& object)
    {
        Credentials ret;
        Json::Value a(Json::arrayValue);
        a = object["AcceptedCredentials"];
        for (auto const& credential : a)
        {
            Json::Value obj(Json::objectValue);
            obj = credential["AcceptedCredential"];
            auto const issuer = obj["Issuer"];
            auto const credentialType = obj["CredentialType"];
            auto aid = parseBase58<AccountID>(issuer.asString());
            auto ct = strUnHex(credentialType.asString());
            ret.emplace_back(
                *parseBase58<AccountID>(issuer.asString()),
                strUnHex(credentialType.asString()).value());
        }
        return ret;
    }

    // Sort credentials the same way as PermissionedDomainSet
    static Credentials
    sortCredentials(Credentials const& input)
    {
        Credentials ret = input;
        std::sort(
            ret.begin(),
            ret.end(),
            [](Credential const& left, Credential const& right) -> bool {
                return left.first < right.first;
            });
        return ret;
    }

    // Get account_info
    static Json::Value
    ownerInfo(Account const& account, Env& env)
    {
        Json::Value params;
        params[jss::account] = account.human();
        auto const& resp = env.rpc("json", "account_info", to_string(params));
        return env.rpc(
            "json",
            "account_info",
            to_string(params))["result"]["account_data"];
    }

    // tests
    // Verify that each tx type can execute if the feature is enabled.
    void
    testEnabled()
    {
        testcase("Enabled");
        Account const alice("alice");
        Env env(*this, withFeature_);
        env.fund(XRP(1000), alice);
        auto const setFee(drops(env.current()->fees().increment));
        Credentials credentials{{alice, toBlob("first credential")}};
        env(setTx(alice, credentials), fee(setFee));
        BEAST_EXPECT(ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
        auto objects = getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
        auto const domain = objects.begin()->first;
        env(deleteTx(alice, domain));
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
        Credentials credentials{{alice, toBlob("first credential")}};
        env(setTx(alice, credentials), fee(setFee), ter(temDISABLED));
        env(deleteTx(alice, uint256(75)), ter(temDISABLED));
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
        env(setTx(account, Credentials(), domain),
            fee(setFee),
            ter(temMALFORMED));

        // Test 11 credentials.
        Credentials const credentials11{
            {alice2, toBlob("credential1")},
            {alice3, toBlob("credential2")},
            {alice4, toBlob("credential3")},
            {alice5, toBlob("credential4")},
            {alice6, toBlob("credential5")},
            {alice7, toBlob("credential6")},
            {alice8, toBlob("credential7")},
            {alice9, toBlob("credential8")},
            {alice10, toBlob("credential9")},
            {alice11, toBlob("credential10")},
            {alice12, toBlob("credential11")}};
        BEAST_EXPECT(
            credentials11.size() == PermissionedDomainSet::PD_ARRAY_MAX + 1);
        env(setTx(account, credentials11, domain),
            fee(setFee),
            ter(temMALFORMED));

        // Test credentials including non-existent issuer.
        Account const nobody("nobody");
        Credentials const credentialsNon{
            {alice2, toBlob("credential1")},
            {alice3, toBlob("credential2")},
            {alice4, toBlob("credential3")},
            {nobody, toBlob("credential4")},
            {alice5, toBlob("credential5")},
            {alice6, toBlob("credential6")},
            {alice7, toBlob("credential7")}};
        env(setTx(account, credentialsNon, domain),
            fee(setFee),
            ter(temBAD_ISSUER));

        Credentials const credentials4{
            {alice2, toBlob("credential1")},
            {alice3, toBlob("credential2")},
            {alice4, toBlob("credential3")},
            {alice5, toBlob("credential4")},
        };
        auto txJsonMutable = setTx(account, credentials4, domain);
        auto const credentialOrig = txJsonMutable["AcceptedCredentials"][2u];

        // Remove Issuer from the 3rd credential and apply.
        txJsonMutable["AcceptedCredentials"][2u]["AcceptedCredential"]
            .removeMember("Issuer");
        env(txJsonMutable, fee(setFee), ter(temMALFORMED));

        txJsonMutable["AcceptedCredentials"][2u] = credentialOrig;
        // Remove Credentialtype from the 3rd credential and apply.
        txJsonMutable["AcceptedCredentials"][2u]["AcceptedCredential"]
            .removeMember("CredentialType");
        env(txJsonMutable, fee(setFee), ter(temMALFORMED));

        // Remove both
        txJsonMutable["AcceptedCredentials"][2u]["AcceptedCredential"]
            .removeMember("Issuer");
        env(txJsonMutable, fee(setFee), ter(temMALFORMED));
    }

    // Test PermissionedDomainSet
    void
    testSet()
    {
        testcase("Set");
        Env env(*this, withFeature_);
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        Account const alice2("alice2");
        env.fund(XRP(1000), alice2);
        Account const alice3("alice3");
        env.fund(XRP(1000), alice3);
        Account const alice4("alice4");
        env.fund(XRP(1000), alice4);
        Account const alice5("alice5");
        env.fund(XRP(1000), alice5);
        Account const alice6("alice6");
        env.fund(XRP(1000), alice6);
        Account const alice7("alice7");
        env.fund(XRP(1000), alice7);
        Account const alice8("alice8");
        env.fund(XRP(1000), alice8);
        Account const alice9("alice9");
        env.fund(XRP(1000), alice9);
        Account const alice10("alice10");
        env.fund(XRP(1000), alice10);
        Account const alice11("alice11");
        env.fund(XRP(1000), alice11);
        Account const alice12("alice12");
        env.fund(XRP(1000), alice12);
        auto const dropsFee = env.current()->fees().increment;
        auto const setFee(drops(dropsFee));

        // Create new from existing account with a single credential.
        Credentials const credentials1{{alice2, toBlob("credential1")}};
        {
            env(setTx(alice, credentials1), fee(setFee));
            BEAST_EXPECT(ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
            auto tx = env.tx()->getJson(JsonOptions::none);
            BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainSet");
            BEAST_EXPECT(tx["Account"] == alice.human());
            auto objects = getObjects(alice, env);
            auto domain = objects.begin()->first;
            auto object = objects.begin()->second;
            BEAST_EXPECT(object["LedgerEntryType"] == "PermissionedDomain");
            BEAST_EXPECT(object["Owner"] == alice.human());
            BEAST_EXPECT(object["Sequence"] == tx["Sequence"]);
            BEAST_EXPECT(credentialsFromJson(object) == credentials1);
        }

        // Create new from existing account with 10 credentials.
        Credentials const credentials10{
            {alice2, toBlob("credential1")},
            {alice3, toBlob("credential2")},
            {alice4, toBlob("credential3")},
            {alice5, toBlob("credential4")},
            {alice6, toBlob("credential5")},
            {alice7, toBlob("credential6")},
            {alice8, toBlob("credential7")},
            {alice9, toBlob("credential8")},
            {alice10, toBlob("credential9")},
            {alice11, toBlob("credential10")},
        };
        uint256 domain2;
        {
            BEAST_EXPECT(
                credentials10.size() == PermissionedDomainSet::PD_ARRAY_MAX);
            BEAST_EXPECT(credentials10 != sortCredentials(credentials10));
            env(setTx(alice, credentials10), fee(setFee));
            auto tx = env.tx()->getJson(JsonOptions::none);
            auto meta = env.meta()->getJson(JsonOptions::none);
            Json::Value a(Json::arrayValue);
            a = meta["AffectedNodes"];

            for (auto const& node : a)
            {
                if (!node.isMember("CreatedNode") ||
                    node["CreatedNode"]["LedgerEntryType"] !=
                        "PermissionedDomain")
                {
                    continue;
                }
                std::ignore = domain2.parseHex(
                    node["CreatedNode"]["LedgerIndex"].asString());
            }
            auto objects = getObjects(alice, env);
            auto object = objects[domain2];
            BEAST_EXPECT(
                credentialsFromJson(object) == sortCredentials(credentials10));
        }

        // Make a new domain with insufficient fee.
        env(setTx(alice, credentials10),
            fee(drops(dropsFee - 1)),
            ter(telINSUF_FEE_P));

        // Update with 1 credential.
        env(setTx(alice, credentials1, domain2));
        BEAST_EXPECT(
            credentialsFromJson(getObjects(alice, env)[domain2]) ==
            credentials1);

        // Update with 10 credentials.
        env(setTx(alice, credentials10, domain2));
        env.close();
        BEAST_EXPECT(
            credentialsFromJson(getObjects(alice, env)[domain2]) ==
            sortCredentials(credentials10));

        // Test bad data when creating a domain.
        testBadData(alice, env);
        // Test bad data when updating a domain.
        testBadData(alice, env, domain2);

        // Try to delete the account with domains.
        auto const acctDelFee(drops(env.current()->fees().increment));
        constexpr std::size_t deleteDelta = 255;
        {
            // Close enough ledgers to make it potentially deletable if empty.
            std::size_t ownerSeq = ownerInfo(alice, env)["Sequence"].asUInt();
            while (deleteDelta + ownerSeq > env.current()->seq())
                env.close();
            env(acctdelete(alice, alice2),
                fee(acctDelFee),
                ter(tecHAS_OBLIGATIONS));
        }

        {
            // Delete the domains and then the owner account.
            for (auto const& objs : getObjects(alice, env))
                env(deleteTx(alice, objs.first));
            env.close();
            std::size_t ownerSeq = ownerInfo(alice, env)["Sequence"].asUInt();
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
        Credentials credentials{{alice, toBlob("first credential")}};
        env(setTx(alice, credentials), fee(setFee));
        env.close();
        auto objects = getObjects(alice, env);
        BEAST_EXPECT(objects.size() == 1);
        auto const domain = objects.begin()->first;

        // Delete a domain that doesn't belong to the account.
        Account const bob("bob");
        env.fund(XRP(1000), bob);
        env(deleteTx(bob, domain), ter(temINVALID_ACCOUNT_ID));

        // Delete a non-existent domain.
        env(deleteTx(alice, uint256(75)), ter(tecNO_ENTRY));

        // Delete a zero domain.
        env(deleteTx(alice, uint256(0)), ter(temMALFORMED));

        // Make sure owner count reflects the existing domain.
        BEAST_EXPECT(ownerInfo(alice, env)["OwnerCount"].asUInt() == 1);
        // Delete domain that belongs to user.
        env(deleteTx(alice, domain), ter(tesSUCCESS));
        auto const tx = env.tx()->getJson(JsonOptions::none);
        BEAST_EXPECT(tx[jss::TransactionType] == "PermissionedDomainDelete");
        // Make sure the owner count goes back to 0.
        BEAST_EXPECT(ownerInfo(alice, env)["OwnerCount"].asUInt() == 0);
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

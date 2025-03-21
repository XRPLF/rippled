//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.
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
#include <test/jtx/delegate.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Permissions.h>

namespace ripple {
namespace test {
class Delegate_test : public beast::unit_test::suite
{
    void
    testFeatureDisabled(FeatureBitset features)
    {
        testcase("test featureDelegate not enabled");
        using namespace jtx;

        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(XRP(1000000), gw, alice, bob);
        env.close();

        // can not set Delegate when feature disabled
        env(delegate_set::delegateSet(gw, alice, {"Payment"}),
            ter(temDISABLED));
    }

    void
    testDelegateSet(FeatureBitset features)
    {
        testcase("test valid request creating, updating, deleting permissions");
        using namespace jtx;

        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        env.fund(XRP(100000), gw, alice);
        env.close();

        auto const permissions = std::vector<std::string>{
            "Payment",
            "EscrowCreate",
            "EscrowFinish",
            "TrustlineAuthorize",
            "CheckCreate"};
        env(delegate_set::delegateSet(gw, alice, permissions));
        env.close();

        // this lambda function is used to get the error message when the
        // user
        // tries to get ledger entry with invalid parameters.
        auto testInvalidParams =
            [&](std::optional<std::string> const& account,
                std::optional<std::string> const& authorize) -> std::string {
            Json::Value jvParams;
            std::string error;
            jvParams[jss::ledger_index] = jss::validated;
            if (account)
                jvParams[jss::delegate][jss::account] = *account;
            if (authorize)
                jvParams[jss::delegate][jss::authorize] = *authorize;
            auto const& response =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            if (response[jss::result].isMember(jss::error))
                error = response[jss::result][jss::error].asString();
            return error;
        };

        // get ledger entry with invalid parameters
        BEAST_EXPECT(
            testInvalidParams(std::nullopt, alice.human()) ==
            "malformedRequest");
        BEAST_EXPECT(
            testInvalidParams(gw.human(), std::nullopt) == "malformedRequest");
        BEAST_EXPECT(
            testInvalidParams("-", alice.human()) == "malformedAddress");
        BEAST_EXPECT(testInvalidParams(gw.human(), "-") == "malformedAddress");

        // this lambda function is used to compare the json value of ledger
        // entry response with the given vector of permissions.
        auto comparePermissions =
            [&](Json::Value const& jle,
                std::vector<std::string> const& permissions,
                Account const& account,
                Account const& authorize) {
                BEAST_EXPECT(
                    !jle[jss::result].isMember(jss::error) &&
                    jle[jss::result].isMember(jss::node));
                BEAST_EXPECT(
                    jle[jss::result][jss::node]["LedgerEntryType"] ==
                    jss::Delegate);
                BEAST_EXPECT(
                    jle[jss::result][jss::node][jss::Account] ==
                    account.human());
                BEAST_EXPECT(
                    jle[jss::result][jss::node][jss::Authorize] ==
                    authorize.human());

                auto const& jPermissions =
                    jle[jss::result][jss::node][sfPermissions.jsonName];
                unsigned i = 0;
                for (auto const& permission : permissions)
                {
                    BEAST_EXPECT(
                        jPermissions[i][sfPermission.jsonName]
                                    [sfPermissionValue.jsonName] == permission);
                    i++;
                }
            };

        // get ledger entry with valid parameter
        comparePermissions(
            delegate_set::ledgerEntry(env, gw, alice), permissions, gw, alice);

        // gw updates permission
        auto const newPermissions = std::vector<std::string>{
            "Payment", "AMMCreate", "AMMDeposit", "AMMWithdraw"};
        env(delegate_set::delegateSet(gw, alice, newPermissions));
        env.close();

        // get ledger entry again, permissions should be updated to
        // newPermissions
        comparePermissions(
            delegate_set::ledgerEntry(env, gw, alice),
            newPermissions,
            gw,
            alice);

        // gw deletes all permissions delegated to alice, this will delete
        // the
        // ledger entry
        env(delegate_set::delegateSet(gw, alice, {}));
        env.close();
        auto const jle = delegate_set::ledgerEntry(env, gw, alice);
        BEAST_EXPECT(jle[jss::result][jss::error] == "entryNotFound");

        // alice can delegate permissions to gw as well
        env(delegate_set::delegateSet(alice, gw, permissions));
        env.close();
        comparePermissions(
            delegate_set::ledgerEntry(env, alice, gw), permissions, alice, gw);
        auto const response = delegate_set::ledgerEntry(env, gw, alice);
        // alice has not been granted any permissions by gw
        BEAST_EXPECT(response[jss::result][jss::error] == "entryNotFound");
    }

    void
    testInvalidRequest(FeatureBitset features)
    {
        testcase("test invalid request");
        using namespace jtx;

        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        env.fund(XRP(100000), gw, alice);
        env.close();

        // when permissions size exceeds the limit 10, should return
        // temARRAY_TOO_LARGE
        {
            env(delegate_set::delegateSet(
                    gw,
                    alice,
                    {"Payment",
                     "EscrowCreate",
                     "EscrowFinish",
                     "EscrowCancel",
                     "CheckCreate",
                     "CheckCash",
                     "CheckCancel",
                     "DepositPreauth",
                     "TrustSet",
                     "NFTokenMint",
                     "NFTokenBurn"}),
                ter(temARRAY_TOO_LARGE));
        }

        // alice can not authorize herself
        {
            env(delegate_set::delegateSet(alice, alice, {"Payment"}),
                ter(temMALFORMED));
        }

        // bad fee
        {
            Json::Value jv;
            jv[jss::TransactionType] = jss::DelegateSet;
            jv[jss::Account] = gw.human();
            jv[jss::Authorize] = alice.human();
            Json::Value permissionsJson(Json::arrayValue);
            Json::Value permissionValue;
            permissionValue[sfPermissionValue.jsonName] = "Payment";
            Json::Value permissionObj;
            permissionObj[sfPermission.jsonName] = permissionValue;
            permissionsJson.append(permissionObj);
            jv[sfPermissions.jsonName] = permissionsJson;
            jv[sfFee.jsonName] = -1;
            env(jv, ter(temBAD_FEE));
        }

        // when the provided permissions include a transaction that does not
        // exist
        {
            try
            {
                env(delegate_set::delegateSet(gw, alice, {"Payment1"}));
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    e.what() ==
                    std::string("invalidParamsError at "
                                "'tx_json.Permissions.[0].Permission'. Field "
                                "'tx_json.Permissions.[0].Permission."
                                "PermissionValue' has invalid data."));
            }
        }

        // when provided permissions contains duplicate values, should return
        // temMALFORMED
        {
            env(delegate_set::delegateSet(
                    gw,
                    alice,
                    {"Payment",
                     "EscrowCreate",
                     "EscrowFinish",
                     "TrustlineAuthorize",
                     "CheckCreate",
                     "TrustlineAuthorize"}),
                ter(temMALFORMED));
        }

        // when authorizing account which does not exist, should return
        // terNO_ACCOUNT
        {
            env(delegate_set::delegateSet(gw, Account("unknown"), {"Payment"}),
                ter(terNO_ACCOUNT));
        }

        // for security reasons, AccountSet, SetRegularKey, SignerListSet,
        // AccountDelete, DelegateSet are prohibited to be delegated to
        // other accounts.
        {
            env(delegate_set::delegateSet(gw, alice, {"SetRegularKey"}),
                ter(tecNO_PERMISSION));
            env(delegate_set::delegateSet(gw, alice, {"AccountSet"}),
                ter(tecNO_PERMISSION));
            env(delegate_set::delegateSet(gw, alice, {"SignerListSet"}),
                ter(tecNO_PERMISSION));
            env(delegate_set::delegateSet(gw, alice, {"DelegateSet"}),
                ter(tecNO_PERMISSION));
            env(delegate_set::delegateSet(gw, alice, {"SetRegularKey"}),
                ter(tecNO_PERMISSION));
        }
    }

    void
    testReserve(FeatureBitset features)
    {
        testcase("test reserve");
        using namespace jtx;

        // test reserve for DelegateSet
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};

            env.fund(drops(env.current()->fees().accountReserve(0)), alice);
            env.fund(
                drops(env.current()->fees().accountReserve(1)), bob, carol);
            env.close();

            // alice does not have enough reserve to create Delegate
            env(delegate_set::delegateSet(alice, bob, {"Payment"}),
                ter(tecINSUFFICIENT_RESERVE));

            // bob has enough reserve
            env(delegate_set::delegateSet(bob, alice, {"Payment"}));
            env.close();

            // now bob create another Delegate, he does not have
            // enough reserve
            env(delegate_set::delegateSet(bob, carol, {"Payment"}),
                ter(tecINSUFFICIENT_RESERVE));
        }

        // test reserve when sending transaction on behalf of other account
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};

            env.fund(drops(env.current()->fees().accountReserve(1)), alice);
            env.fund(drops(env.current()->fees().accountReserve(2)), bob);
            env.close();

            // alice gives bob permission
            env(delegate_set::delegateSet(alice, bob, {"DIDSet", "DIDDelete"}));
            env.close();

            // bob set DID on behalf of alice, but alice does not have enough
            // reserve
            env(did::set(alice),
                did::uri("uri"),
                delegate(bob),
                ter(tecINSUFFICIENT_RESERVE));

            // bob can set DID for himself because he has enough reserve
            env(did::set(bob), did::uri("uri"));
            env.close();
        }
    }

    void
    testFee(FeatureBitset features)
    {
        testcase("test fee");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        env.fund(XRP(10000), alice, carol);
        env.fund(XRP(1000), bob);
        env.close();

        {
            // Fee should be checked before permission check,
            // otherwise tecNO_PERMISSION returned when permission check fails
            // could cause context reset to pay fee because it is tec error
            auto aliceBalance = env.balance(alice);
            auto bobBalance = env.balance(bob);
            auto carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)),
                fee(XRP(2000)),
                delegate(bob),
                ter(terINSUF_FEE_B));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }

        env(delegate_set::delegateSet(alice, bob, {"Payment"}));
        env.close();

        {
            // Delegate pays the fee
            auto aliceBalance = env.balance(alice);
            auto bobBalance = env.balance(bob);
            auto carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)), fee(XRP(10)), delegate(bob));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(100));
            BEAST_EXPECT(env.balance(bob) == bobBalance - XRP(10));
            BEAST_EXPECT(env.balance(carol) == carolBalance + XRP(100));
        }

        {
            // insufficient balance to pay fee
            auto aliceBalance = env.balance(alice);
            auto bobBalance = env.balance(bob);
            auto carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)),
                fee(XRP(2000)),
                delegate(bob),
                ter(terINSUF_FEE_B));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }

        {
            // fee is paid by Delegate
            // on context reset (tec error)
            auto aliceBalance = env.balance(alice);
            auto bobBalance = env.balance(bob);
            auto carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(20000)),
                fee(XRP(10)),
                delegate(bob),
                ter(tecUNFUNDED_PAYMENT));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance - XRP(10));
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("test deleting account");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(XRP(100000), alice, bob);
        env.close();

        env(delegate_set::delegateSet(alice, bob, {"Payment"}));
        env.close();
        BEAST_EXPECT(
            env.closed()->exists(keylet::delegate(alice.id(), bob.id())));

        for (std::uint32_t i = 0; i < 256; ++i)
            env.close();

        auto const aliceBalance = env.balance(alice);
        auto const bobBalance = env.balance(bob);

        // alice deletes account, this will remove the Delegate object
        auto const deleteFee = drops(env.current()->fees().increment);
        env(acctdelete(alice, bob), fee(deleteFee));
        env.close();

        BEAST_EXPECT(!env.closed()->exists(keylet::account(alice.id())));
        BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(alice.id())));
        BEAST_EXPECT(env.balance(bob) == bobBalance + aliceBalance - deleteFee);

        BEAST_EXPECT(
            !env.closed()->exists(keylet::delegate(alice.id(), bob.id())));
    }

    void
    testDelegateTransaction(FeatureBitset features)
    {
        testcase("test delegate transaction");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};

        XRPAmount const baseFee{env.current()->fees().base};

        // use different initial amout to distinguish the source balance
        env.fund(XRP(10000), alice);
        env.fund(XRP(20000), bob);
        env.fund(XRP(30000), carol);
        env.close();
        auto aliceBalance = env.balance(alice, XRP);
        auto bobBalance = env.balance(bob, XRP);
        auto carolBalance = env.balance(carol, XRP);

        env(delegate_set::delegateSet(alice, bob, {"Payment"}));
        env.close();
        env.require(balance(alice, aliceBalance - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);

        // bob pays 50 XRP to carol on behalf of alice
        env(pay(alice, carol, XRP(50)), delegate(bob));
        env.close();
        env.require(balance(alice, aliceBalance - XRP(50)));
        env.require(balance(carol, carolBalance + XRP(50)));
        // bob pays the fee
        env.require(balance(bob, bobBalance - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);
        bobBalance = env.balance(bob, XRP);
        carolBalance = env.balance(carol, XRP);

        // bob pays 50 XRP to bob self on behalf of alice
        env(pay(alice, bob, XRP(50)), delegate(bob));
        env.close();
        env.require(balance(alice, aliceBalance - XRP(50)));
        env.require(balance(bob, bobBalance + XRP(50) - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);
        bobBalance = env.balance(bob, XRP);

        // bob pay 50 XRP to alice herself on behalf of alice
        env(pay(alice, alice, XRP(50)), delegate(bob), ter(temREDUNDANT));
        env.close();

        // bob does not have permission to create check
        env(check::create(alice, bob, XRP(10)),
            delegate(bob),
            ter(tecNO_PERMISSION));
    }

    void
    testPaymentGranular(FeatureBitset features)
    {
        testcase("test payment granular");
        using namespace jtx;

        // test PaymentMint and PaymentBurn
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            auto const USD = gw["USD"];
            auto const EUR = gw2["EUR"];

            env.fund(XRP(10000), alice);
            env.fund(XRP(20000), bob);
            env.fund(XRP(40000), gw, gw2);
            env.trust(USD(200), alice);
            env.trust(EUR(400), gw);
            env.close();

            XRPAmount const baseFee{env.current()->fees().base};
            auto aliceBalance = env.balance(alice, XRP);
            auto bobBalance = env.balance(bob, XRP);
            auto gwBalance = env.balance(gw, XRP);
            auto gw2Balance = env.balance(gw2, XRP);

            // gw gives bob burn permission
            env(delegate_set::delegateSet(gw, bob, {"PaymentBurn"}));
            env.close();
            env.require(balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // bob sends a payment transaction on behalf of gw
            env(pay(gw, alice, USD(50)), delegate(bob), ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // gw gives bob mint permission, alice gives bob burn permission
            env(delegate_set::delegateSet(gw, bob, {"PaymentMint"}));
            env(delegate_set::delegateSet(alice, bob, {"PaymentBurn"}));
            env.close();
            env.require(balance(alice, aliceBalance - drops(baseFee)));
            env.require(balance(gw, gwBalance - drops(baseFee)));
            aliceBalance = env.balance(alice, XRP);
            gwBalance = env.balance(gw, XRP);

            // can not send XRP
            env(pay(gw, alice, XRP(50)), delegate(bob), ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // mint 50 USD
            env(pay(gw, alice, USD(50)), delegate(bob));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(gw, gwBalance));
            env.require(balance(gw, alice["USD"](-50)));
            env.require(balance(alice, USD(50)));
            BEAST_EXPECT(env.balance(bob, USD) == USD(0));
            bobBalance = env.balance(bob, XRP);

            // burn 30 USD
            env(pay(alice, gw, USD(30)), delegate(bob));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(gw, gwBalance));
            env.require(balance(gw, alice["USD"](-20)));
            env.require(balance(alice, USD(20)));
            BEAST_EXPECT(env.balance(bob, USD) == USD(0));
            bobBalance = env.balance(bob, XRP);

            // bob has both mint and burn permissions
            env(delegate_set::delegateSet(
                gw, bob, {"PaymentMint", "PaymentBurn"}));
            env.close();
            env.require(balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // mint 100 USD for gw
            env(pay(gw, alice, USD(100)), delegate(bob));
            env.close();
            env.require(balance(gw, alice["USD"](-120)));
            env.require(balance(alice, USD(120)));
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // gw2 pays gw 200 EUR
            env(pay(gw2, gw, EUR(200)));
            env.close();
            env.require(balance(gw2, gw2Balance - drops(baseFee)));
            gw2Balance = env.balance(gw2, XRP);
            env.require(balance(gw2, gw["EUR"](-200)));
            env.require(balance(gw, EUR(200)));

            // burn 100 EUR for gw
            env(pay(gw, gw2, EUR(100)), delegate(bob));
            env.close();
            env.require(balance(gw2, gw["EUR"](-100)));
            env.require(balance(gw, EUR(100)));
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(gw, gwBalance));
            env.require(balance(gw2, gw2Balance));
            env.require(balance(alice, aliceBalance));
        }

        // test PaymentMint won't affect Payment transaction level delegation.
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account gw{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(10000), alice);
            env.fund(XRP(20000), bob);
            env.fund(XRP(40000), gw);
            env.trust(USD(200), alice);
            env.close();

            XRPAmount const baseFee{env.current()->fees().base};

            auto aliceBalance = env.balance(alice, XRP);
            auto bobBalance = env.balance(bob, XRP);
            auto gwBalance = env.balance(gw, XRP);

            // gw gives bob PaymentBurn permission
            env(delegate_set::delegateSet(gw, bob, {"PaymentBurn"}));
            env.close();
            env.require(balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // bob can not mint on behalf of gw because he only has burn
            // permission
            env(pay(gw, alice, USD(50)), delegate(bob), ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // gw gives bob Payment permission as well
            env(delegate_set::delegateSet(gw, bob, {"PaymentBurn", "Payment"}));
            env.close();
            env.require(balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // bob now can mint on behalf of gw
            env(pay(gw, alice, USD(50)), delegate(bob));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(gw, gwBalance));
            env.require(balance(alice, aliceBalance));
            env.require(balance(gw, alice["USD"](-50)));
            env.require(balance(alice, USD(50)));
            BEAST_EXPECT(env.balance(bob, USD) == USD(0));
        }
    }

    void
    testTrustSetGranular(FeatureBitset features)
    {
        testcase("test TrustSet granular permissions");
        using namespace jtx;

        // test TrustlineUnfreeze, TrustlineFreeze and TrustlineAuthorize
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(alice, asfRequireAuth));
            env.close();

            env(delegate_set::delegateSet(alice, bob, {"TrustlineUnfreeze"}));
            env.close();
            // bob can not create trustline on behalf of alice because he only
            // has unfreeze permission
            env(trust(alice, gw["USD"](50), 0),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env.close();

            // alice creates trustline by herself
            env(trust(alice, gw["USD"](50), 0));
            env.close();

            // unsupported flags
            env(trust(alice, gw["USD"](50), tfSetNoRipple),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env(trust(alice, gw["USD"](50), tfClearNoRipple),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env.close();

            // supported flags with wrong permission
            env(trust(alice, gw["USD"](50), tfSetfAuth),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env(trust(alice, gw["USD"](50), tfSetFreeze),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env.close();
            env(delegate_set::delegateSet(alice, bob, {"TrustlineAuthorize"}));
            env.close();
            env(trust(alice, gw["USD"](50), tfClearFreeze),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env.close();

            // supported flags with correct permission
            env(trust(alice, gw["USD"](50), tfSetfAuth), delegate(bob));
            env.close();
            env(delegate_set::delegateSet(
                alice, bob, {"TrustlineAuthorize", "TrustlineFreeze"}));
            env.close();
            env(trust(alice, gw["USD"](50), tfSetFreeze), delegate(bob));
            env.close();
            env(delegate_set::delegateSet(
                alice, bob, {"TrustlineAuthorize", "TrustlineUnfreeze"}));
            env.close();
            env(trust(alice, gw["USD"](50), tfClearFreeze), delegate(bob));
            env.close();
            // but bob can not freeze trustline because he no longer has freeze
            // permission
            env(trust(alice, gw["USD"](50), tfSetFreeze),
                delegate(bob),
                ter(tecNO_PERMISSION));

            // cannot update LimitAmout with granular permission, both high and
            // low account
            env(trust(gw, alice["USD"](50), 0));
            env(delegate_set::delegateSet(gw, bob, {"TrustlineUnfreeze"}));
            env.close();
            env(trust(alice, gw["USD"](100)),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env(trust(gw, alice["USD"](100)),
                delegate(bob),
                ter(tecNO_PERMISSION));
        }

        // test mix of transaction level delegation and granular delegation
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(alice, asfRequireAuth));
            env.close();

            env(delegate_set::delegateSet(
                alice, bob, {"TrustlineUnfreeze", "NFTokenCreateOffer"}));
            env.close();
            env(trust(alice, gw["USD"](50), 0),
                delegate(bob),
                ter(tecNO_PERMISSION));

            // add TrustSet permission and some unrelated permission
            env(delegate_set::delegateSet(
                alice,
                bob,
                {"TrustlineUnfreeze",
                 "NFTokenCreateOffer",
                 "TrustSet",
                 "AccountTransferRateSet"}));
            env.close();
            env(trust(alice, gw["USD"](50), 0), delegate(bob));
            env.close();

            // since bob has TrustSet permission, he does not need
            // TrustlineFreeze granular permission to freeze the trustline
            env(trust(alice, gw["USD"](50), tfSetFreeze), delegate(bob));
            env(trust(alice, gw["USD"](50), tfClearFreeze), delegate(bob));
            env(trust(alice, gw["USD"](50), tfSetNoRipple), delegate(bob));
            env(trust(alice, gw["USD"](50), tfClearNoRipple), delegate(bob));
            env(trust(alice, gw["USD"](50), tfSetfAuth), delegate(bob));
        }
    }

    void
    testAccountSetGranular(FeatureBitset features)
    {
        testcase("test AccountSet granular permissions");
        using namespace jtx;

        // test AccountDomainSet, AccountEmailHashSet,
        // AccountMessageKeySet,AccountTransferRateSet, and AccountTickSizeSet
        // granular permissions
        {
            Env env(*this, features);
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            // alice gives bob some random permission, which is not related to
            // the AccountSet transaction
            env(delegate_set::delegateSet(alice, bob, {"TrustlineUnfreeze"}));
            env.close();

            // bob does not have permission to set domain
            // on behalf of alice
            std::string const domain = "example.com";
            auto jt = noop(alice);
            jt[sfDomain.fieldName] = strHex(domain);
            jt[sfDelegate.fieldName] = bob.human();

            // add granular permission related to AccountSet but is not the
            // correct permission for domain set
            env(delegate_set::delegateSet(
                alice, bob, {"TrustlineUnfreeze", "AccountEmailHashSet"}));
            env.close();
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountDomainSet to bob
            env(delegate_set::delegateSet(alice, bob, {"AccountDomainSet"}));
            env.close();

            // bob set account domain on behalf of alice
            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

            // bob can reset domain
            jt[sfDomain.fieldName] = "";
            env(jt);
            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfDomain));

            // bob tries to update domain and set email hash,
            // but he does not have permission to set email hash
            jt[sfDomain.fieldName] = strHex(domain);
            std::string const mh("5F31A79367DC3137FADA860C05742EE6");
            jt[sfEmailHash.fieldName] = mh;
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountEmailHashSet to bob
            env(delegate_set::delegateSet(
                alice, bob, {"AccountDomainSet", "AccountEmailHashSet"}));
            env.close();
            env(jt);
            BEAST_EXPECT(to_string((*env.le(alice))[sfEmailHash]) == mh);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

            // bob does not have permission to set message key for alice
            auto const rkp = randomKeyPair(KeyType::ed25519);
            jt[sfMessageKey.fieldName] = strHex(rkp.first.slice());
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountMessageKeySet to bob
            env(delegate_set::delegateSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            // bob can set message key for alice
            env(jt);
            BEAST_EXPECT(
                strHex((*env.le(alice))[sfMessageKey]) ==
                strHex(rkp.first.slice()));
            jt[sfMessageKey.fieldName] = "";
            env(jt);
            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfMessageKey));

            // bob does not have permission to set transfer rate for alice
            env(rate(alice, 2.0), delegate(bob), ter(tecNO_PERMISSION));

            // alice give granular permission of AccountTransferRateSet to bob
            env(delegate_set::delegateSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet",
                 "AccountTransferRateSet"}));
            env.close();
            env(rate(alice, 2.0), delegate(bob));
            BEAST_EXPECT((*env.le(alice))[sfTransferRate] == 2000000000);

            // bob does not have permission to set ticksize for alice
            jt[sfTickSize.fieldName] = 8;
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountTickSizeSet to bob
            env(delegate_set::delegateSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet",
                 "AccountTransferRateSet",
                 "AccountTickSizeSet"}));
            env.close();
            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfTickSize] == 8);

            // can not set asfRequireAuth flag for alice
            env(fset(alice, asfRequireAuth),
                delegate(bob),
                ter(tecNO_PERMISSION));

            // reset Delegate will delete the Delegate
            // object
            env(delegate_set::delegateSet(alice, bob, {}));
            // bib still does not have permission to set asfRequireAuth for
            // alice
            env(fset(alice, asfRequireAuth),
                delegate(bob),
                ter(tecNO_PERMISSION));
            // alice can set for herself
            env(fset(alice, asfRequireAuth));
            env.require(flags(alice, asfRequireAuth));
            env.close();

            // can not update tick size because bob no longer has permission
            jt[sfTickSize.fieldName] = 7;
            env(jt, ter(tecNO_PERMISSION));

            env(delegate_set::delegateSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            // bob does not have permission to set wallet locater for alice
            std::string const locator =
                "9633EC8AF54F16B5286DB1D7B519EF49EEFC050C0C8AC4384F1D88ACD1BFDF"
                "05";
            auto jt2 = noop(alice);
            jt2[sfDomain.fieldName] = strHex(domain);
            jt2[sfDelegate.fieldName] = bob.human();
            jt2[sfWalletLocator.fieldName] = locator;
            env(jt2, ter(tecNO_PERMISSION));
        }

        // can not set AccountSet flags on behalf of other account
        {
            Env env(*this, features);
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto testSetClearFlag = [&](std::uint32_t flag) {
                // bob can not set flag on behalf of alice
                env(fset(alice, flag), delegate(bob), ter(tecNO_PERMISSION));
                // alice set by herself
                env(fset(alice, flag));
                env.close();
                env.require(flags(alice, flag));
                // bob can not clear on behalf of alice
                env(fclear(alice, flag), delegate(bob), ter(tecNO_PERMISSION));
            };

            // testSetClearFlag(asfNoFreeze);
            testSetClearFlag(asfRequireAuth);
            testSetClearFlag(asfAllowTrustLineClawback);

            // alice gives some granular permissions to bob
            env(delegate_set::delegateSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            testSetClearFlag(asfDefaultRipple);
            testSetClearFlag(asfDepositAuth);
            testSetClearFlag(asfDisallowIncomingCheck);
            testSetClearFlag(asfDisallowIncomingNFTokenOffer);
            testSetClearFlag(asfDisallowIncomingPayChan);
            testSetClearFlag(asfDisallowIncomingTrustline);
            testSetClearFlag(asfDisallowXRP);
            testSetClearFlag(asfRequireDest);
            testSetClearFlag(asfGlobalFreeze);

            // bob can not set asfAccountTxnID on behalf of alice
            env(fset(alice, asfAccountTxnID),
                delegate(bob),
                ter(tecNO_PERMISSION));
            env(fset(alice, asfAccountTxnID));
            env.close();
            BEAST_EXPECT(env.le(alice)->isFieldPresent(sfAccountTxnID));
            env(fclear(alice, asfAccountTxnID),
                delegate(bob),
                ter(tecNO_PERMISSION));

            // bob can not set asfAuthorizedNFTokenMinter on behalf of alice
            Json::Value jt = fset(alice, asfAuthorizedNFTokenMinter);
            jt[sfDelegate.fieldName] = bob.human();
            jt[sfNFTokenMinter.fieldName] = bob.human();
            env(jt, ter(tecNO_PERMISSION));

            // bob gives alice some permissions
            env(delegate_set::delegateSet(
                bob,
                alice,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            // since we can not set asfNoFreeze if asfAllowTrustLineClawback is
            // set, which can not be clear either. Test alice set asfNoFreeze on
            // behalf of bob.
            env(fset(alice, asfNoFreeze), delegate(bob), ter(tecNO_PERMISSION));
            env(fset(bob, asfNoFreeze));
            env.close();
            env.require(flags(bob, asfNoFreeze));
            // alice can not clear on behalf of bob
            env(fclear(alice, asfNoFreeze),
                delegate(bob),
                ter(tecNO_PERMISSION));

            // bob can not set asfDisableMaster on behalf of alice
            Account const bobKey{"bobKey", KeyType::secp256k1};
            env(regkey(bob, bobKey));
            env.close();
            env(fset(alice, asfDisableMaster),
                delegate(bob),
                sig(bob),
                ter(tecNO_PERMISSION));
        }
    }

    void
    testMPTokenIssuanceSetGranular(FeatureBitset features)
    {
        testcase("test MPTokenIssuanceSet granular");
        using namespace jtx;

        // test MPTokenIssuanceUnlock and MPTokenIssuanceLock permissions
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceUnlock
            env(delegate_set::delegateSet(
                alice, bob, {"MPTokenIssuanceUnlock"}));
            env.close();
            // bob does not have lock permission
            mpt.set(
                {.account = alice,
                 .flags = tfMPTLock,
                 .delegate = bob,
                 .err = tecNO_PERMISSION});
            // bob now has lock permission, but does not have unlock permission
            env(delegate_set::delegateSet(alice, bob, {"MPTokenIssuanceLock"}));
            env.close();
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
            mpt.set(
                {.account = alice,
                 .flags = tfMPTUnlock,
                 .delegate = bob,
                 .err = tecNO_PERMISSION});

            // now bob can lock and unlock
            env(delegate_set::delegateSet(
                alice, bob, {"MPTokenIssuanceLock", "MPTokenIssuanceUnlock"}));
            env.close();
            mpt.set({.account = alice, .flags = tfMPTUnlock, .delegate = bob});
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
            env.close();
        }

        // test mix of granular and transaction level permission
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceLock
            env(delegate_set::delegateSet(alice, bob, {"MPTokenIssuanceLock"}));
            env.close();
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
            // bob does not have unlock permission
            mpt.set(
                {.account = alice,
                 .flags = tfMPTUnlock,
                 .delegate = bob,
                 .err = tecNO_PERMISSION});

            // alice gives bob some unrelated permission with
            // MPTokenIssuanceLock
            env(delegate_set::delegateSet(
                alice,
                bob,
                {"NFTokenMint", "MPTokenIssuanceLock", "NFTokenBurn"}));
            env.close();
            // bob can not unlock
            mpt.set(
                {.account = alice,
                 .flags = tfMPTUnlock,
                 .delegate = bob,
                 .err = tecNO_PERMISSION});

            // alice add MPTokenIssuanceSet to permissions
            env(delegate_set::delegateSet(
                alice,
                bob,
                {"NFTokenMint",
                 "MPTokenIssuanceLock",
                 "NFTokenBurn",
                 "MPTokenIssuanceSet"}));
            mpt.set({.account = alice, .flags = tfMPTUnlock, .delegate = bob});
            // alice can lock by herself
            mpt.set({.account = alice, .flags = tfMPTLock});
            mpt.set({.account = alice, .flags = tfMPTUnlock, .delegate = bob});
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
        }
    }

    void
    testSingleSign(FeatureBitset features)
    {
        testcase("test single sign");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        env.fund(XRP(100000), alice, bob, carol);
        env.close();

        env(delegate_set::delegateSet(alice, bob, {"Payment"}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);

        env(pay(alice, carol, XRP(100)), fee(XRP(10)), delegate(bob), sig(bob));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(100));
        BEAST_EXPECT(env.balance(bob) == bobBalance - XRP(10));
        BEAST_EXPECT(env.balance(carol) == carolBalance + XRP(100));
    }

    void
    testSingleSignBadSecret(FeatureBitset features)
    {
        testcase("test single sign with bad secret");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        env.fund(XRP(100000), alice, bob, carol);
        env.close();

        env(delegate_set::delegateSet(alice, bob, {"Payment"}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);

        env(pay(alice, carol, XRP(100)),
            fee(XRP(10)),
            delegate(bob),
            sig(alice),
            ter(tefBAD_AUTH));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance);
        BEAST_EXPECT(env.balance(bob) == bobBalance);
        BEAST_EXPECT(env.balance(carol) == carolBalance);
    }

    void
    testMultiSign(FeatureBitset features)
    {
        testcase("test multi sign");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        Account daria{"daria"};
        Account edward{"edward"};
        env.fund(XRP(100000), alice, bob, carol, daria, edward);
        env.close();

        env(signers(bob, 2, {{daria, 1}, {edward, 1}}));
        env.close();

        env(delegate_set::delegateSet(alice, bob, {"Payment"}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);
        auto dariaBalance = env.balance(daria);
        auto edwardBalance = env.balance(edward);

        env(pay(alice, carol, XRP(100)),
            fee(XRP(10)),
            delegate(bob),
            msig(daria, edward));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(100));
        BEAST_EXPECT(env.balance(bob) == bobBalance - XRP(10));
        BEAST_EXPECT(env.balance(carol) == carolBalance + XRP(100));
        BEAST_EXPECT(env.balance(daria) == dariaBalance);
        BEAST_EXPECT(env.balance(edward) == edwardBalance);
    }

    void
    testMultiSignQuorumNotMet(FeatureBitset features)
    {
        testcase("test multi sign which does not meet quorum");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        Account daria = Account{"daria"};
        Account edward = Account{"edward"};
        Account fred = Account{"fred"};
        env.fund(XRP(100000), alice, bob, carol, daria, edward, fred);
        env.close();

        env(signers(bob, 3, {{daria, 1}, {edward, 1}, {fred, 1}}));
        env.close();

        env(delegate_set::delegateSet(alice, bob, {"Payment"}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);
        auto dariaBalance = env.balance(daria);
        auto edwardBalance = env.balance(edward);

        env(pay(alice, carol, XRP(100)),
            fee(XRP(10)),
            delegate(bob),
            msig(daria, edward),
            ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance);
        BEAST_EXPECT(env.balance(bob) == bobBalance);
        BEAST_EXPECT(env.balance(carol) == carolBalance);
        BEAST_EXPECT(env.balance(daria) == dariaBalance);
        BEAST_EXPECT(env.balance(edward) == edwardBalance);
    }

    void
    run() override
    {
        FeatureBitset const all{jtx::supported_amendments()};
        testFeatureDisabled(all - featureDelegate);
        testDelegateSet(all);
        testInvalidRequest(all);
        testReserve(all);
        testFee(all);
        testAccountDelete(all);
        testDelegateTransaction(all);
        testPaymentGranular(all);
        testTrustSetGranular(all);
        testAccountSetGranular(all);
        testMPTokenIssuanceSetGranular(all);
        testSingleSign(all);
        testSingleSignBadSecret(all);
        testMultiSign(all);
        testMultiSignQuorumNotMet(all);
    }
};
BEAST_DEFINE_TESTSUITE(Delegate, app, ripple);
}  // namespace test
}  // namespace ripple
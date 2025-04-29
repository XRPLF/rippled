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

#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpld/ledger/View.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

using namespace test::jtx;

class Vault_test : public beast::unit_test::suite
{
    static auto constexpr negativeAmount =
        [](PrettyAsset const& asset) -> PrettyAmount {
        return {STAmount{asset.raw(), 1ul, 0, true, STAmount::unchecked{}}, ""};
    };

    void
    testSequences()
    {
        using namespace test::jtx;

        auto const testSequence = [this](
                                      std::string const& prefix,
                                      Env& env,
                                      Account const& issuer,
                                      Account const& owner,
                                      Account const& depositor,
                                      Account const& charlie,
                                      Vault& vault,
                                      PrettyAsset const& asset) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            BEAST_EXPECT(env.le(keylet));

            auto const share = [&env, keylet = keylet, this]() -> PrettyAsset {
                auto const vault = env.le(keylet);
                BEAST_EXPECT(vault != nullptr);
                return MPTIssue(vault->at(sfShareMPTID));
            }();

            // Several 3rd party accounts which cannot receive funds
            Account alice{"alice"};
            Account dave{"dave"};
            Account erin{"erin"};  // not authorized by issuer
            env.fund(XRP(1000), alice, dave, erin);
            env(fset(alice, asfDepositAuth));
            env(fset(dave, asfRequireDest));
            env.close();

            {
                testcase(prefix + " fail to set negative maximum");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = negativeAmount(asset).number();
                env(tx, ter{temMALFORMED});
            }

            {
                testcase(prefix + " fail to deposit more than assets held");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(10000)});
                env(tx, ter(tecINSUFFICIENT_FUNDS));
            }

            {
                testcase(prefix + " fail to deposit negative amount");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = negativeAmount(asset)});
                env(tx, ter(temBAD_AMOUNT));
            }

            {
                testcase(prefix + " fail to deposit zero amount");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(0)});
                env(tx, ter(temBAD_AMOUNT));
            }

            {
                testcase(prefix + " fail to deposit to zero vaultID");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = beast::zero,
                     .amount = asset(10)});
                env(tx, ter(temMALFORMED));
            }

            {
                testcase(prefix + " deposit non-zero amount");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                env(tx);
            }

            {
                testcase(prefix + " deposit non-zero amount again");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                env(tx);
            }

            {
                testcase(prefix + " fail to delete non-empty vault");
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx, ter(tecHAS_OBLIGATIONS));
            }

            {
                testcase(prefix + " fail to update because wrong owner");
                auto tx = vault.set({.owner = issuer, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(50).number();
                env(tx, ter(tecNO_PERMISSION));
            }

            {
                testcase(prefix + " fail to update immutable flags");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfFlags] = tfVaultPrivate;
                env(tx, ter(temINVALID_FLAG));
            }

            {
                testcase(
                    prefix + " fail to set maximum lower than current amount");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(50).number();
                env(tx, ter(tecLIMIT_EXCEEDED));
            }

            {
                testcase(prefix + " fail to set zero vault");
                auto tx = vault.set({.owner = owner, .id = beast::zero});
                tx[sfAssetsMaximum] = asset(150).number();
                env(tx, ter(temMALFORMED));
            }

            {
                testcase(prefix + " set maximum higher than current amount");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(150).number();
                env(tx);
            }

            {
                testcase(prefix + " fail to set domain on public vault");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(base_uint<256>(42ul));
                env(tx, ter{tecNO_PERMISSION});
            }

            {
                testcase(prefix + " fail to deposit more than maximum");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx, ter(tecLIMIT_EXCEEDED));
            }

            {
                testcase(prefix + " reset maximum to zero i.e. not enforced");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(0).number();
                env(tx);
            }

            {
                testcase(prefix + " fail to withdraw negative amount");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = negativeAmount(asset)});
                env(tx, ter(temBAD_AMOUNT));
            }

            {
                testcase(prefix + " fail to withdraw zero amount");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(0)});
                env(tx, ter(temBAD_AMOUNT));
            }

            {
                testcase(prefix + " fail to withdraw more than assets held");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1000)});
                env(tx, ter(tecINSUFFICIENT_FUNDS));
            }

            {
                testcase(prefix + " deposit some more");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx);
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " fail to clawback because wrong issuer");
                auto tx = vault.clawback(
                    {.issuer = owner,
                     .id = keylet.key,
                     .holder = depositor,
                     .amount = asset(50)});
                env(tx, ter(temMALFORMED));
            }

            {
                testcase(prefix + " fail to clawback negative amount");
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = depositor,
                     .amount = negativeAmount(asset)});
                env(tx, ter(temBAD_AMOUNT));
            }

            {
                testcase(prefix + " clawback some");
                auto code =
                    asset.raw().native() ? ter(temMALFORMED) : ter(tesSUCCESS);
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = depositor,
                     .amount = asset(10)});
                env(tx, code);
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " fail to clawback zero vault");
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = beast::zero,
                     .holder = depositor,
                     .amount = asset(10)});
                env(tx, ter(temMALFORMED));
            }

            {
                testcase(prefix + " clawback all");
                auto code = asset.raw().native() ? ter(tecNO_PERMISSION)
                                                 : ter(tesSUCCESS);
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = depositor});
                env(tx, code);
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " deposit again");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(200)});
                env(tx);
            }

            {
                testcase(prefix + " fail to withdraw zero vault");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = beast::zero,
                     .amount = asset(100)});
                env(tx, ter(temMALFORMED));
            }

            {
                testcase(
                    prefix + " fail to withdraw to 3rd party lsfDepositAuth");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                tx[sfDestination] = alice.human();
                env(tx, ter{tecNO_PERMISSION});
            }

            if (!asset.raw().native())
            {
                testcase(
                    prefix + " fail to withdraw to 3rd party no authorization");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                tx[sfDestination] = erin.human();
                env(tx,
                    ter{asset.raw().holds<Issue>() ? tecNO_LINE : tecNO_AUTH});
            }

            if (!asset.raw().native() && asset.raw().holds<Issue>())
            {
                testcase(prefix + " temporary authorization for 3rd party");
                env(trust(erin, asset(1000)));
                env(trust(issuer, asset(0), erin, tfSetfAuth));
                env(pay(issuer, erin, asset(10)));

                // Erin deposits all in vault, then sends shares to depositor
                auto tx = vault.deposit(
                    {.depositor = erin, .id = keylet.key, .amount = asset(10)});
                env(tx);
                env(pay(erin, depositor, share(10)));

                testcase(prefix + " withdraw to authorized 3rd party");
                // Depositor withdraws shares, destined to Erin
                tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[sfDestination] = erin.human();
                env(tx);
                // Erin returns assets to issuer
                env(pay(erin, issuer, asset(10)));

                testcase(prefix + " fail to pay to unauthorized 3rd party");
                env(trust(erin, asset(0)));
                // Erin has MPToken but is no longer authorized to hold assets
                env(pay(depositor, erin, share(1)), ter{tecNO_LINE});
            }

            {
                testcase(
                    prefix +
                    " fail to withdraw to 3rd party lsfRequireDestTag");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                tx[sfDestination] = dave.human();
                env(tx, ter{tecDST_TAG_NEEDED});
            }

            {
                testcase(prefix + " withdraw to authorized 3rd party");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                tx[sfDestination] = charlie.human();
                env(tx);
            }

            {
                testcase(prefix + " withdraw to issuer");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                tx[sfDestination] = issuer.human();
                env(tx);
            }

            {
                testcase(prefix + " withdraw remaining assets");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                env(tx);
            }

            {
                testcase(prefix + " fail to delete because wrong owner");
                auto tx = vault.del({.owner = issuer, .id = keylet.key});
                env(tx, ter(tecNO_PERMISSION));
            }

            {
                testcase(prefix + " fail to delete zero vault");
                auto tx = vault.del({.owner = owner, .id = beast::zero});
                env(tx, ter(temMALFORMED));
            }

            {
                testcase(prefix + " delete empty vault");
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx);
                BEAST_EXPECT(!env.le(keylet));
            }
        };

        auto testCases = [this, &testSequence](
                             std::string prefix,
                             std::function<PrettyAsset(
                                 Env & env,
                                 Account const& issuer,
                                 Account const& owner,
                                 Account const& depositor,
                                 Account const& charlie)> setup) {
            Env env{*this, supported_amendments() | featureSingleAssetVault};
            Account issuer{"issuer"};
            Account owner{"owner"};
            Account depositor{"depositor"};
            Account charlie{"charlie"};  // authorized 3rd party
            Vault vault{env};
            env.fund(XRP(1000), issuer, owner, depositor, charlie);
            env.close();
            env(fset(issuer, asfAllowTrustLineClawback));
            env(fset(issuer, asfRequireAuth));
            env.close();
            env.require(flags(issuer, asfAllowTrustLineClawback));
            env.require(flags(issuer, asfRequireAuth));

            PrettyAsset asset = setup(env, issuer, owner, depositor, charlie);
            testSequence(
                prefix, env, issuer, owner, depositor, charlie, vault, asset);
        };

        testCases(
            "XRP",
            [](Env& env,
               Account const& issuer,
               Account const& owner,
               Account const& depositor,
               Account const& charlie) -> PrettyAsset {
                return {xrpIssue(), 1'000'000};
            });

        testCases(
            "IOU",
            [](Env& env,
               Account const& issuer,
               Account const& owner,
               Account const& depositor,
               Account const& charlie) -> Asset {
                PrettyAsset asset = issuer["IOU"];
                env(trust(owner, asset(1000)));
                env(trust(depositor, asset(1000)));
                env(trust(charlie, asset(1000)));
                env(trust(issuer, asset(0), owner, tfSetfAuth));
                env(trust(issuer, asset(0), depositor, tfSetfAuth));
                env(trust(issuer, asset(0), charlie, tfSetfAuth));
                env(pay(issuer, depositor, asset(1000)));
                env.close();
                return asset;
            });

        testCases(
            "MPT",
            [](Env& env,
               Account const& issuer,
               Account const& owner,
               Account const& depositor,
               Account const& charlie) -> Asset {
                MPTTester mptt{env, issuer, mptInitNoFund};
                mptt.create(
                    {.flags =
                         tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
                PrettyAsset asset = mptt.issuanceID();
                mptt.authorize({.account = depositor});
                mptt.authorize({.account = charlie});
                env(pay(issuer, depositor, asset(1000)));
                env.close();
                return asset;
            });
    }

    void
    testPreflight()
    {
        {
            testcase("disabled single asset vault");
            Env env{*this, supported_amendments() - featureSingleAssetVault};
            Account issuer{"issuer"};
            Account owner{"owner"};
            env.fund(XRP(100000), issuer, owner);
            env.close();

            PrettyAsset asset = issuer["IOU"];
            env(trust(owner, asset(1000)));
            env(pay(issuer, owner, asset(1000)));
            env.close();

            Vault vault{env};
            auto [tx0, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx0, ter{temDISABLED});

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                env(tx, ter{temDISABLED});
            }

            {
                auto tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx, ter{temDISABLED});
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx, ter{temDISABLED});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = asset(10)});
                env(tx, ter{temDISABLED});
            }

            {
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx, ter{temDISABLED});
            }
        }

        {
            testcase("invalid flags");
            Env env{*this, supported_amendments() | featureSingleAssetVault};
            Account issuer{"issuer"};
            Account owner{"owner"};
            env.fund(XRP(100000), issuer, owner);
            env.close();

            PrettyAsset asset = issuer["IOU"];
            env(trust(owner, asset(1000)));
            env(pay(issuer, owner, asset(1000)));
            env.close();

            Vault vault{env};
            auto [tx0, keylet] = vault.create({.owner = owner, .asset = asset});
            tx0[sfFlags] = tfClearDeepFreeze;
            env(tx0, ter{temINVALID_FLAG});

            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                tx[sfFlags] = tfVaultPrivate | tfVaultShareNonTransferable;
                env(tx);
            }

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, ter{temINVALID_FLAG});
            }

            {
                auto tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, ter{temINVALID_FLAG});
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, ter{temINVALID_FLAG});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = asset(10)});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, ter{temINVALID_FLAG});
            }

            {
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, ter{temINVALID_FLAG});
            }
        }

        {
            testcase("disabled permissioned domain");

            Env env{
                *this,
                (supported_amendments() | featureSingleAssetVault) -
                    featurePermissionedDomains};
            Account owner{"owner"};
            env.fund(XRP(100000), owner);
            env.close();

            Vault vault{env};
            auto [tx, keylet] =
                vault.create({.owner = owner, .asset = xrpIssue()});
            tx[sfDomainID] = to_string(base_uint<256>(42ul));
            env(tx, ter{temDISABLED});

            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = xrpIssue()});
                env(tx);
            }

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(base_uint<256>(42ul));
                env(tx, ter{temDISABLED});
            }
        }
    }

    // Test for non-asset specific behaviors.
    void
    testCreateFailXRP()
    {
        using namespace test::jtx;

        auto testCase = [this](std::function<void(
                                   Env & env,
                                   Account const& issuer,
                                   Account const& owner,
                                   Account const& depositor,
                                   Asset const& asset,
                                   Vault& vault)> test) {
            Env env{*this, supported_amendments() | featureSingleAssetVault};
            Account issuer{"issuer"};
            Account owner{"owner"};
            Account depositor{"depositor"};
            env.fund(XRP(1000), issuer, owner, depositor);
            env.close();
            Vault vault{env};
            Asset asset = xrpIssue();

            test(env, issuer, owner, depositor, asset, vault);
        };

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault) {
            testcase("nothing to set");
            auto tx = vault.set({.owner = owner, .id = keylet::skip().key});
            tx[sfAssetsMaximum] = asset(0).number();
            env(tx, ter(tecNO_ENTRY));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault) {
            testcase("nothing to deposit to");
            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet::skip().key,
                 .amount = asset(10)});
            env(tx, ter(tecNO_ENTRY));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault) {
            testcase("nothing to withdraw from");
            auto tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet::skip().key,
                 .amount = asset(10)});
            env(tx, ter(tecNO_ENTRY));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("nothing to delete");
            auto tx = vault.del({.owner = owner, .id = keylet::skip().key});
            env(tx, ter(tecNO_ENTRY));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("cannot create public vault with domain");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfDomainID] = to_string(base_uint<256>(42ul));
            env(tx, ter{temMALFORMED});
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("cannot create public vault with negative maximum");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfAssetsMaximum] = negativeAmount(asset);
            env(tx, ter{temMALFORMED});
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            testcase("transaction is good");
            env(tx);
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfWithdrawalPolicy] = 1;
            testcase("explicitly select withdrawal policy");
            env(tx);
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfWithdrawalPolicy] = 0;
            testcase("invalid withdrawal policy");
            env(tx, ter(temMALFORMED));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            testcase("insufficient fee");
            env(tx, fee(env.current()->fees().base), ter(telINSUF_FEE_P));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            testcase("insufficient reserve");
            // It is possible to construct a complicated mathematical
            // expression for this amount, but it is sadly not easy.
            env(pay(owner, issuer, XRP(775)));
            env.close();
            env(tx, ter(tecINSUFFICIENT_RESERVE));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            testcase("empty data");
            tx[sfData] = "";
            env(tx, ter(temMALFORMED));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            testcase("data too large");
            // A hexadecimal string of 257 bytes.
            tx[sfData] = std::string(514, 'A');
            env(tx, ter(temMALFORMED));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            testcase("empty metadata");
            tx[sfMPTokenMetadata] = "";
            env(tx, ter(temMALFORMED));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            testcase("metadata too large");
            // This metadata is for the share token.
            // A hexadecimal string of 1025 bytes.
            tx[sfMPTokenMetadata] = std::string(2050, 'B');
            env(tx, ter(temMALFORMED));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfFlags] = tfVaultPrivate;
            tx[sfDomainID] = to_string(base_uint<256>(0));
            testcase("invalid zero domain");
            env(tx, ter{temMALFORMED});
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfFlags] = tfVaultPrivate;
            tx[sfDomainID] = to_string(base_uint<256>(42ul));
            testcase("non-existing domain");
            env(tx, ter{tecOBJECT_NOT_FOUND});
        });
    }

    void
    testCreateFailIOU()
    {
        using namespace test::jtx;
        {
            testcase("IOU fail create frozen");
            Env env{*this, supported_amendments() | featureSingleAssetVault};
            Account issuer{"issuer"};
            Account owner{"owner"};
            Account depositor{"depositor"};
            env.fund(XRP(1000), issuer, owner, depositor);
            env.close();
            Vault vault{env};
            Asset asset = issuer["IOU"];

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            env(fset(issuer, asfGlobalFreeze));
            env.close();
            env(tx, ter(tecFROZEN));
            env.close();
        }

        {
            testcase("IOU fail create vault for AMM LPToken");
            Env env{*this, supported_amendments() | featureSingleAssetVault};
            Account const gw("gateway");
            Account const alice("alice");
            Account const carol("carol");
            IOU const USD = gw["USD"];

            auto const [asset1, asset2] =
                std::pair<STAmount, STAmount>(XRP(10000), USD(10000));
            auto tofund = [&](STAmount const& a) -> STAmount {
                if (a.native())
                {
                    auto const defXRP = XRP(30000);
                    if (a <= defXRP)
                        return defXRP;
                    return a + XRP(1000);
                }
                auto const defIOU = STAmount{a.issue(), 30000};
                if (a <= defIOU)
                    return defIOU;
                return a + STAmount{a.issue(), 1000};
            };
            auto const toFund1 = tofund(asset1);
            auto const toFund2 = tofund(asset2);
            BEAST_EXPECT(asset1 <= toFund1 && asset2 <= toFund2);

            if (!asset1.native() && !asset2.native())
                fund(env, gw, {alice, carol}, {toFund1, toFund2}, Fund::All);
            else if (asset1.native())
                fund(env, gw, {alice, carol}, toFund1, {toFund2}, Fund::All);
            else if (asset2.native())
                fund(env, gw, {alice, carol}, toFund2, {toFund1}, Fund::All);

            AMM ammAlice(
                env, alice, asset1, asset2, CreateArg{.log = false, .tfee = 0});

            Account const owner{"owner"};
            env.fund(XRP(1000000), owner);

            Vault vault{env};
            auto [tx, k] =
                vault.create({.owner = owner, .asset = ammAlice.lptIssue()});
            env(tx, ter{tecWRONG_ASSET});
            env.close();
        }
    }

    void
    testCreateFailMPT()
    {
        using namespace test::jtx;
        Env env{*this, supported_amendments() | featureSingleAssetVault};
        Account issuer{"issuer"};
        Account owner{"owner"};
        Account depositor{"depositor"};
        env.fund(XRP(1000), issuer, owner, depositor);
        env.close();
        Vault vault{env};

        MPTTester mptt{env, issuer, mptInitNoFund};

        // Locked because that is the default flag.
        mptt.create();
        Asset asset = mptt.issuanceID();
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx, ter(tecNO_AUTH));
    }

    void
    testNonTransferableShares()
    {
        using namespace test::jtx;
        Env env{*this, supported_amendments() | featureSingleAssetVault};
        Account issuer{"issuer"};
        Account owner{"owner"};
        Account depositor{"depositor"};
        env.fund(XRP(1000), issuer, owner, depositor);
        env.close();

        Vault vault{env};
        PrettyAsset asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(100)));
        env.trust(asset(1000), depositor);
        env(pay(issuer, depositor, asset(100)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        tx[sfFlags] = tfVaultShareNonTransferable;
        env(tx);
        env.close();

        {
            testcase("nontransferable deposits");
            auto tx1 = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(40)});
            env(tx1);

            auto tx2 = vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(60)});
            env(tx2);
            env.close();
        }

        auto const vaultAccount =  //
            [&env, key = keylet.key, this]() -> AccountID {
            auto jvVault = env.rpc("vault_info", strHex(key));

            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][sfAssetsTotal] == "100");
            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][jss::shares]
                       [sfOutstandingAmount] == "100");

            // Vault pseudo-account
            return parseBase58<AccountID>(
                       jvVault[jss::result][jss::vault][jss::Account]
                           .asString())
                .value();
        }();

        auto const MptID = makeMptID(1, vaultAccount);
        Asset shares = MptID;

        {
            testcase("nontransferable shares cannot be moved");
            env(pay(owner, depositor, shares(10)), ter{tecNO_AUTH});
            env(pay(depositor, owner, shares(10)), ter{tecNO_AUTH});
        }

        {
            testcase("nontransferable shares can be used to withdraw");
            auto tx1 = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(20)});
            env(tx1);

            auto tx2 = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(30)});
            env(tx2);
            env.close();
        }

        {
            testcase("nontransferable shares balance check");
            auto jvVault = env.rpc("vault_info", strHex(keylet.key));
            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][sfAssetsTotal] == "50");
            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][jss::shares]
                       [sfOutstandingAmount] == "50");
        }

        {
            testcase("nontransferable shares withdraw rest");
            auto tx1 = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(20)});
            env(tx1);

            auto tx2 = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(30)});
            env(tx2);
            env.close();
        }

        {
            testcase("nontransferable shares delete empty vault");
            auto tx = vault.del({.owner = owner, .id = keylet.key});
            env(tx);
            BEAST_EXPECT(!env.le(keylet));
        }
    }

    void
    testWithMPT()
    {
        using namespace test::jtx;

        auto testCase = [this](std::function<void(
                                   Env & env,
                                   Account const& issuer,
                                   Account const& owner,
                                   Account const& depositor,
                                   Asset const& asset,
                                   Vault& vault,
                                   MPTTester& mptt)> test) {
            Env env{*this, supported_amendments() | featureSingleAssetVault};
            Account issuer{"issuer"};
            Account owner{"owner"};
            Account depositor{"depositor"};
            env.fund(XRP(1000), issuer, owner, depositor);
            env.close();
            Vault vault{env};

            MPTTester mptt{env, issuer, mptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock | lsfMPTCanClawback |
                     tfMPTRequireAuth});
            PrettyAsset asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = issuer, .holder = owner});
            mptt.authorize({.account = depositor});
            mptt.authorize({.account = issuer, .holder = depositor});
            env(pay(issuer, depositor, asset(1000)));
            env.close();

            test(env, issuer, owner, depositor, asset, vault, mptt);
        };

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("nothing to clawback from");
            auto tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet::skip().key,
                 .holder = depositor,
                 .amount = asset(10)});
            env(tx, ter(tecNO_ENTRY));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("global lock blocks create");
            mptt.set({.account = issuer, .flags = tfMPTLock});
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, ter(tecFROZEN));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("global lock blocks withdrawal");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx);
            env.close();

            // Check that the OutstandingAmount field of MPTIssuance
            // accounts for the issued shares.
            auto v = env.le(keylet);
            BEAST_EXPECT(v);
            MPTID share = (*v)[sfShareMPTID];
            auto issuance = env.le(keylet::mptIssuance(share));
            BEAST_EXPECT(issuance);
            Number outstandingShares = issuance->at(sfOutstandingAmount);
            BEAST_EXPECT(outstandingShares == 100);

            mptt.set({.account = issuer, .flags = tfMPTLock});
            tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx, ter(tecLOCKED));

            tx[sfDestination] = issuer.human();
            env(tx, ter(tecLOCKED));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT un-authorization blocks withdrawal");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(1000)});
            env(tx);
            env.close();

            mptt.authorize(
                {.account = issuer,
                 .holder = depositor,
                 .flags = tfMPTUnauthorize});
            tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx, ter(tecNO_AUTH));

            // Withdrawal to other (authorized) accounts works
            tx[sfDestination] = issuer.human();
            env(tx);
            tx[sfDestination] = owner.human();
            env(tx);

            // Clawback works
            tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = depositor,
                 .amount = asset(800)});
            env(tx);

            // Can delete empty vault
            tx = vault.del({.owner = owner, .id = keylet.key});
            env(tx);
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT lock of vault pseudo-account");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const vaultAccount =
                [&env, keylet = keylet, this]() -> AccountID {
                auto const vault = env.le(keylet);
                BEAST_EXPECT(vault != nullptr);
                return vault->at(sfAccount);
            }();

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx);
            env.close();

            tx = [&]() {
                Json::Value jv;
                jv[jss::Account] = issuer.human();
                jv[sfMPTokenIssuanceID] =
                    to_string(asset.get<MPTIssue>().getMptID());
                jv[jss::Holder] = toBase58(vaultAccount);
                jv[jss::TransactionType] = jss::MPTokenIssuanceSet;
                jv[jss::Flags] = tfMPTLock;
                return jv;
            }();
            env(tx);
            env.close();

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx, ter(tecLOCKED));

            tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx, ter(tecLOCKED));

            // Clawback works, even when locked
            tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = depositor,
                 .amount = asset(100)});
            env(tx);

            // Cannot delete an empty vault, because its shares are
            // (transitively, by asset) locked.
            tx = vault.del({.owner = owner, .id = keylet.key});
            env(tx, ter{tecNO_PERMISSION});
        });

        {
            testcase("MPT shares to a vault");

            Env env{*this, supported_amendments() | featureSingleAssetVault};
            Account owner{"owner"};
            Account issuer{"issuer"};
            env.fund(XRP(1000000), owner, issuer);
            env.close();
            Vault vault{env};

            MPTTester mptt{env, issuer, mptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock | lsfMPTCanClawback |
                     tfMPTRequireAuth});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = issuer, .holder = owner});
            PrettyAsset asset = mptt.issuanceID();
            env(pay(issuer, owner, asset(100)));
            auto [tx1, k1] = vault.create({.owner = owner, .asset = asset});
            env(tx1);
            env.close();

            auto const shares = [&env, keylet = k1, this]() -> Asset {
                auto const vault = env.le(keylet);
                BEAST_EXPECT(vault != nullptr);
                return MPTIssue(vault->at(sfShareMPTID));
            }();

            auto [tx2, k2] = vault.create({.owner = owner, .asset = shares});
            env(tx2, ter{tecWRONG_ASSET});
            env.close();
        }
    }

    void
    testWithDomainCheck()
    {
        testcase("private vault");

        Env env{*this, supported_amendments() | featureSingleAssetVault};
        Account issuer{"issuer"};
        Account owner{"owner"};
        Account depositor{"depositor"};
        Account charlie{"charlie"};
        Account pdOwner{"pdOwner"};
        Account credIssuer1{"credIssuer1"};
        Account credIssuer2{"credIssuer2"};
        std::string const credType = "credential";
        Vault vault{env};
        env.fund(
            XRP(1000),
            issuer,
            owner,
            depositor,
            charlie,
            pdOwner,
            credIssuer1,
            credIssuer2);
        env.close();
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(issuer, asfAllowTrustLineClawback));

        PrettyAsset asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(500)));
        env.trust(asset(1000), depositor);
        env(pay(issuer, depositor, asset(500)));
        env.close();

        auto [tx, keylet] = vault.create(
            {.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();
        BEAST_EXPECT(env.le(keylet));

        {
            testcase("private vault owner can deposit");
            auto tx = vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx);
        }

        {
            testcase("private vault depositor not authorized yet");
            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx, ter{tecNO_AUTH});
        }

        {
            testcase("private vault cannot set non-existing domain");
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = to_string(base_uint<256>(42ul));
            env(tx, ter{tecOBJECT_NOT_FOUND});
        }

        {
            testcase("private vault set domainId");

            {
                pdomain::Credentials const credentials1{
                    {.issuer = credIssuer1, .credType = credType}};

                env(pdomain::setTx(pdOwner, credentials1));
                auto const domainId1 = [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::none);
                    return pdomain::getNewDomain(env.meta());
                }();

                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(domainId1);
                env(tx);
                env.close();

                // Update domain second time, should be harmless
                env(tx);
                env.close();
            }

            {
                pdomain::Credentials const credentials{
                    {.issuer = credIssuer1, .credType = credType},
                    {.issuer = credIssuer2, .credType = credType}};

                env(pdomain::setTx(pdOwner, credentials));
                auto const domainId = [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::none);
                    return pdomain::getNewDomain(env.meta());
                }();

                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(domainId);
                env(tx);
                env.close();
            }
        }

        {
            testcase("private vault depositor still not authorized");
            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx, ter{tecNO_AUTH});
            env.close();
        }

        auto const credKeylet =
            credentials::keylet(depositor, credIssuer1, credType);
        {
            testcase("private vault depositor now authorized");
            env(credentials::create(depositor, credIssuer1, credType));
            env(credentials::accept(depositor, credIssuer1, credType));
            env(credentials::create(charlie, credIssuer1, credType));
            env(credentials::accept(charlie, credIssuer1, credType));
            env.close();
            auto credSle = env.le(credKeylet);
            BEAST_EXPECT(credSle != nullptr);

            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx);
            env.close();

            tx = vault.deposit(
                {.depositor = charlie, .id = keylet.key, .amount = asset(50)});
            env(tx, ter{tecINSUFFICIENT_FUNDS});
            env.close();
        }

        {
            testcase("private vault depositor lost authorization");
            env(credentials::deleteCred(
                credIssuer1, depositor, credIssuer1, credType));
            env.close();
            auto credSle = env.le(credKeylet);
            BEAST_EXPECT(credSle == nullptr);

            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx, ter{tecNO_AUTH});
            env.close();
        }

        {
            testcase("private vault depositor new authorization");
            env(credentials::create(depositor, credIssuer2, credType));
            env(credentials::accept(depositor, credIssuer2, credType));
            env.close();

            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx);
            env.close();
        }

        {
            testcase("private vault reset domainId");
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = "0";
            env(tx);
            env.close();

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx, ter{tecNO_AUTH});
            env.close();

            tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx);
        }
    }

    void
    testWithDomainCheckXRP()
    {
        testcase("private XRP vault");

        Env env{*this, supported_amendments() | featureSingleAssetVault};
        Account owner{"owner"};
        Account depositor{"depositor"};
        Account alice{"charlie"};
        std::string const credType = "credential";
        Vault vault{env};
        env.fund(XRP(100000), owner, depositor, alice);
        env.close();

        PrettyAsset asset = xrpIssue();
        auto [tx, keylet] = vault.create(
            {.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();

        auto const [vaultAccount, issuanceId] =
            [&env, keylet = keylet, this]() -> std::tuple<AccountID, uint192> {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return {vault->at(sfAccount), vault->at(sfShareMPTID)};
        }();
        BEAST_EXPECT(env.le(keylet::account(vaultAccount)));
        BEAST_EXPECT(env.le(keylet::mptIssuance(issuanceId)));
        PrettyAsset shares{issuanceId};

        {
            testcase("private XRP vault owner can deposit");
            auto tx = vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx);
        }

        {
            testcase("private XRP vault cannot pay shares to depositor yet");
            env(pay(owner, depositor, shares(1)), ter{tecNO_AUTH});
        }

        {
            testcase("private XRP vault depositor not authorized yet");
            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx, ter{tecNO_AUTH});
        }

        {
            testcase("private XRP vault set DomainID");
            pdomain::Credentials const credentials{
                {.issuer = owner, .credType = credType}};

            env(pdomain::setTx(owner, credentials));
            auto const domainId = [&]() {
                auto tx = env.tx()->getJson(JsonOptions::none);
                return pdomain::getNewDomain(env.meta());
            }();

            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = to_string(domainId);
            env(tx);
            env.close();
        }

        auto const credKeylet = credentials::keylet(depositor, owner, credType);
        {
            testcase("private XRP vault depositor now authorized");
            env(credentials::create(depositor, owner, credType));
            env(credentials::accept(depositor, owner, credType));
            env.close();

            BEAST_EXPECT(env.le(credKeylet));
            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx);
            env.close();
        }

        {
            testcase("private XRP vault can pay shares to depositor");
            env(pay(owner, depositor, shares(1)));
        }

        {
            testcase("private XRP vault cannot pay shares to 3rd party");
            Json::Value jv;
            jv[sfAccount] = alice.human();
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfMPTokenIssuanceID] = to_string(issuanceId);
            env(jv);
            env.close();

            env(pay(owner, alice, shares(1)), ter{tecNO_AUTH});
        }
    }

    void
    testWithIOU()
    {
        testcase("IOU");
        Env env{*this, supported_amendments() | featureSingleAssetVault};
        Account const owner{"owner"};
        Account const issuer{"issuer"};
        Account const charlie{"charlie"};
        Vault vault{env};
        env.fund(XRP(1000), issuer, owner, charlie);
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(200)));
        env(rate(issuer, 1.25));
        env.close();
        auto const issue = asset.raw().get<Issue>();

        auto const [tx, keylet] =
            vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        auto const [vaultAccount, issuanceId] =
            [&env, keylet = keylet, this]() -> std::tuple<AccountID, uint192> {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return {vault->at(sfAccount), vault->at(sfShareMPTID)};
        }();

        auto const share = [&env, keylet = keylet, this]() -> Asset {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return MPTIssue(vault->at(sfShareMPTID));
        }();

        auto const vaultBalance =  //
            [&, account = vaultAccount, this]() -> PrettyAmount {
            auto const sle = env.le(keylet::line(account, issue));
            BEAST_EXPECT(sle != nullptr);
            auto amount = sle->getFieldAmount(sfBalance);
            amount.setIssuer(issue.account);
            if (account > issue.account)
                amount.negate();
            return {amount, env.lookup(issue.account).name()};
        };
        BEAST_EXPECT(vaultBalance() == asset(0));

        {
            testcase("IOU cannot update random trustline");
            PrettyAsset const foo = issuer["FOO"];

            auto tx = [&]() {
                Json::Value jv;
                jv[jss::Account] = issuer.human();
                {
                    auto& ja = jv[jss::LimitAmount] =
                        foo(0).value().getJson(JsonOptions::none);
                    ja[jss::issuer] = toBase58(vaultAccount);
                }
                jv[jss::TransactionType] = jss::TrustSet;
                jv[jss::Flags] = tfSetFreeze;
                return jv;
            }();
            env(tx, ter{tecNO_PERMISSION});
            env.close();
        }

        {
            testcase("IOU cannot deposit when frozen");

            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            auto tx0 = [&]() {
                Json::Value jv;
                jv[jss::Account] = issuer.human();
                {
                    auto& ja = jv[jss::LimitAmount] =
                        asset(0).value().getJson(JsonOptions::none);
                    ja[jss::issuer] = toBase58(vaultAccount);
                }
                jv[jss::TransactionType] = jss::TrustSet;
                jv[jss::Flags] = tfSetFreeze;
                return jv;
            }();
            env(tx0);
            env.close();

            // Note, the "frozen" state of the trust line is reported as
            // "locked" state of the vault shares, because this state is
            // attached to shares by means of the transitive isFrozen check.
            env(vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(100)}),
                ter{tecLOCKED});
            env.close();

            // Clawback works, even when locked
            auto tx1 = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = owner,
                 .amount = asset(0)});
            env(tx1);
            env.close();

            // Cannot delete an empty vault, because its shares are
            // (transitively, by asset) locked.
            auto tx2 = vault.del({.owner = owner, .id = keylet.key});
            env(tx2, ter{tecNO_PERMISSION});
            env.close();

            tx0[jss::Flags] = tfClearFreeze;
            env(tx0);
            env.close();

            env(pay(issuer, owner, asset(100)));
            env.close();
        }

        {
            testcase("IOU zero fee on deposit");
            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            BEAST_EXPECT(env.balance(owner, issue) == asset(100));
            BEAST_EXPECT(vaultBalance() == asset(100));
        }

        {
            testcase("IOU zero fee on withdraw");
            env(vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(60)}));
            env.close();

            BEAST_EXPECT(env.balance(owner, issue) == asset(160));
            BEAST_EXPECT(vaultBalance() == asset(40));
        }

        {
            testcase("IOU zero fee on withdraw for 3rd party");
            auto tx = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(20)});
            tx[sfDestination] = charlie.human();
            env(tx);
            env.close();

            BEAST_EXPECT(env.balance(owner, issue) == asset(160));
            BEAST_EXPECT(env.balance(charlie, issue) == asset(20));
            BEAST_EXPECT(vaultBalance() == asset(20));
        }

        {
            testcase("IOU froze trust line, cannot withdraw to 3rd party");
            auto tx1 = test::jtx::pay(owner, charlie, STAmount{share, 10});
            env(tx1, ter{tecNO_AUTH});
            auto tx2 = test::jtx::pay(charlie, owner, STAmount{share, 10});
            env(tx2, ter{tecNO_AUTH});
            env.close();

            env(trust(issuer, asset(0), owner, tfSetFreeze));
            env.close();

            // Since the vault is public, Charlie can simply create MPToken
            // to gain authorization to receive its shares.
            Json::Value jv;
            jv[sfAccount] = charlie.human();
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfMPTokenIssuanceID] = to_string(issuanceId);
            env(jv);
            env.close();

            auto tx = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(10)});
            env(tx, ter{tecFROZEN});

            tx[sfDestination] = charlie.human();
            env(tx, ter{tecLOCKED});  // owner transitively locked via MPToken
            env(tx1, ter{tecLOCKED});
            env(tx2, ter{tecLOCKED});
            env.close();

            BEAST_EXPECT(env.balance(charlie, issue) == asset(20));
        }

        {
            testcase("IOU unfroze trust line, can withdraw or pay");
            env(trust(issuer, asset(500), owner, tfClearFreeze));
            env.close();

            auto tx = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(1)});
            env(tx);

            tx[sfDestination] = charlie.human();
            env(tx);

            auto tx1 = test::jtx::pay(owner, charlie, STAmount{share, 1});
            env(tx1);

            auto tx2 = test::jtx::pay(charlie, owner, STAmount{share, 1});
            env(tx2);
        }

        {
            testcase("IOU global freeze");
            env(fset(issuer, asfGlobalFreeze));
            env.close();

            auto tx = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(1)});
            env(tx, ter{tecFROZEN});

            tx[sfDestination] = issuer.human();
            env(tx, ter{tecFROZEN});

            auto tx1 = test::jtx::pay(owner, charlie, STAmount{share, 10});
            env(tx1, ter{tecLOCKED});
            auto tx2 = test::jtx::pay(charlie, owner, STAmount{share, 10});
            env(tx2, ter{tecLOCKED});
            env.close();

            // Clawback is permitted
            auto tx3 = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = owner,
                 .amount = asset(0)});
            env(tx3);
            env.close();

            // Cannot delete an empty vault, because its shares are
            // (transitively, by asset) locked.
            auto tx4 = vault.del({.owner = owner, .id = keylet.key});
            env(tx4, ter{tecNO_PERMISSION});
            env.close();
        }
    }

    void
    testFailedPseudoAccount()
    {
        using namespace test::jtx;

        testcase("failed pseudo-account allocation");
        Env env{*this, supported_amendments() | featureSingleAssetVault};
        Account const owner{"owner"};
        Vault vault{env};
        env.fund(XRP(1000), owner);

        auto const keylet = keylet::vault(owner.id(), env.seq(owner));
        for (int i = 0; i < 256; ++i)
        {
            AccountID const accountId =
                ripple::pseudoAccountAddress(*env.current(), keylet.key);

            env(pay(env.master.id(), accountId, XRP(1000)),
                seq(autofill),
                fee(autofill),
                sig(autofill));
        }

        auto [tx, keylet1] =
            vault.create({.owner = owner, .asset = xrpIssue()});
        BEAST_EXPECT(keylet.key == keylet1.key);
        env(tx, ter{terADDRESS_COLLISION});
    }

    void
    testRPC()
    {
        testcase("RPC");
        Env env{*this, supported_amendments() | featureSingleAssetVault};
        Account const owner{"owner"};
        Account const issuer{"issuer"};
        Vault vault{env};
        env.fund(XRP(1000), issuer, owner);
        env.close();

        PrettyAsset asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(200)));
        env.close();

        auto const sequence = env.seq(owner);
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        // Set some fields
        {
            auto tx1 = vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx1);

            auto tx2 = vault.set({.owner = owner, .id = keylet.key});
            tx2[sfAssetsMaximum] = asset(1000).number();
            env(tx2);
            env.close();
        }

        auto const sleVault = [&env, keylet = keylet, this]() {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return vault;
        }();

        auto const check = [&, keylet = keylet, sle = sleVault, this](
                               Json::Value const& vault,
                               Json::Value const& issuance = Json::nullValue) {
            BEAST_EXPECT(vault.isObject());

            constexpr auto checkString =
                [](auto& node, SField const& field, std::string v) -> bool {
                return node.isMember(field.fieldName) &&
                    node[field.fieldName].isString() &&
                    node[field.fieldName] == v;
            };
            constexpr auto checkObject =
                [](auto& node, SField const& field, Json::Value v) -> bool {
                return node.isMember(field.fieldName) &&
                    node[field.fieldName].isObject() &&
                    node[field.fieldName] == v;
            };
            constexpr auto checkInt =
                [](auto& node, SField const& field, int v) -> bool {
                return node.isMember(field.fieldName) &&
                    ((node[field.fieldName].isInt() &&
                      node[field.fieldName] == Json::Int(v)) ||
                     (node[field.fieldName].isUInt() &&
                      node[field.fieldName] == Json::UInt(v)));
            };

            BEAST_EXPECT(vault["LedgerEntryType"].asString() == "Vault");
            BEAST_EXPECT(vault[jss::index].asString() == strHex(keylet.key));
            BEAST_EXPECT(checkInt(vault, sfFlags, 0));
            // Ignore all other standard fields, this test doesn't care

            BEAST_EXPECT(
                checkString(vault, sfAccount, toBase58(sle->at(sfAccount))));
            BEAST_EXPECT(
                checkObject(vault, sfAsset, to_json(sle->at(sfAsset))));
            BEAST_EXPECT(checkString(vault, sfAssetsAvailable, "50"));
            BEAST_EXPECT(checkString(vault, sfAssetsMaximum, "1000"));
            BEAST_EXPECT(checkString(vault, sfAssetsTotal, "50"));
            BEAST_EXPECT(checkString(vault, sfLossUnrealized, "0"));

            auto const strShareID = strHex(sle->at(sfShareMPTID));
            BEAST_EXPECT(checkString(vault, sfShareMPTID, strShareID));
            BEAST_EXPECT(checkString(vault, sfOwner, toBase58(owner.id())));
            BEAST_EXPECT(checkInt(vault, sfSequence, sequence));
            BEAST_EXPECT(checkInt(
                vault, sfWithdrawalPolicy, vaultStrategyFirstComeFirstServe));

            if (issuance.isObject())
            {
                BEAST_EXPECT(
                    issuance["LedgerEntryType"].asString() ==
                    "MPTokenIssuance");
                BEAST_EXPECT(
                    issuance[jss::mpt_issuance_id].asString() == strShareID);
                BEAST_EXPECT(checkInt(issuance, sfSequence, 1));
                BEAST_EXPECT(checkInt(
                    issuance,
                    sfFlags,
                    int(lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer)));
                BEAST_EXPECT(checkString(issuance, sfOutstandingAmount, "50"));
            }
        };

        {
            testcase("RPC ledger_entry selected by key");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = strHex(keylet.key);
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(!jvVault[jss::result].isMember(jss::error));
            BEAST_EXPECT(jvVault[jss::result].isMember(jss::node));
            check(jvVault[jss::result][jss::node]);
        }

        {
            testcase("RPC ledger_entry selected by owner and seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = owner.human();
            jvParams[jss::vault][jss::seq] = sequence;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(!jvVault[jss::result].isMember(jss::error));
            BEAST_EXPECT(jvVault[jss::result].isMember(jss::node));
            check(jvVault[jss::result][jss::node]);
        }

        {
            testcase("RPC ledger_entry cannot find vault by key");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = to_string(uint256(42));
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC ledger_entry cannot find vault by owner and seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = 1'000'000;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC ledger_entry malformed key");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = 42;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() ==
                "malformedRequest");
        }

        {
            testcase("RPC ledger_entry malformed owner");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = 42;
            jvParams[jss::vault][jss::seq] = sequence;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() ==
                "malformedOwner");
        }

        {
            testcase("RPC ledger_entry malformed seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = "foo";
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() ==
                "malformedRequest");
        }

        {
            testcase("RPC ledger_entry zero seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = 0;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() ==
                "malformedRequest");
        }

        {
            testcase("RPC ledger_entry negative seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = -1;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() ==
                "malformedRequest");
        }

        {
            testcase("RPC ledger_entry oversized seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = 1e20;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(
                jvVault[jss::result][jss::error].asString() ==
                "malformedRequest");
        }

        {
            testcase("RPC account_objects");

            Json::Value jvParams;
            jvParams[jss::account] = owner.human();
            jvParams[jss::type] = jss::vault;
            auto jv = env.rpc(
                "json", "account_objects", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jv[jss::account_objects].size() == 1);
            check(jv[jss::account_objects][0u]);
        }

        {
            testcase("RPC ledger_data");

            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::binary] = false;
            jvParams[jss::type] = jss::vault;
            Json::Value jv =
                env.rpc("json", "ledger_data", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::state].size() == 1);
            check(jv[jss::result][jss::state][0u]);
        }

        {
            testcase("RPC vault_info command line");
            Json::Value jv =
                env.rpc("vault_info", strHex(keylet.key), "validated");

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(
                jv[jss::result][jss::vault],
                jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info json");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(
                jv[jss::result][jss::vault],
                jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info json invalid index");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = 0;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json by owner and sequence");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(
                jv[jss::result][jss::vault],
                jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info json malformed sequence");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = "foobar";
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid sequence");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = 0;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json negative sequence");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = -1;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json oversized sequence");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = 1e20;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json malformed owner");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = "foobar";
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination only owner");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination only seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination seq vault_id");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination owner vault_id");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase(
                "RPC vault_info json invalid combination owner seq vault_id");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::seq] = sequence;
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json no input");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info command line invalid index");
            Json::Value jv = env.rpc("vault_info", "foobar", "validated");
            BEAST_EXPECT(jv[jss::error].asString() == "invalidParams");
        }

        {
            testcase("RPC vault_info command line invalid index");
            Json::Value jv = env.rpc("vault_info", "0", "validated");
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info command line invalid index");
            Json::Value jv =
                env.rpc("vault_info", strHex(uint256(42)), "validated");
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC vault_info command line invalid ledger");
            Json::Value jv = env.rpc("vault_info", strHex(keylet.key), "0");
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "lgrNotFound");
        }
    }

public:
    void
    run() override
    {
        testSequences();
        testPreflight();
        testCreateFailXRP();
        testCreateFailIOU();
        testCreateFailMPT();
        testWithMPT();
        testWithIOU();
        testWithDomainCheck();
        testWithDomainCheckXRP();
        testNonTransferableShares();
        testFailedPseudoAccount();
        testRPC();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, tx, ripple, 1);

}  // namespace ripple

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

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

using namespace test::jtx;

class Vault_test : public beast::unit_test::suite
{
    void
    testSequences()
    {
        using namespace test::jtx;

        static auto constexpr negativeAmount =
            [](PrettyAsset const& asset) -> PrettyAmount {
            return {
                STAmount{asset.raw(), 1ul, 0, true, STAmount::unchecked{}}, ""};
        };

        auto const testSequence = [this](
                                      std::string const& prefix,
                                      Env& env,
                                      Account const& issuer,
                                      Account const& owner,
                                      Account const& depositor,
                                      Vault& vault,
                                      PrettyAsset const& asset) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            BEAST_EXPECT(env.le(keylet));

            {
                testcase(prefix + " fail to set negative maximum");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetMaximum] = negativeAmount(asset).number();
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
                tx[sfAssetMaximum] = asset(50).number();
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
                tx[sfAssetMaximum] = asset(50).number();
                env(tx, ter(tecLIMIT_EXCEEDED));
            }

            {
                testcase(prefix + " set maximum higher than current amount");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetMaximum] = asset(150).number();
                env(tx);
            }

            {
                testcase(prefix + " fail to set zero domain");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(base_uint<256>(beast::zero));
                env(tx, ter(temMALFORMED));
            }

            {
                testcase(prefix + " fail to set nonexistent domain");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(base_uint<256>(42ul));
                env(tx, ter(tecINVALID_DOMAIN));
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
                tx[sfAssetMaximum] = asset(0).number();
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
                testcase(prefix + " withdraw non-zero assets");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(200)});
                env(tx);
            }

            {
                testcase(prefix + " fail to delete because wrong owner");
                auto tx = vault.del({.owner = issuer, .id = keylet.key});
                env(tx, ter(tecNO_PERMISSION));
            }

            {
                testcase(prefix + " delete empty vault");
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx);
                BEAST_EXPECT(!env.le(keylet));
            }
        };

        auto testCases =
            [this, &testSequence](
                std::string prefix,
                std::function<PrettyAsset(
                    Env & env, Account const& issuer, Account const& depositor)>
                    setup) {
                Env env{*this};
                Account issuer{"issuer"};
                Account owner{"owner"};
                Account depositor{"depositor"};
                auto vault = env.vault();
                env.fund(XRP(1000), issuer, owner, depositor);
                env.close();
                env(fset(issuer, asfAllowTrustLineClawback));
                env.close();
                env.require(flags(issuer, asfAllowTrustLineClawback));

                PrettyAsset asset = setup(env, issuer, depositor);
                testSequence(
                    prefix, env, issuer, owner, depositor, vault, asset);
            };

        testCases(
            "XRP",
            [](Env& env, Account const& issuer, Account const& depositor)
                -> PrettyAsset { return {xrpIssue(), 1'000'000}; });

        testCases(
            "IOU",
            [](Env& env,
               Account const& issuer,
               Account const& depositor) -> Asset {
                PrettyAsset asset = issuer["IOU"];
                env.trust(asset(1000), depositor);
                env(pay(issuer, depositor, asset(1000)));
                env.close();
                return asset;
            });

        testCases(
            "MPT",
            [](Env& env,
               Account const& issuer,
               Account const& depositor) -> Asset {
                MPTTester mptt{env, issuer, mptInitNoFund};
                mptt.create(
                    {.flags =
                         tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
                PrettyAsset asset = mptt.issuanceID();
                mptt.authorize({.account = depositor});
                env(pay(issuer, depositor, asset(1000)));
                env.close();
                return asset;
            });
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
            Env env{*this};
            Account issuer{"issuer"};
            Account owner{"owner"};
            Account depositor{"depositor"};
            env.fund(XRP(1000), issuer, owner, depositor);
            env.close();
            auto vault = env.vault();
            Asset asset = xrpIssue();

            test(env, issuer, owner, depositor, asset, vault);
        };

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("nothing to delete");
            auto tx = vault.del({.owner = issuer, .id = keylet::skip().key});
            env(tx, ter(tecOBJECT_NOT_FOUND));
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
    }

    void
    testCreateFailIOU()
    {
        using namespace test::jtx;
        Env env{*this};
        Account issuer{"issuer"};
        Account owner{"owner"};
        Account depositor{"depositor"};
        env.fund(XRP(1000), issuer, owner, depositor);
        env.close();
        auto vault = env.vault();
        Asset asset = issuer["IOU"];

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

        env(fset(issuer, asfGlobalFreeze));
        env.close();
        env(tx, ter(tecFROZEN));
        env.close();
    }

    void
    testCreateFailMPT()
    {
        using namespace test::jtx;
        Env env{*this};
        Account issuer{"issuer"};
        Account owner{"owner"};
        Account depositor{"depositor"};
        env.fund(XRP(1000), issuer, owner, depositor);
        env.close();
        auto vault = env.vault();

        MPTTester mptt{env, issuer, mptInitNoFund};

        // Locked because that is the default flag.
        mptt.create();
        Asset asset = mptt.issuanceID();
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx, ter(tecNO_AUTH));
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
            Env env{*this};
            Account issuer{"issuer"};
            Account owner{"owner"};
            Account depositor{"depositor"};
            env.fund(XRP(1000), issuer, owner, depositor);
            env.close();
            auto vault = env.vault();

            MPTTester mptt{env, issuer, mptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset asset = mptt.issuanceID();
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1000)));
            env.close();

            test(env, issuer, owner, depositor, asset, vault, mptt);
        };

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("global lock");
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
            testcase("deposit non-zero amount");
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
            MPTID share = (*v)[sfMPTokenIssuanceID];
            auto issuance = env.le(keylet::mptIssuance(share));
            BEAST_EXPECT(issuance);
            Number outstandingShares = issuance->at(sfOutstandingAmount);
            BEAST_EXPECT(outstandingShares > 0);
            BEAST_EXPECT(outstandingShares == 100);
        });
    }

    void
    testWithDomainCheck()
    {
        testcase("private vault");

        Env env{*this};
        Account issuer{"issuer"};
        Account owner{"owner"};
        Account depositor{"depositor"};
        Account pdOwner{"pdOwner"};
        Account credIssuer1{"credIssuer1"};
        Account credIssuer2{"credIssuer2"};
        std::string const credType = "credential";
        auto vault = env.vault();
        env.fund(
            XRP(1000),
            issuer,
            owner,
            depositor,
            pdOwner,
            credIssuer1,
            credIssuer2);
        env.close();
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(issuer, asfAllowTrustLineClawback));

        PrettyAsset asset{xrpIssue(), 1'000'000};
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
            env.close();
            auto credSle = env.le(credKeylet);
            BEAST_EXPECT(credSle != nullptr);

            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx);
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
            testcase("private vault no authorization needed to withdraw");
            env(credentials::deleteCred(
                depositor, depositor, credIssuer2, credType));
            env.close();

            auto tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx);
        }

        {
            testcase("private vault depositor not authorized");
            auto tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx, ter{tecNO_AUTH});
            env.close();
        }
    }

    void
    testWithIOU()
    {
        testcase("IOU fees");
        Env env{*this};
        Account const owner{"owner"};
        Account const issuer{"issuer"};
        Account const charlie{"charlie"};
        auto vault = env.vault();
        env.fund(XRP(1000), issuer, owner, charlie);
        env.close();

        PrettyAsset asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(200)));
        env(rate(issuer, 1.25));
        env.close();
        auto const issue = asset.raw().get<Issue>();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        auto const [vaultAccount, ownerAccount, mptID] =  //
            [&env, &owner, key = keylet.key, this]()      //
            -> std::tuple<AccountID, AccountID, uint192> {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = strHex(key);
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));

            // Vault pseudo-account
            auto const vAcct =
                parseBase58<AccountID>(
                    jvVault[jss::result][jss::node][jss::Account].asString())
                    .value();

            // Owner account
            auto const oAcct =
                parseBase58<AccountID>(
                    jvVault[jss::result][jss::node][jss::Owner].asString())
                    .value();
            BEAST_EXPECT(oAcct == owner.id());

            // MPTID of the vault shares
            auto const strMpt = jvVault[jss::result][jss::node][jss::Share]
                                       [jss::mpt_issuance_id]
                                           .asString();
            uint192 mpt;
            BEAST_EXPECT(mpt.parseHex(strMpt));

            return {vAcct, oAcct, mpt};
        }();

        BEAST_EXPECT(makeMptID(1, vaultAccount) == mptID);
        BEAST_EXPECT(
            keylet::vault(ownerAccount, env.seq(owner) - 1).key == keylet.key);

        auto const vaultBalance = [&]() -> PrettyAmount {
            auto const sle = env.le(keylet::line(vaultAccount, issue));
            BEAST_EXPECT(sle != nullptr);
            auto amount = sle->getFieldAmount(sfBalance);
            amount.setIssuer(issue.account);
            if (vaultAccount > issue.account)
                amount.negate();
            return {amount, env.lookup(issue.account).name()};
        };
        BEAST_EXPECT(vaultBalance() == asset(0));

        {
            testcase("zero IOU fee on deposit");
            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            BEAST_EXPECT(env.balance(owner, issue) == asset(100));
            BEAST_EXPECT(vaultBalance() == asset(100));
        }

        {
            testcase("zero IOU fee on withdraw");
            env(vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(60)}));
            env.close();

            BEAST_EXPECT(env.balance(owner, issue) == asset(160));
            BEAST_EXPECT(vaultBalance() == asset(40));
        }

        {
            testcase("zero IOU fee on withdraw for 3rd party");
            auto tx = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(40)});
            tx[sfDestination] = charlie.human();
            env(tx);
            env.close();

            BEAST_EXPECT(env.balance(owner, issue) == asset(160));
            BEAST_EXPECT(env.balance(charlie, issue) == asset(40));
            BEAST_EXPECT(vaultBalance() == asset(0));
        }
    }

public:
    void
    run() override
    {
        testSequences();
        testCreateFailXRP();
        testCreateFailIOU();
        testCreateFailMPT();
        testWithMPT();
        testWithIOU();
        testWithDomainCheck();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, tx, ripple, 1);

}  // namespace ripple

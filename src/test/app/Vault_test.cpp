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
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>

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
                tx[sfAssetMaximum] = asset(200).number();
                env(tx);
            }

            {
                testcase(prefix + " fail to deposit more than maximum");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(200)});
                env(tx, ter(tecLIMIT_EXCEEDED));
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
                testcase(prefix + " deposit up to maximum");
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
                env(tx, ter(tecNO_PERMISSION));
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
                auto code = asset.raw().native() ? ter(tecNO_PERMISSION)
                                                 : ter(tesSUCCESS);
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
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = depositor,
                     .amount = std::nullopt});
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
                mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
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

public:
    void
    run() override
    {
        testSequences();
        testCreateFailXRP();
        testCreateFailIOU();
        testCreateFailMPT();
        testWithMPT();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, tx, ripple, 1);

}  // namespace ripple

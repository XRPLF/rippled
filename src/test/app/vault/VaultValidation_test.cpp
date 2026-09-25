#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <functional>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultValidation_test : public VaultTestBase
{
private:
    void
    testPreflight()
    {
        using namespace test::jtx;

        struct CaseArgs
        {
            FeatureBitset features = testableAmendments();
        };

        auto testCase = [&, this](
                            std::function<void(
                                Env & env,
                                Account const& issuer,
                                Account const& owner,
                                Asset const& asset,
                                Vault& vault)> test,
                            CaseArgs args = {}) {
            Env env{*this, args.features};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Vault vault{env};
            env.fund(XRP(1000), issuer, owner);
            env.close();

            env(fset(issuer, asfAllowTrustLineClawback));
            env(fset(issuer, asfRequireAuth));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env(trust(owner, asset(1000)));
            env(trust(issuer, asset(0), owner, tfSetfAuth));
            env(pay(issuer, owner, asset(1000)));
            env.close();

            test(env, issuer, owner, asset, vault);
        };

        auto testDisabled = [&](TER resultAfterCreate = temDISABLED) {
            return [&, resultAfterCreate](
                       Env& env,
                       Account const& issuer,
                       Account const& owner,
                       Asset const& asset,
                       Vault& vault) {
                testcase("disabled single asset vault");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx, Ter{temDISABLED});

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    env(tx, kData("test"), Ter{resultAfterCreate});
                }

                {
                    auto tx =
                        vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                    env(tx, Ter{resultAfterCreate});
                }

                {
                    auto tx =
                        vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                    env(tx, Ter{resultAfterCreate});
                }

                {
                    auto tx = vault.clawback(
                        {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(10)});
                    env(tx, Ter{resultAfterCreate});
                }

                {
                    auto tx = vault.del({.owner = owner, .id = keylet.key});
                    env(tx, Ter{resultAfterCreate});
                }
            };
        };

        testCase(testDisabled(), {.features = testableAmendments() - featureSingleAssetVault});

        testCase(testDisabled(tecNO_ENTRY), {.features = testableAmendments() - featureMPTokensV1});

        testCase(
            [&](Env& env,
                Account const& issuer,
                Account const& owner,
                Asset const& asset,
                Vault& vault) {
                testcase("disabled permissioned domains");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);

                tx[sfFlags] = tx[sfFlags].asUInt() | tfVaultPrivate;
                tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
                env(tx, Ter{temDISABLED});

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    env(tx, kData("Test"));

                    tx[sfDomainID] = to_string(BaseUInt<256>(13ul));
                    env(tx, Ter{temDISABLED});
                }
            },
            {.features = testableAmendments() - featurePermissionedDomains});

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid flags");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfFlags] = tfClearDeepFreeze;
            env(tx, Ter{temINVALID_FLAG});

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, Ter{temINVALID_FLAG});
            }

            {
                auto tx =
                    vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, Ter{temINVALID_FLAG});
            }

            {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, Ter{temINVALID_FLAG});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(10)});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, Ter{temINVALID_FLAG});
            }

            {
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                tx[sfFlags] = tfClearDeepFreeze;
                env(tx, Ter{temINVALID_FLAG});
            }
        });

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid fee");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[jss::Fee] = "-1";
            env(tx, Ter{temBAD_FEE});

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[jss::Fee] = "-1";
                env(tx, Ter{temBAD_FEE});
            }

            {
                auto tx =
                    vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                tx[jss::Fee] = "-1";
                env(tx, Ter{temBAD_FEE});
            }

            {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                tx[jss::Fee] = "-1";
                env(tx, Ter{temBAD_FEE});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(10)});
                tx[jss::Fee] = "-1";
                env(tx, Ter{temBAD_FEE});
            }

            {
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                tx[jss::Fee] = "-1";
                env(tx, Ter{temBAD_FEE});
            }
        });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const&, Vault& vault) {
                testcase("disabled permissioned domain");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
                tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
                env(tx, Ter{temDISABLED});

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
                    env(tx, Ter{temDISABLED});
                }

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    tx[sfDomainID] = "0";
                    env(tx, Ter{temDISABLED});
                }
            },
            {.features = (testableAmendments()) - featurePermissionedDomains});

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("use zero vault");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});

            {
                auto tx = vault.set({
                    .owner = owner,
                    .id = beast::kZero,
                });
                env(tx, Ter{temMALFORMED});
            }

            {
                auto tx =
                    vault.deposit({.depositor = owner, .id = beast::kZero, .amount = asset(10)});
                env(tx, Ter(temMALFORMED));
            }

            {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = beast::kZero, .amount = asset(10)});
                env(tx, Ter{temMALFORMED});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = beast::kZero, .holder = owner, .amount = asset(10)});
                env(tx, Ter{temMALFORMED});
            }

            {
                auto tx = vault.del({
                    .owner = owner,
                    .id = beast::kZero,
                });
                env(tx, Ter{temMALFORMED});
            }
        });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("withdraw to bad destination");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx =
                        vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                    tx[jss::Destination] = "0";
                    env(tx, Ter{temMALFORMED});
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("create with Scale");

                {
                    auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                    tx[sfScale] = 255;
                    env(tx, Ter(temMALFORMED));
                }

                {
                    auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                    tx[sfScale] = 19;
                    env(tx, Ter(temMALFORMED));
                }

                // accepted range from 0 to 18
                {
                    auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                    tx[sfScale] = 18;
                    env(tx);
                    env.close();
                    auto const sleVault = env.le(keylet);
                    BEAST_EXPECT(sleVault);
                    BEAST_EXPECT((*sleVault)[sfScale] == 18);
                }

                {
                    auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                    tx[sfScale] = 0;
                    env(tx);
                    env.close();
                    auto const sleVault = env.le(keylet);
                    BEAST_EXPECT(sleVault);
                    BEAST_EXPECT((*sleVault)[sfScale] == 0);
                }

                {
                    auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                    env(tx);
                    env.close();
                    auto const sleVault = env.le(keylet);
                    BEAST_EXPECT(sleVault);
                    BEAST_EXPECT((*sleVault)[sfScale] == 6);
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("create or set invalid data");

                auto [tx1, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = tx1;
                    tx[sfData] = "";
                    env(tx, Ter(temMALFORMED));
                }

                {
                    auto tx = tx1;
                    // A hexadecimal string of 257 bytes.
                    tx[sfData] = std::string(514, 'A');
                    env(tx, Ter(temMALFORMED));
                }

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    tx[sfData] = "";
                    env(tx, Ter{temMALFORMED});
                }

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    // A hexadecimal string of 257 bytes.
                    tx[sfData] = std::string(514, 'A');
                    env(tx, Ter{temMALFORMED});
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("set nothing updated");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    env(tx, Ter{temMALFORMED});
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("create with invalid metadata");

                auto [tx1, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = tx1;
                    tx[sfMPTokenMetadata] = "";
                    env(tx, Ter(temMALFORMED));
                }

                {
                    auto tx = tx1;
                    // This metadata is for the share token.
                    // A hexadecimal string of 1025 bytes.
                    tx[sfMPTokenMetadata] = std::string(2050, 'B');
                    env(tx, Ter(temMALFORMED));
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("set negative maximum");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    tx[sfAssetsMaximum] = kNegativeAmount(asset).number();
                    env(tx, Ter{temMALFORMED});
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("invalid deposit amount");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = vault.deposit(
                        {.depositor = owner, .id = keylet.key, .amount = kNegativeAmount(asset)});
                    env(tx, Ter(temBAD_AMOUNT));
                }

                {
                    auto tx =
                        vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(0)});
                    env(tx, Ter(temBAD_AMOUNT));
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("invalid set immutable flag");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    tx[sfFlags] = tfVaultPrivate;
                    env(tx, Ter(temINVALID_FLAG));
                }
            });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("invalid withdraw amount");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = vault.withdraw(
                        {.depositor = owner, .id = keylet.key, .amount = kNegativeAmount(asset)});
                    env(tx, Ter(temBAD_AMOUNT));
                }

                {
                    auto tx =
                        vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(0)});
                    env(tx, Ter(temBAD_AMOUNT));
                }
            });

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid clawback");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            // Preclaim only checks for native assets.
            if (asset.native())
            {
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(50)});
                env(tx, Ter(temMALFORMED));
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = kNegativeAmount(asset)});
                env(tx, Ter(temBAD_AMOUNT));
            }
        });

        testCase(
            [&](Env& env, Account const&, Account const& owner, Asset const& asset, Vault& vault) {
                testcase("invalid create");

                auto [tx1, keylet] = vault.create({.owner = owner, .asset = asset});

                {
                    auto tx = tx1;
                    tx[sfWithdrawalPolicy] = 0;
                    env(tx, Ter(temMALFORMED));
                }

                {
                    auto tx = tx1;
                    tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
                    env(tx, Ter{temMALFORMED});
                }

                {
                    auto tx = tx1;
                    tx[sfAssetsMaximum] = kNegativeAmount(asset).number();
                    env(tx, Ter{temMALFORMED});
                }

                {
                    auto tx = tx1;
                    tx[sfFlags] = tfVaultPrivate;
                    tx[sfDomainID] = "0";
                    env(tx, Ter{temMALFORMED});
                }
            });
    }

    // Test for non-asset specific behaviors.
    void
    testCreateFailXRP()
    {
        using namespace test::jtx;

        auto testCase = [this](
                            std::function<void(
                                Env & env,
                                Account const& issuer,
                                Account const& owner,
                                Account const& depositor,
                                Asset const& asset,
                                Vault& vault)> test) {
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};

            env.fund(XRP(1000), issuer, owner, depositor);
            env.close();
            Vault vault{env};
            Asset const asset = xrpIssue();

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
            env(tx, Ter(tecNO_ENTRY));
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
                {.depositor = depositor, .id = keylet::skip().key, .amount = asset(10)});
            env(tx, Ter(tecNO_ENTRY));
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
                {.depositor = depositor, .id = keylet::skip().key, .amount = asset(10)});
            env(tx, Ter(tecNO_ENTRY));
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
            env(tx, Ter(tecNO_ENTRY));
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
            testcase("insufficient fee");
            env(tx, Fee(env.current()->fees().base - 1), Ter(telINSUF_FEE_P));
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
            env(tx, Ter(tecINSUFFICIENT_RESERVE));
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
            tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
            testcase("non-existing domain");
            env(tx, Ter{tecOBJECT_NOT_FOUND});
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("cannot set Scale=0");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = 0;
            env(tx, Ter{temMALFORMED});
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("cannot set Scale=1");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = 1;
            env(tx, Ter{temMALFORMED});
        });
    }

    void
    testCreateFailIOU()
    {
        using namespace test::jtx;
        {
            {
                testcase("IOU fail because MPT is disabled");
                Env env{*this, (testableAmendments() - featureMPTokensV1)};
                Account const issuer{"issuer"};
                Account const owner{"owner"};
                env.fund(XRP(1000), issuer, owner);
                env.close();

                Vault const vault{env};
                Asset const asset = issuer["IOU"].asset();
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                env(tx, Ter(temDISABLED));
                env.close();
            }

            {
                testcase("IOU fail create frozen");
                Env env{*this, testableAmendments()};
                Account const issuer{"issuer"};
                Account const owner{"owner"};
                env.fund(XRP(1000), issuer, owner);
                env.close();
                env(fset(issuer, asfGlobalFreeze));
                env.close();

                Vault const vault{env};
                Asset const asset = issuer["IOU"].asset();
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

                env(tx, Ter(tecFROZEN));
                env.close();
            }

            {
                testcase("IOU fail create no ripling");
                Env env{*this, testableAmendments()};
                Account const issuer{"issuer"};
                Account const owner{"owner"};
                env.fund(XRP(1000), issuer, owner);
                env.close();
                env(fclear(issuer, asfDefaultRipple));
                env.close();

                Vault const vault{env};
                Asset const asset = issuer["IOU"].asset();
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx, Ter(terNO_RIPPLE));
                env.close();
            }

            {
                testcase("IOU no issuer");
                Env env{*this, testableAmendments()};
                Account const issuer{"issuer"};
                Account const owner{"owner"};
                env.fund(XRP(1000), owner);
                env.close();

                Vault const vault{env};
                Asset const asset = issuer["IOU"].asset();
                {
                    auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                    env(tx, Ter(terNO_ACCOUNT));
                    env.close();
                }
            }
        }

        {
            testcase("IOU fail create vault for AMM LPToken");
            Env env{*this, testableAmendments()};
            Account const gw("gateway");
            Account const alice("alice");
            Account const carol("carol");
            IOU const usd = gw["USD"];

            auto const [asset1, asset2] = std::pair<STAmount, STAmount>(XRP(10000), usd(10000));
            auto toFund = [&](STAmount const& a) -> STAmount {
                if (a.native())
                {
                    auto const defXRP = XRP(30000);
                    if (a <= defXRP)
                        return defXRP;
                    return a + XRP(1000);
                }
                auto defIOU = STAmount{a.asset(), 30000};
                if (a <= defIOU)
                    return defIOU;
                return a + STAmount{a.asset(), 1000};
            };
            auto const toFund1 = toFund(asset1);
            auto const toFund2 = toFund(asset2);
            BEAST_EXPECT(asset1 <= toFund1 && asset2 <= toFund2);

            if (!asset1.native() && !asset2.native())
            {
                fund(env, gw, {alice, carol}, {toFund1, toFund2}, Fund::All);
            }
            else if (asset1.native())
            {
                fund(env, gw, {alice, carol}, toFund1, {toFund2}, Fund::All);
            }
            else if (asset2.native())
            {
                fund(env, gw, {alice, carol}, toFund2, {toFund1}, Fund::All);
            }

            AMM const ammAlice(env, alice, asset1, asset2, CreateArg{.log = false, .tfee = 0});

            Account const owner{"owner"};
            env.fund(XRP(1000000), owner);

            Vault const vault{env};
            auto [tx, k] = vault.create({.owner = owner, .asset = ammAlice.lptIssue()});
            env(tx, Ter{tecWRONG_ASSET});
            env.close();
        }
    }

    void
    testCreateFailMPT()
    {
        using namespace test::jtx;

        auto testCase = [this](
                            std::function<void(
                                Env & env,
                                Account const& issuer,
                                Account const& owner,
                                Account const& depositor,
                                Asset const& asset,
                                Vault& vault)> test) {
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            env.fund(XRP(1000), issuer, owner, depositor);
            env.close();
            Vault vault{env};
            MPTTester mptt{env, issuer, kMptInitNoFund};
            // Locked because that is the default flag.
            mptt.create();
            Asset const asset = mptt.issuanceID();

            test(env, issuer, owner, depositor, asset, vault);
        };

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("MPT no authorization");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tecNO_AUTH));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("MPT cannot set Scale=0");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = 0;
            env(tx, Ter{temMALFORMED});
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault) {
            testcase("MPT cannot set Scale=1");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = 1;
            env(tx, Ter{temMALFORMED});
        });
    }

    void
    testVaultDeleteMemoData()
    {
        using namespace test::jtx;

        Env env{*this};

        Account const owner{"owner"};
        env.fund(XRP(1'000'000), owner);
        env.close();

        Vault const vault{env};

        auto const keylet = keylet::vault(owner.id(), SeqProxy::rawSequence(1));
        auto delTx = vault.del({.owner = owner, .id = keylet.key});

        // Test VaultDelete with featureLendingProtocolV1_1 disabled
        // Transaction fails if the data field is provided
        {
            testcase("VaultDelete memo data featureLendingProtocolV1_1 disabled");
            env.disableFeature(featureLendingProtocolV1_1);
            delTx[sfMemoData] = strHex(std::string(kMaxDataPayloadLength, 'A'));
            env(delTx, Ter(temDISABLED));
            env.enableFeature(featureLendingProtocolV1_1);
            env.close();
        }

        // Transaction fails if the data field is too large
        {
            testcase("VaultDelete memo data featureLendingProtocolV1_1 enabled data too large");
            delTx[sfMemoData] = strHex(std::string(kMaxDataPayloadLength + 1, 'A'));
            env(delTx, Ter(temMALFORMED));
            env.close();
        }

        // Transaction fails if the data field is set, but is empty
        {
            testcase("VaultDelete memo data featureLendingProtocolV1_1 enabled data empty");
            delTx[sfMemoData] = strHex(std::string());
            env(delTx, Ter(temMALFORMED));
            env.close();
        }

        {
            testcase("VaultDelete memo data featureLendingProtocolV1_1 enabled no vault");
            auto const keylet = keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));

            // Recreate the transaction as the vault keylet changed
            auto delTx = vault.del({.owner = owner, .id = keylet.key});
            delTx[sfMemoData] = strHex(std::string(kMaxDataPayloadLength, 'A'));
            env(delTx, Ter(tecNO_ENTRY));
            env.close();
        }

        {
            testcase("VaultDelete memo data featureLendingProtocolV1_1 enabled data valid");
            PrettyAsset const xrpAsset = xrpIssue();
            auto const [tx, keylet] = vault.create({.owner = owner, .asset = xrpAsset});
            env(tx, Ter(tesSUCCESS));
            env.close();
            // Recreate the transaction as the vault keylet changed
            auto delTx = vault.del({.owner = owner, .id = keylet.key});
            delTx[sfMemoData] = strHex(std::string(kMaxDataPayloadLength, 'A'));
            env(delTx, Ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testVaultCreateLEVersion()
    {
        using namespace test::jtx;

        Account const owner{"owner"};
        PrettyAsset const xrpAsset = xrpIssue();

        {
            testcase("VaultCreate LEVersion: featureLendingProtocolV1_1 disabled, field absent");
            Env env{*this};
            env.disableFeature(featureLendingProtocolV1_1);
            env.fund(XRP(1'000'000), owner);
            env.close();

            Vault const vault{env};
            auto const [tx, keylet] = vault.create({.owner = owner, .asset = xrpAsset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault);
            BEAST_EXPECT(!sleVault->isFieldPresent(sfLEVersion));
        }

        {
            testcase(
                "VaultCreate LEVersion: featureLendingProtocolV1_1 enabled, LEVersion == "
                "VaultVersion::CashBasis");
            Env env{*this};
            env.fund(XRP(1'000'000), owner);
            env.close();

            Vault const vault{env};
            auto const [tx, keylet] = vault.create({.owner = owner, .asset = xrpAsset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault);
            BEAST_EXPECT(sleVault->isFieldPresent(sfLEVersion));
            BEAST_EXPECT(sleVault->at(sfLEVersion) == std::to_underlying(VaultVersion::CashBasis));
        }

        {
            testcase("VaultCreate rejects LEVersion set in the transaction");
            Env env{*this};
            env.fund(XRP(1'000'000), owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpAsset});
            tx[sfLEVersion] = 2;
            env(tx, Ter(temMALFORMED));
            env.close();

            BEAST_EXPECT(!env.le(keylet));
        }

        {
            testcase("VaultSet rejects LEVersion set in the transaction");
            Env env{*this};
            env.fund(XRP(1'000'000), owner);
            env.close();

            Vault const vault{env};
            auto const [createTx, keylet] = vault.create({.owner = owner, .asset = xrpAsset});
            env(createTx, Ter(tesSUCCESS));
            env.close();

            auto setTx = vault.set({.owner = owner, .id = keylet.key});
            setTx[sfLEVersion] = 2;
            env(setTx, Ter(temMALFORMED));
            env.close();
        }
    }

    // Covers the third obligation gate in VaultDelete::preclaim
    // (sfAssetsReserved != 0). The first two guards (sfAssetsAvailable and
    // sfAssetsTotal) short-circuit on any real-world path that inflates
    // sfAssetsReserved — the only production writer is the two-step LoanSet
    // pending-loan bookkeeping, which simultaneously moves the same amount
    // out of sfAssetsAvailable, so the first check always fires first.
    // Reproducing the (Available == 0, Total == 0, Reserved != 0)
    // combination from real txs is not possible, so this test installs the
    // residual directly on the vault SLE via OpenLedger::modify (the same
    // lower-layer edit VaultShares_test uses to tamper with token fields)
    // and confirms preclaim rejects the delete with tecHAS_OBLIGATIONS.
    void
    testVaultDeleteAssetsReservedBlocks()
    {
        testcase("VaultDelete rejected when only AssetsReserved is non-zero");

        using namespace test::jtx;

        Env env{*this};
        Account const owner{"owner"};
        env.fund(XRP(1'000'000), owner);
        env.close();

        Vault const vault{env};
        PrettyAsset const xrpAsset = xrpIssue();
        auto const [tx, keylet] = vault.create({.owner = owner, .asset = xrpAsset});
        env(tx, Ter(tesSUCCESS));
        env.close();

        // Baseline: a freshly-created empty vault has all three buckets at
        // zero, so without the mutation below VaultDelete would succeed.
        if (auto const v = env.le(keylet); BEAST_EXPECT(v))
        {
            BEAST_EXPECT(v->at(sfAssetsAvailable) == beast::kZero);
            BEAST_EXPECT(v->at(sfAssetsTotal) == beast::kZero);
            BEAST_EXPECT(v->at(sfAssetsReserved) == beast::kZero);
        }

        // Install a non-zero sfAssetsReserved directly on the vault SLE.
        // ValidVault only inspects vault accounting when a tx mutates the
        // vault; the raw edit happens outside the tx machinery so no
        // invariant fires. VaultDelete below rejects at preclaim, so it
        // never modifies the vault and invariants stay silent for the tx
        // too.
        Number const kReserved{1'000};
        auto const mutated =
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
                Sandbox sb(&view, TapNone);
                auto v = sb.peek(keylet);
                if (!v)
                    return false;
                v->at(sfAssetsReserved) = kReserved;
                sb.update(v);
                sb.apply(view);
                return true;
            });
        if (!BEAST_EXPECT(mutated))
            return;

        // Sanity: the residual is visible and the two preceding guards
        // (Available, Total) still resolve to zero, so preclaim's third
        // check is the one that fires.
        if (auto const v = env.le(keylet); BEAST_EXPECT(v))
        {
            BEAST_EXPECT(v->at(sfAssetsAvailable) == beast::kZero);
            BEAST_EXPECT(v->at(sfAssetsTotal) == beast::kZero);
            BEAST_EXPECT(v->at(sfAssetsReserved) == kReserved);
        }

        // Delete against the mutated open view. Not closing after: on
        // close, OpenLedger::accept rebuilds the open view from the
        // last-closed ledger and re-applies pending txs, discarding raw
        // mutations, so the post-condition is read from the open view.
        env(vault.del({.owner = owner, .id = keylet.key}), Ter(tecHAS_OBLIGATIONS));

        // Preclaim rejected the delete, so the fee was charged but the
        // vault SLE is untouched.
        BEAST_EXPECT(env.le(keylet) != nullptr);
    }

    // A pseudo-account belongs to a ledger object, so it must never be the
    // destination of a withdrawal. The payout is refused either way, by the
    // deposit authorization every pseudo-account carries, so the only change
    // is a misleading tecNO_PERMISSION becoming tecPSEUDO_ACCOUNT. The check
    // runs ahead of the private-vault domain check, which would otherwise
    // report a domain problem against an account that can never join one.
    void
    testVaultWithdrawPseudoAccountDestination(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_4_0];
        testcase(
            std::string{"VaultWithdraw pseudo-account destination"} +
            (withFix ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer{"credIssuer"};
        std::string const credType = "credential";

        Env env{*this, features};
        Vault const vault{env};

        env.fund(XRP(100'000), issuer, owner, depositor, pdOwner, credIssuer);
        // Rippling plays no part in what is being tested here, and would
        // otherwise stop the payout before it reaches the check under test.
        env(fset(issuer, asfDefaultRipple));
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        for (auto const& account : {owner, depositor})
        {
            env.trust(asset(1'000'000), account);
            env(pay(issuer, account, asset(10'000)));
        }
        env.close();

        // Another vault over the same asset supplies the destination. Its
        // pseudo-account holds a trust line for the asset from creation, so
        // the payout is refused for being a pseudo-account and nothing else.
        auto const pseudoDestination = [&]() {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            return Account("otherVault", env.le(keylet)->at(sfAccount));
        }();

        TER const expected = withFix ? TER(tecPSEUDO_ACCOUNT) : TER(tecNO_PERMISSION);

        auto const withdrawToPseudo = [&](uint256 const& vaultId) {
            auto tx = vault.withdraw({.depositor = depositor, .id = vaultId, .amount = asset(1)});
            tx[sfDestination] = pseudoDestination.human();
            return tx;
        };

        {
            auto [createTx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(createTx);
            env.close();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1'000)}));
            env.close();

            env(withdrawToPseudo(keylet.key), Ter(expected));
            env.close();

            // Withdrawing to self out of the same vault stays unaffected.
            env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(1)}));
            env.close();
        }

        {
            auto const domainId = [&]() {
                pdomain::Credentials const credentials{
                    {.issuer = credIssuer, .credType = credType}};
                env(pdomain::setTx(pdOwner, credentials));
                env.close();
                return pdomain::getNewDomain(env.meta());
            }();

            env(credentials::create(depositor, credIssuer, credType));
            env(credentials::accept(depositor, credIssuer, credType));
            env.close();

            auto [createTx, keylet] =
                vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
            env(createTx);
            env.close();

            auto setTx = vault.set({.owner = owner, .id = keylet.key});
            setTx[sfDomainID] = to_string(domainId);
            env(setTx);
            env.close();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1'000)}));
            env.close();

            // The domain check never gets a say: the destination is rejected
            // for what it is, not for the domain it is missing.
            env(withdrawToPseudo(keylet.key), Ter(expected));
            env.close();
        }
    }

public:
    void
    run() override
    {
        testPreflight();
        testCreateFailXRP();
        testCreateFailIOU();
        testCreateFailMPT();
        testVaultDeleteMemoData();
        testVaultDeleteAssetsReservedBlocks();
        testVaultCreateLEVersion();

        testVaultWithdrawPseudoAccountDestination(all_ - fixCleanup3_4_0);
        testVaultWithdrawPseudoAccountDestination(all_);
    }
};

BEAST_DEFINE_TESTSUITE(VaultValidation, app, xrpl);

}  // namespace xrpl

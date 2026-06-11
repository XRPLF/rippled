#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/rate.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class Vault_test : public beast::unit_test::Suite
{
    using PrettyAsset = xrpl::test::jtx::PrettyAsset;
    using PrettyAmount = xrpl::test::jtx::PrettyAmount;

    static constexpr auto kNegativeAmount = [](PrettyAsset const& asset) -> PrettyAmount {
        return {STAmount{asset.raw(), 1ul, 0, true, STAmount::Unchecked{}}, ""};
    };

    void
    testSequences()
    {
        using namespace test::jtx;
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const charlie{"charlie"};  // authorized 3rd party
        Account const dave{"dave"};

        auto const testSequence = [&, this](
                                      std::string const& prefix,
                                      Env& env,
                                      Vault& vault,
                                      PrettyAsset const& asset) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfData] = "AFEED00E";
            tx[sfAssetsMaximum] = asset(100).number();
            env(tx);
            env.close();
            BEAST_EXPECT(env.le(keylet));
            std::uint64_t const scale = asset.raw().holds<MPTIssue>() ? 1 : 1e6;

            auto const [share, vaultAccount] =
                [&env, keylet = keylet, asset, this]() -> std::tuple<PrettyAsset, Account> {
                auto const vault = env.le(keylet);
                BEAST_EXPECT(vault != nullptr);
                if (!asset.integral())
                {
                    BEAST_EXPECT(vault->at(sfScale) == 6);
                }
                else
                {
                    BEAST_EXPECT(vault->at(sfScale) == 0);
                }
                auto const shares = env.le(keylet::mptIssuance(vault->at(sfShareMPTID)));
                BEAST_EXPECT(shares != nullptr);
                if (!asset.integral())
                {
                    BEAST_EXPECT(shares->at(sfAssetScale) == 6);
                }
                else
                {
                    BEAST_EXPECT(shares->at(sfAssetScale) == 0);
                }
                return {MPTIssue(vault->at(sfShareMPTID)), Account("vault", vault->at(sfAccount))};
            }();
            auto const shares = share.raw().get<MPTIssue>();
            env.memoize(vaultAccount);

            // Several 3rd party accounts which cannot receive funds
            Account const alice{"alice"};
            Account const erin{"erin"};  // not authorized by issuer
            env.fund(XRP(1000), alice, erin);
            env(fset(alice, asfDepositAuth));
            env.close();

            {
                testcase(prefix + " fail to deposit more than assets held");
                auto tx = vault.deposit(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(10000)});
                env(tx, Ter(tecINSUFFICIENT_FUNDS));
                env.close();
            }

            {
                testcase(prefix + " deposit non-zero amount");
                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(depositor, shares) == share(50 * scale));
            }

            {
                testcase(prefix + " deposit non-zero amount again");
                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(depositor, shares) == share(100 * scale));
            }

            {
                testcase(prefix + " fail to delete non-empty vault");
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx, Ter(tecHAS_OBLIGATIONS));
                env.close();
            }

            {
                testcase(prefix + " fail to update because wrong owner");
                auto tx = vault.set({.owner = issuer, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(50).number();
                env(tx, Ter(tecNO_PERMISSION));
                env.close();
            }

            {
                testcase(prefix + " fail to set maximum lower than current amount");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(50).number();
                env(tx, Ter(tecLIMIT_EXCEEDED));
                env.close();
            }

            {
                testcase(prefix + " set maximum higher than current amount");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(150).number();
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " set maximum is idempotent, set it again");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(150).number();
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " set data");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfData] = "0";
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " fail to set domain on public vault");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
                env(tx, Ter{tecNO_PERMISSION});
                env.close();
            }

            {
                testcase(prefix + " fail to deposit more than maximum");
                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                env(tx, Ter(tecLIMIT_EXCEEDED));
                env.close();
            }

            {
                testcase(prefix + " reset maximum to zero i.e. not enforced");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(0).number();
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " fail to withdraw more than assets held");
                auto tx = vault.withdraw(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(1000)});
                env(tx, Ter(tecINSUFFICIENT_FUNDS));
                env.close();
            }

            {
                testcase(prefix + " deposit some more");
                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(depositor, shares) == share(200 * scale));
            }

            {
                testcase(prefix + " clawback some");
                auto code = asset.raw().native() ? Ter(temMALFORMED) : Ter(tesSUCCESS);
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(10)});
                env(tx, code);
                env.close();
                if (!asset.raw().native())
                {
                    BEAST_EXPECT(env.balance(depositor, shares) == share(190 * scale));
                }
            }

            {
                testcase(prefix + " clawback all");
                auto code = asset.raw().native() ? Ter(tecNO_PERMISSION) : Ter(tesSUCCESS);
                auto tx = vault.clawback({.issuer = issuer, .id = keylet.key, .holder = depositor});
                env(tx, code);
                env.close();
                if (!asset.raw().native())
                {
                    BEAST_EXPECT(env.balance(depositor, shares) == share(0));

                    {
                        auto tx = vault.clawback(
                            {.issuer = issuer,
                             .id = keylet.key,
                             .holder = depositor,
                             .amount = asset(10)});
                        env(tx, Ter{tecPRECISION_LOSS});
                        env.close();
                    }

                    {
                        auto tx = vault.withdraw(
                            {.depositor = depositor, .id = keylet.key, .amount = asset(10)});
                        env(tx, Ter{tecPRECISION_LOSS});
                        env.close();
                    }
                }
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " deposit again");
                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(200)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(depositor, shares) == share(200 * scale));
            }
            else
            {
                testcase(prefix + " deposit/withdrawal same or less than fee");
                auto const amount = env.current()->fees().base;

                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = amount});
                env(tx);
                env.close();

                tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = amount});
                env(tx);
                env.close();

                tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = amount});
                env(tx);
                env.close();

                // Withdraw to 3rd party
                tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = amount});
                tx[sfDestination] = charlie.human();
                env(tx);
                env.close();

                tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = amount - 1});
                env(tx);
                env.close();

                tx = vault.withdraw(
                    {.depositor = depositor, .id = keylet.key, .amount = amount - 1});
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " fail to withdraw to 3rd party lsfDepositAuth");
                auto tx = vault.withdraw(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                tx[sfDestination] = alice.human();
                env(tx, Ter{tecNO_PERMISSION});
                env.close();
            }

            {
                testcase(prefix + " fail to withdraw to zero destination");
                auto tx = vault.withdraw(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(1000)});
                tx[sfDestination] = "0";
                env(tx, Ter(temMALFORMED));
                env.close();
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " fail to withdraw to 3rd party no authorization");
                auto tx = vault.withdraw(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                tx[sfDestination] = erin.human();
                env(tx, Ter{asset.raw().holds<Issue>() ? tecNO_LINE : tecNO_AUTH});
                env.close();
            }

            {
                testcase(prefix + " fail to withdraw to 3rd party lsfRequireDestTag");
                auto tx = vault.withdraw(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                tx[sfDestination] = dave.human();
                env(tx, Ter{tecDST_TAG_NEEDED});
                env.close();
            }

            {
                testcase(prefix + " withdraw to 3rd party lsfRequireDestTag");
                auto tx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                tx[sfDestination] = dave.human();
                tx[sfDestinationTag] = "0";
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " deposit again");
                auto tx = vault.deposit({.depositor = dave, .id = keylet.key, .amount = asset(50)});
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " fail to withdraw lsfRequireDestTag");
                auto tx =
                    vault.withdraw({.depositor = dave, .id = keylet.key, .amount = asset(50)});
                env(tx, Ter{tecDST_TAG_NEEDED});
                env.close();
            }

            {
                testcase(prefix + " withdraw with tag");
                auto tx =
                    vault.withdraw({.depositor = dave, .id = keylet.key, .amount = asset(50)});
                tx[sfDestinationTag] = "0";
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " withdraw to authorized 3rd party");
                auto tx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                tx[sfDestination] = charlie.human();
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(depositor, shares) == share(100 * scale));
            }

            {
                testcase(prefix + " withdraw to issuer");
                auto tx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                tx[sfDestination] = issuer.human();
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(depositor, shares) == share(50 * scale));
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " issuer deposits");
                auto tx =
                    vault.deposit({.depositor = issuer, .id = keylet.key, .amount = asset(10)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(issuer, shares) == share(10 * scale));

                testcase(prefix + " issuer withdraws");
                tx = vault.withdraw(
                    {.depositor = issuer, .id = keylet.key, .amount = share(10 * scale)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(issuer, shares) == share(0 * scale));
            }

            {
                testcase(prefix + " withdraw remaining assets");
                auto tx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(depositor, shares) == share(0));

                if (!asset.raw().native())
                {
                    auto tx = vault.clawback(
                        {.issuer = issuer,
                         .id = keylet.key,
                         .holder = depositor,
                         .amount = asset(0)});
                    env(tx, Ter{tecPRECISION_LOSS});
                    env.close();
                }

                {
                    auto tx = vault.withdraw(
                        {.depositor = depositor, .id = keylet.key, .amount = share(10)});
                    env(tx, Ter{tecINSUFFICIENT_FUNDS});
                    env.close();
                }
            }

            if (!asset.integral())
            {
                testcase(prefix + " temporary authorization for 3rd party");
                env(trust(erin, asset(1000)));
                env(trust(issuer, asset(0), erin, tfSetfAuth));
                env(pay(issuer, erin, asset(10)));

                // Erin deposits all in vault, then sends shares to depositor
                auto tx = vault.deposit({.depositor = erin, .id = keylet.key, .amount = asset(10)});
                env(tx);
                env.close();
                {
                    auto tx = pay(erin, depositor, share(10 * scale));

                    // depositor no longer has MPToken for shares
                    env(tx, Ter{tecNO_AUTH});
                    env.close();

                    // depositor will gain MPToken for shares again
                    env(vault.deposit(
                        {.depositor = depositor, .id = keylet.key, .amount = asset(1)}));
                    env.close();

                    env(tx);
                    env.close();
                }

                testcase(prefix + " withdraw to authorized 3rd party");
                // Depositor withdraws assets, destined to Erin
                tx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(10)});
                tx[sfDestination] = erin.human();
                env(tx);
                env.close();

                // Erin returns assets to issuer
                env(pay(erin, issuer, asset(10)));
                env.close();

                testcase(prefix + " fail to pay to unauthorized 3rd party");
                env(trust(erin, asset(0)));
                env.close();

                // Erin has MPToken but is no longer authorized to hold assets
                env(pay(depositor, erin, share(1)), Ter{tecNO_LINE});
                env.close();

                // Depositor withdraws remaining single asset
                tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(1)});
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " fail to delete because wrong owner");
                auto tx = vault.del({.owner = issuer, .id = keylet.key});
                env(tx, Ter(tecNO_PERMISSION));
                env.close();
            }

            {
                testcase(prefix + " delete empty vault");
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx);
                env.close();
                BEAST_EXPECT(!env.le(keylet));
            }
        };

        auto testCases = [&, this](
                             std::string prefix, std::function<PrettyAsset(Env & env)> setup) {
            Env env{*this, testableAmendments()};

            Vault vault{env};
            env.fund(XRP(1000), issuer, owner, depositor, charlie, dave);
            env.close();
            env(fset(issuer, asfAllowTrustLineClawback));
            env(fset(issuer, asfRequireAuth));
            env(fset(dave, asfRequireDest));
            env.close();
            env.require(Flags(issuer, asfAllowTrustLineClawback));
            env.require(Flags(issuer, asfRequireAuth));

            PrettyAsset const asset = setup(env);
            testSequence(prefix, env, vault, asset);
        };

        testCases("XRP", [&](Env& env) -> PrettyAsset { return {xrpIssue(), 1'000'000}; });

        testCases("IOU", [&](Env& env) -> Asset {
            PrettyAsset const asset = issuer["IOU"];
            env(trust(owner, asset(1000)));
            env(trust(depositor, asset(1000)));
            env(trust(charlie, asset(1000)));
            env(trust(dave, asset(1000)));
            env(trust(issuer, asset(0), owner, tfSetfAuth));
            env(trust(issuer, asset(0), depositor, tfSetfAuth));
            env(trust(issuer, asset(0), charlie, tfSetfAuth));
            env(trust(issuer, asset(0), dave, tfSetfAuth));
            env(pay(issuer, depositor, asset(1000)));
            env.close();
            return asset;
        });

        testCases("MPT", [&](Env& env) -> Asset {
            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = depositor});
            mptt.authorize({.account = charlie});
            mptt.authorize({.account = dave});
            env(pay(issuer, depositor, asset(1000)));
            env.close();
            return asset;
        });
    }

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
    testNonTransferableShares()
    {
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(1000), issuer, owner, depositor);
        env.close();

        Vault const vault{env};
        PrettyAsset const asset = issuer["IOU"];
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
            auto tx1 =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(40)});
            env(tx1);

            auto tx2 = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(60)});
            env(tx2);
            env.close();
        }

        auto const vaultAccount =  //
            [&env, key = keylet.key, this]() -> AccountID {
            auto jvVault = env.rpc("vault_info", strHex(key));

            BEAST_EXPECT(jvVault[jss::result][jss::vault][sfAssetsTotal] == "100");
            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][jss::shares][sfOutstandingAmount] == "100000000");

            // Vault pseudo-account
            return parseBase58<AccountID>(jvVault[jss::result][jss::vault][jss::Account].asString())
                .value();
        }();

        auto const mptId = makeMptID(1, vaultAccount);
        Asset const shares = mptId;

        {
            testcase("nontransferable shares cannot be moved");
            env(pay(owner, depositor, shares(10)), Ter{tecNO_AUTH});
            env(pay(depositor, owner, shares(10)), Ter{tecNO_AUTH});
        }

        {
            testcase("nontransferable shares can be used to withdraw");
            auto tx1 =
                vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(20)});
            env(tx1);

            auto tx2 = vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(30)});
            env(tx2);
            env.close();
        }

        {
            testcase("nontransferable shares balance check");
            auto jvVault = env.rpc("vault_info", strHex(keylet.key));
            BEAST_EXPECT(jvVault[jss::result][jss::vault][sfAssetsTotal] == "50");
            BEAST_EXPECT(
                jvVault[jss::result][jss::vault][jss::shares][sfOutstandingAmount] == "50000000");
        }

        {
            testcase("nontransferable shares withdraw rest");
            auto tx1 =
                vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(20)});
            env(tx1);

            auto tx2 = vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(30)});
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

        struct CaseArgs
        {
            bool enableClawback = true;
            bool requireAuth = true;
            int initialXRP = 1000;
        };

        auto testCase = [this](
                            std::function<void(
                                Env & env,
                                Account const& issuer,
                                Account const& owner,
                                Account const& depositor,
                                Asset const& asset,
                                Vault& vault,
                                MPTTester& mptt)> test,
                            CaseArgs args = {}) {
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            env.fund(XRP(args.initialXRP), issuer, owner, depositor);
            env.close();
            Vault vault{env};

            MPTTester mptt{env, issuer, kMptInitNoFund};
            auto const kNone = LedgerSpecificFlags(0);
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock |
                     (args.enableClawback ? tfMPTCanClawback : kNone) |
                     (args.requireAuth ? tfMPTRequireAuth : kNone),
                 .mutableFlags = tmfMPTCanMutateCanTransfer});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            if (args.requireAuth)
            {
                mptt.authorize({.account = issuer, .holder = owner});
                mptt.authorize({.account = issuer, .holder = depositor});
            }

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
            testcase("MPT nothing to clawback from");
            auto tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet::skip().key,
                 .holder = depositor,
                 .amount = asset(10)});
            env(tx, Ter(tecNO_ENTRY));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT global lock blocks create");
            mptt.set({.account = issuer, .flags = tfMPTLock});
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tecLOCKED));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT global lock blocks deposit");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            mptt.set({.account = issuer, .flags = tfMPTLock});
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx, Ter{tecLOCKED});
            env.close();

            // Can delete empty vault, even if global lock
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
            testcase("MPT global lock blocks withdrawal");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx);
            env.close();

            // Check that the OutstandingAmount field of MPTIssuance
            // accounts for the issued shares.
            auto v = env.le(keylet);
            BEAST_EXPECT(v);
            MPTID const share = (*v)[sfShareMPTID];
            auto issuance = env.le(keylet::mptIssuance(share));
            BEAST_EXPECT(issuance);
            Number const outstandingShares = issuance->at(sfOutstandingAmount);
            BEAST_EXPECT(outstandingShares == 100);

            mptt.set({.account = issuer, .flags = tfMPTLock});
            env.close();

            tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx, Ter(tecLOCKED));

            tx[sfDestination] = issuer.human();
            env(tx, Ter(tecLOCKED));

            // Clawback is still permitted, even with global lock
            tx = vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(0)});
            env(tx);
            env.close();

            // Clawback removed shares MPToken
            auto const mptSle = env.le(keylet::mptoken(share, depositor.id()));
            BEAST_EXPECT(mptSle == nullptr);

            // Can delete empty vault, even if global lock
            tx = vault.del({.owner = owner, .id = keylet.key});
            env(tx);
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT only issuer can clawback");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx);
            env.close();

            {
                auto tx = vault.clawback({
                    .issuer = depositor,
                    .id = keylet.key,
                    .holder = depositor,
                });
                env(tx, Ter(tecNO_PERMISSION));
            }

            {
                auto tx = vault.clawback({
                    .issuer = owner,
                    .id = keylet.key,
                    .holder = depositor,
                });
                env(tx, Ter(tecNO_PERMISSION));
            }
        });

        testCase(
            [this](
                Env& env,
                Account const& issuer,
                Account const& owner,
                Account const& depositor,
                PrettyAsset const& asset,
                Vault& vault,
                MPTTester& mptt) {
                testcase("MPT depositor without MPToken, auth required");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                tx = vault.deposit(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(1000)});
                env(tx);
                env.close();

                {
                    // Remove depositor MPToken and it will not be re-created
                    mptt.authorize({.account = depositor, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken = keylet::mptoken(mptt.issuanceID(), depositor);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 == nullptr);

                    tx = vault.withdraw(
                        {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                    env(tx, Ter{tecNO_AUTH});
                    env.close();

                    auto const sleMPT2 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT2 == nullptr);
                }

                {
                    // Set destination to 3rd party without MPToken
                    Account const charlie{"charlie"};
                    env.fund(XRP(1000), charlie);
                    env.close();

                    tx = vault.withdraw(
                        {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                    tx[sfDestination] = charlie.human();
                    env(tx, Ter(tecNO_AUTH));
                }
            },
            {.requireAuth = true});

        testCase(
            [this](
                Env& env,
                Account const& issuer,
                Account const& owner,
                Account const& depositor,
                PrettyAsset const& asset,
                Vault& vault,
                MPTTester& mptt) {
                testcase("MPT depositor without MPToken, no auth required");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();
                auto v = env.le(keylet);
                BEAST_EXPECT(v);

                tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1000)});  // all assets held by depositor
                env(tx);
                env.close();

                {
                    // Remove depositor's MPToken and it will be re-created
                    mptt.authorize({.account = depositor, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken = keylet::mptoken(mptt.issuanceID(), depositor);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 == nullptr);

                    tx = vault.withdraw(
                        {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                    env(tx);
                    env.close();

                    auto const sleMPT2 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT2 != nullptr);
                    BEAST_EXPECT(sleMPT2->at(sfMPTAmount) == 100);
                }

                {
                    // Remove 3rd party MPToken and it will not be re-created
                    mptt.authorize({.account = owner, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken = keylet::mptoken(mptt.issuanceID(), owner);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 == nullptr);

                    tx = vault.withdraw(
                        {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                    tx[sfDestination] = owner.human();
                    env(tx, Ter(tecNO_AUTH));
                    env.close();

                    auto const sleMPT2 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT2 == nullptr);
                }
            },
            {.requireAuth = false});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env const env{*this, testableAmendments()};
            return {
                env.current()->fees().accountReserve(0).drops() / kDropsPerXrp.drops(),
                env.current()->fees().increment.drops() / kDropsPerXrp.drops()};
        }();

        testCase(
            [&, this](
                Env& env,
                Account const& issuer,
                Account const& owner,
                Account const& depositor,
                PrettyAsset const& asset,
                Vault& vault,
                MPTTester& mptt) {
                testcase("MPT fail reserve to re-create MPToken");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();
                auto v = env.le(keylet);
                BEAST_EXPECT(v);

                env(pay(depositor, owner, asset(1000)));
                env.close();

                tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(1000)});  // all assets held by owner
                env(tx);
                env.close();

                {
                    // Remove owners's MPToken and it will not be re-created
                    mptt.authorize({.account = owner, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken = keylet::mptoken(mptt.issuanceID(), owner);
                    auto const sleMPT = env.le(mptoken);
                    BEAST_EXPECT(sleMPT == nullptr);

                    // Use one reserve so the next transaction fails
                    env(ticket::create(owner, 1));
                    env.close();

                    // No reserve to create MPToken for asset in VaultWithdraw
                    tx = vault.withdraw(
                        {.depositor = owner, .id = keylet.key, .amount = asset(100)});
                    env(tx, Ter{tecINSUFFICIENT_RESERVE});
                    env.close();

                    env(pay(depositor, owner, XRP(incReserve)));
                    env.close();

                    // Withdraw can now create asset MPToken, tx will succeed
                    env(tx);
                    env.close();
                }
            },
            {.requireAuth = false, .initialXRP = acctReserve + (incReserve * 4) + 1});

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT issuance deleted");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1000)});
            env(tx);
            env.close();

            {
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(0)});
                env(tx);
            }

            mptt.destroy({.issuer = issuer, .id = mptt.issuanceID()});
            env.close();

            {
                auto [tx, keylet] = vault.create({.owner = depositor, .asset = asset});
                env(tx, Ter{tecOBJECT_NOT_FOUND});
            }

            {
                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(10)});
                env(tx, Ter{tecOBJECT_NOT_FOUND});
            }

            {
                auto tx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(10)});
                env(tx, Ter{tecOBJECT_NOT_FOUND});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(0)});
                env(tx, Ter{tecOBJECT_NOT_FOUND});
            }

            env(vault.del({.owner = owner, .id = keylet.key}));
        });

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT vault owner can receive shares unless unauthorized");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1000)});
            env(tx);
            env.close();

            auto const issuanceId = [&env](xrpl::Keylet keylet) -> MPTID {
                auto const vault = env.le(keylet);
                return vault->at(sfShareMPTID);
            }(keylet);
            PrettyAsset const shares = MPTIssue(issuanceId);

            {
                // owner has MPToken for shares they did not explicitly create
                env(pay(depositor, owner, shares(1)));
                env.close();

                tx = vault.withdraw({.depositor = owner, .id = keylet.key, .amount = shares(1)});
                env(tx);
                env.close();

                // owner's MPToken for vault shares not destroyed by withdraw
                env(pay(depositor, owner, shares(1)));
                env.close();

                tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(0)});
                env(tx);
                env.close();

                // owner's MPToken for vault shares not destroyed by clawback
                env(pay(depositor, owner, shares(1)));
                env.close();

                // pay back, so we can destroy owner's MPToken now
                env(pay(owner, depositor, shares(1)));
                env.close();

                {
                    // explicitly destroy vault owners MPToken with zero balance
                    json::Value jv;
                    jv[sfAccount] = owner.human();
                    jv[sfMPTokenIssuanceID] = to_string(issuanceId);
                    jv[sfFlags] = tfMPTUnauthorize;
                    jv[sfTransactionType] = jss::MPTokenAuthorize;
                    env(jv);
                    env.close();
                }

                // owner no longer has MPToken for vault shares
                tx = pay(depositor, owner, shares(1));
                env(tx, Ter{tecNO_AUTH});
                env.close();

                // destroy all remaining shares, so we can delete vault
                tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(0)});
                env(tx);
                env.close();

                // will soft fail destroying MPToken for vault owner
                env(vault.del({.owner = owner, .id = keylet.key}));
                env.close();
            }
        });

        testCase(
            [this](
                Env& env,
                Account const& issuer,
                Account const& owner,
                Account const& depositor,
                PrettyAsset const& asset,
                Vault& vault,
                MPTTester& mptt) {
                testcase("MPT clawback disabled");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                tx = vault.deposit(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(1000)});
                env(tx);
                env.close();

                {
                    auto tx = vault.clawback(
                        {.issuer = issuer,
                         .id = keylet.key,
                         .holder = depositor,
                         .amount = asset(0)});
                    env(tx, Ter{tecNO_PERMISSION});
                }
            },
            {.enableClawback = false});

        testCase([this](
                     Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Account const& depositor,
                     Asset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT un-authorization");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1000)});
            env(tx);
            env.close();

            mptt.authorize({.account = issuer, .holder = depositor, .flags = tfMPTUnauthorize});
            env.close();

            {
                auto tx = vault.withdraw(
                    {.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                env(tx, Ter(tecNO_AUTH));

                // Withdrawal to other (authorized) accounts works
                tx[sfDestination] = issuer.human();
                env(tx);
                env.close();

                tx[sfDestination] = owner.human();
                env(tx);
                env.close();
            }

            {
                // Cannot deposit some more
                auto tx =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
                env(tx, Ter(tecNO_AUTH));
            }

            {
                // Cannot clawback if issuer is the holder
                tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = issuer, .amount = asset(800)});
                env(tx, Ter(tecNO_PERMISSION));
            }
            // Clawback works
            tx = vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(800)});
            env(tx);
            env.close();

            env(vault.del({.owner = owner, .id = keylet.key}));
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

            auto const vaultAccount = [&env, keylet = keylet, this]() -> AccountID {
                auto const vault = env.le(keylet);
                BEAST_EXPECT(vault != nullptr);
                return vault->at(sfAccount);
            }();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx);
            env.close();

            tx = [&]() {
                json::Value jv;
                jv[jss::Account] = issuer.human();
                jv[sfMPTokenIssuanceID] = to_string(asset.get<MPTIssue>().getMptID());
                jv[jss::Holder] = toBase58(vaultAccount);
                jv[jss::TransactionType] = jss::MPTokenIssuanceSet;
                jv[jss::Flags] = tfMPTLock;
                return jv;
            }();
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx, Ter(tecLOCKED));

            tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx, Ter(tecLOCKED));

            // Clawback works, even when locked
            tx = vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(100)});
            env(tx);

            // Can delete an empty vault even when asset is locked.
            tx = vault.del({.owner = owner, .id = keylet.key});
            env(tx);
        });

        {
            testcase("MPT shares to a vault");

            Env env{*this, testableAmendments()};
            Account const owner{"owner"};
            Account const issuer{"issuer"};
            env.fund(XRP(1000000), owner, issuer);
            env.close();
            Vault const vault{env};

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock | lsfMPTCanClawback | tfMPTRequireAuth});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = issuer, .holder = owner});
            PrettyAsset const asset = mptt.issuanceID();
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
            env(tx2, Ter{tecWRONG_ASSET});
            env.close();
        }

        testCase([this](
                     Env& env,
                     Account const&,
                     Account const& owner,
                     Account const& depositor,
                     PrettyAsset const& asset,
                     Vault& vault,
                     MPTTester& mptt) {
            testcase("MPT non-transferable: block deposit, allow withdraw");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx);
            env.close();

            // Issuer governance: clear CanTransfer. New exposure must be
            // blocked, but recovery paths must remain open so existing
            // depositors are not trapped.
            mptt.set({.mutableFlags = tmfMPTClearCanTransfer});
            env.close();

            // New deposit is blocked.
            env(tx, Ter{tecNO_AUTH});
            env.close();

            // Existing depositor can always withdraw, even though the asset
            // is no longer freely transferable.
            tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(tx);
            env.close();

            // Delete vault with zero balance
            env(vault.del({.owner = owner, .id = keylet.key}));
        });

        {
            testcase("MPT non-transferable: pre-fixCleanup3_2_0 withdraw blocked");

            // Regression: before fixCleanup3_2_0 a depositor was trapped if
            // the issuer cleared lsfMPTCanTransfer. Verify that the legacy
            // (broken) behavior is preserved when the amendment is disabled.
            Env env{*this, testableAmendments() - fixCleanup3_2_0};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            env.fund(XRP(10'000), issuer, owner, depositor);
            env.close();
            Vault const vault{env};

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock,
                 .mutableFlags = tmfMPTCanMutateCanTransfer});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1'000)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)}));
            env.close();

            mptt.set({.mutableFlags = tmfMPTClearCanTransfer});
            env.close();

            // Pre-amendment: deposit blocked (matches new behavior).
            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)}),
                Ter{tecNO_AUTH});
            env.close();

            // Pre-amendment: withdraw is also blocked - this is the bug
            // that fixCleanup3_2_0 fixes.
            env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(100)}),
                Ter{tecNO_AUTH});
            env.close();
        }

        {
            testcase("MPT non-transferable: vault shares inherit restriction");

            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), issuer, owner, alice, bob);
            env.close();
            Vault const vault{env};

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock,
                 .mutableFlags = tmfMPTCanMutateCanTransfer});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = alice});
            mptt.authorize({.account = bob});
            env(pay(issuer, alice, asset(1'000)));
            env(pay(issuer, bob, asset(1'000)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(500)}));
            // Bob also deposits so he has a share MPToken to receive into.
            env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(500)}));
            env.close();

            auto const shares = [&]() -> PrettyAsset {
                auto const sle = env.le(keylet);
                BEAST_EXPECT(sle != nullptr);
                return MPTIssue(sle->at(sfShareMPTID));
            }();

            // Sanity: while CanTransfer is set on the underlying, peer-to-peer
            // share transfers are allowed.
            env(pay(alice, bob, shares(1)));
            env.close();

            // Issuer governance: clear CanTransfer on the underlying.
            mptt.set({.mutableFlags = tmfMPTClearCanTransfer});
            env.close();

            // Vault shares inherit the restriction: third-party share-to-share
            // payments are blocked.
            env(pay(alice, bob, shares(1)), Ter{tecNO_AUTH});
            env.close();

            // Recovery path: existing share holders can still redeem shares
            // for the underlying asset via VaultWithdraw.
            env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = shares(1)}));
            env.close();
        }

        {
            testcase("MPT locked: vault shares inherit underlying lock");

            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(10'000), issuer, owner, alice, bob, carol);
            env.close();
            Vault const vault{env};

            MPTTester asset{
                {.env = env,
                 .issuer = issuer,
                 .holders = {owner, alice, bob, carol},
                 .flags = tfMPTCanTransfer | tfMPTCanTrade | tfMPTCanLock}};
            env(pay(issuer, alice, asset(1'000)));
            env(pay(issuer, bob, asset(1'000)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(500)}));
            // Bob also deposits so he has a share MPToken to receive into.
            env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(500)}));
            env.close();

            auto const shares = [&]() -> PrettyAsset {
                auto const sle = env.le(keylet);
                BEAST_EXPECT(sle != nullptr);
                return MPTIssue(sle->at(sfShareMPTID));
            }();
            auto const shareMptID = shares.raw().get<MPTIssue>().getMptID();
            auto const shareBalance = [&](Account const& account) {
                auto const sle = env.le(keylet::mptoken(shareMptID, account));
                return sle ? sle->at(sfMPTAmount) : 0;
            };

            // Sanity: before the underlying lock, peer-to-peer share
            // transfers are allowed.
            env(pay(alice, bob, shares(1)));
            env.close();

            // Create the offer while shares are spendable, then lock the
            // underlying to test whether a stale offer can still be crossed.
            env(offer(alice, XRP(1), shares(1)));
            env.close();

            // Lock the underlying after the vault and share balances exist.
            asset.set({.account = issuer, .flags = tfMPTLock});
            env.close();

            // Direct vault share payment inherits the underlying lock via
            // sfReferenceHolding.
            BEAST_EXPECT(shareBalance(alice) == 499);
            BEAST_EXPECT(shareBalance(bob) == 501);
            env(pay(alice, bob, shares(1)), Ter{tecLOCKED});
            env.close();
            BEAST_EXPECT(shareBalance(alice) == 499);
            BEAST_EXPECT(shareBalance(bob) == 501);

            // The same inherited lock must also block DEX payment paths that
            // would consume an offer selling vault shares.
            env(pay(carol, bob, shares(1)),
                Sendmax(XRP(1)),
                Path(BookSpec{shares.raw()}),
                Ter{tecPATH_PARTIAL});
            env.close();
            BEAST_EXPECT(shareBalance(alice) == 499);
            BEAST_EXPECT(shareBalance(bob) == 501);
            BEAST_EXPECT(expectOffers(env, alice, 1));
        }

        {
            testcase("MPT non-transferable: pre-fixCleanup3_2_0 share transfer succeeds");

            // Regression: before fixCleanup3_2_0 a peer-to-peer share Payment
            // succeeded even when the underlying asset's lsfMPTCanTransfer
            // was cleared. Verify that the legacy (non-inheriting) behavior
            // is preserved when the amendment is disabled.
            Env env{*this, testableAmendments() - fixCleanup3_2_0};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), issuer, owner, alice, bob);
            env.close();
            Vault const vault{env};

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock,
                 .mutableFlags = tmfMPTCanMutateCanTransfer});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = alice});
            mptt.authorize({.account = bob});
            env(pay(issuer, alice, asset(1'000)));
            env(pay(issuer, bob, asset(1'000)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(500)}));
            env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(500)}));
            env.close();

            auto const shares = [&]() -> PrettyAsset {
                auto const sle = env.le(keylet);
                BEAST_EXPECT(sle != nullptr);
                return MPTIssue(sle->at(sfShareMPTID));
            }();

            mptt.set({.mutableFlags = tmfMPTClearCanTransfer});
            env.close();

            // Pre-amendment: share transfer leaks past underlying restriction.
            env(pay(alice, bob, shares(1)));
            env.close();
        }

        {
            testcase("MPT CanTrade governance: share inherits underlying on DEX and AMM");

            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100'000), issuer, owner, alice, bob);
            env.close();
            Vault const vault{env};

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanTrade | tfMPTCanLock,
                 .mutableFlags = tmfMPTCanMutateCanTrade});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = alice});
            mptt.authorize({.account = bob});
            env(pay(issuer, alice, asset(10'000)));
            env(pay(issuer, bob, asset(10'000)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            // Seed shares so we can later place them on trading venues.
            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(5'000)}));
            env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(5'000)}));
            env.close();

            auto const shares = [&]() -> PrettyAsset {
                auto const sle = env.le(keylet);
                BEAST_EXPECT(sle != nullptr);
                return MPTIssue(sle->at(sfShareMPTID));
            }();

            // Sanity: while CanTrade is set on the underlying, both the asset
            // and the vault share can be placed on the DEX.
            env(offer(alice, XRP(1), asset(10)));
            env(offer(alice, XRP(1), shares(1)));
            env.close();

            // Issuer governance: clear CanTrade on the underlying.
            mptt.set({.mutableFlags = tmfMPTClearCanTrade});
            env.close();

            // Control: clearing CanTrade on the underlying is observable on
            // the DEX path for that asset.
            env(offer(alice, XRP(1), asset(10)), Ter{tecNO_PERMISSION});
            env.close();

            // Control: clearing CanTrade on the underlying is also observable
            // on the AMM path for that asset.
            AMM const ammUnderlyingFails(
                env, alice, XRP(1'000), asset(1'000), Ter{tecNO_PERMISSION});

            // Post-fixCleanup3_2_0: vault shares inherit the underlying's
            // CanTrade restriction on the DEX path (canTrade reads the
            // share's sfReferenceHolding and dispatches to the underlying).
            env(offer(bob, XRP(1), shares(1)), Ter{tecNO_PERMISSION});
            env.close();

            // checkMPTAllowed mirrors the inheritance for AMM/Offer-
            // crossing/Check paths, so a share AMM also cannot be created
            // when the underlying CanTrade is cleared.
            AMM const ammShares(env, alice, XRP(1'000), shares(100), Ter{tecNO_PERMISSION});

            // Deposit still works (canAddHolding does not consult the field).
            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(100)}));
            env.close();

            // Peer-to-peer share transfers still work (CanTransfer is set on
            // both layers).
            env(pay(alice, bob, shares(1)));
            env.close();

            // Withdraw still works.
            env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = asset(100)}));
            env.close();
        }

        {
            testcase("MPT OutstandingAmount > MaximumAmount");

            Env env{*this, testableAmendments() | featureSingleAssetVault};
            Account const alice{"alice"};
            Account const issuer{"issuer"};
            env.fund(XRP(1'000), alice, issuer);
            env.close();
            Vault const vault{env};

            MPTTester const btc({.env = env, .issuer = issuer, .holders = {alice}, .maxAmt = 100});

            auto [tx, k] = vault.create({.owner = issuer, .asset = btc});
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = issuer, .id = k.key, .amount = btc(110)});
            // accountHolds is the first check and the issuer has only BTC(100)
            // available
            env(tx, Ter{tecINSUFFICIENT_FUNDS});
            env.close();

            // OutstandingAmount == MaximumAmount
            env(pay(issuer, alice, btc(100)));
            env.close();

            tx = vault.deposit({.depositor = issuer, .id = k.key, .amount = btc(100)});
            // the issuer has BTC(0) available
            env(tx, Ter{tecINSUFFICIENT_FUNDS});
            env.close();

            tx = vault.deposit({.depositor = alice, .id = k.key, .amount = btc(100)});
            // alice transfers BTC(100), OutstandingAmount is 100
            env(tx);
            env.close();
        }
    }

    void
    testWithIOU()
    {
        using namespace test::jtx;

        struct CaseArgs
        {
            int initialXRP = 1000;
            Number initialIOU = 200;
            double transferRate = 1.0;
            bool charlieRipple = true;
        };

        auto testCase = [&, this](
                            std::function<void(
                                Env & env,
                                Account const& owner,
                                Account const& issuer,
                                Account const& charlie,
                                std::function<Account(xrpl::Keylet)> vaultAccount,
                                Vault& vault,
                                PrettyAsset const& asset,
                                std::function<MPTID(xrpl::Keylet)> issuanceId)> test,
                            CaseArgs args = {}) {
            Env env{*this, testableAmendments()};
            Account const owner{"owner"};
            Account const issuer{"issuer"};
            Account const charlie{"charlie"};
            Vault vault{env};
            env.fund(XRP(args.initialXRP), issuer, owner, charlie);
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1000), owner);
            env(pay(issuer, owner, asset(args.initialIOU)));
            env.close();
            if (!args.charlieRipple)
            {
                env(fset(issuer, 0, asfDefaultRipple));
                env.close();
                env.trust(asset(1000), charlie);
                env.close();
                env(pay(issuer, charlie, asset(args.initialIOU)));
                env.close();
                env(fset(issuer, asfDefaultRipple));
            }
            else
            {
                env.trust(asset(1000), charlie);
            }
            env.close();
            env(rate(issuer, args.transferRate));
            env.close();

            auto const vaultAccount = [&env](xrpl::Keylet keylet) -> Account {
                return Account("vault", env.le(keylet)->at(sfAccount));
            };
            auto const issuanceId = [&env](xrpl::Keylet keylet) -> MPTID {
                return env.le(keylet)->at(sfShareMPTID);
            };

            test(env, owner, issuer, charlie, vaultAccount, vault, asset, issuanceId);
        };

        testCase([&, this](
                     Env& env,
                     Account const& owner,
                     Account const& issuer,
                     Account const&,
                     auto vaultAccount,
                     Vault& vault,
                     PrettyAsset const& asset,
                     auto&&...) {
            testcase("IOU cannot use different asset");
            PrettyAsset const foo = issuer["FOO"];

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            {
                // Cannot create new trustline to a vault
                auto tx = [&, account = vaultAccount(keylet)]() {
                    json::Value jv;
                    jv[jss::Account] = issuer.human();
                    {
                        auto& ja = jv[jss::LimitAmount] =
                            foo(0).value().getJson(JsonOptions::Values::None);
                        ja[jss::issuer] = toBase58(account);
                    }
                    jv[jss::TransactionType] = jss::TrustSet;
                    jv[jss::Flags] = tfSetFreeze;
                    return jv;
                }();
                env(tx, Ter{tecNO_PERMISSION});
                env.close();
            }

            {
                auto tx = vault.deposit({.depositor = issuer, .id = keylet.key, .amount = foo(20)});
                env(tx, Ter{tecWRONG_ASSET});
                env.close();
            }

            {
                auto tx =
                    vault.withdraw({.depositor = issuer, .id = keylet.key, .amount = foo(20)});
                env(tx, Ter{tecWRONG_ASSET});
                env.close();
            }

            env(vault.del({.owner = owner, .id = keylet.key}));
            env.close();
        });

        testCase([&, this](
                     Env& env,
                     Account const& owner,
                     Account const& issuer,
                     Account const& charlie,
                     auto vaultAccount,
                     Vault& vault,
                     PrettyAsset const& asset,
                     auto issuanceId) {
            testcase("IOU frozen trust line to vault account");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            Asset const share = Asset(issuanceId(keylet));

            // Freeze the trustline to the vault
            auto trustSet = [&, account = vaultAccount(keylet)]() {
                json::Value jv;
                jv[jss::Account] = issuer.human();
                {
                    auto& ja = jv[jss::LimitAmount] =
                        asset(0).value().getJson(JsonOptions::Values::None);
                    ja[jss::issuer] = toBase58(account);
                }
                jv[jss::TransactionType] = jss::TrustSet;
                jv[jss::Flags] = tfSetFreeze;
                return jv;
            }();
            env(trustSet);
            env.close();

            {
                // Note, the "frozen" state of the trust line to vault account
                // is reported as  "locked" state of the vault shares, because
                // this state is attached to shares by means of the transitive
                // isFrozen.
                auto tx =
                    vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(80)});
                env(tx, Ter{tecLOCKED});
            }

            {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(100)});
                env(tx, Ter{tecLOCKED});

                // also when trying to withdraw to a 3rd party
                tx[sfDestination] = charlie.human();
                env(tx, Ter{tecLOCKED});
                env.close();
            }

            {
                // Clawback works, even when locked
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(50)});
                env(tx);
                env.close();
            }

            // Clear the frozen state
            trustSet[jss::Flags] = tfClearFreeze;
            env(trustSet);
            env.close();

            env(vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = share(50'000'000)}));

            env(vault.del({.owner = owner, .id = keylet.key}));
            env.close();
        });

        testCase(
            [&, this](
                Env& env,
                Account const& owner,
                Account const& issuer,
                Account const& charlie,
                auto vaultAccount,
                Vault& vault,
                PrettyAsset const& asset,
                auto issuanceId) {
                testcase("IOU transfer fees not applied");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
                env.close();

                auto const issue = asset.raw().get<Issue>();
                Asset const share = Asset(issuanceId(keylet));

                // transfer fees ignored on deposit
                BEAST_EXPECT(env.balance(owner, issue) == asset(100));
                BEAST_EXPECT(env.balance(vaultAccount(keylet), issue) == asset(100));

                {
                    auto tx = vault.clawback(
                        {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(50)});
                    env(tx);
                    env.close();
                }

                // transfer fees ignored on clawback
                BEAST_EXPECT(env.balance(owner, issue) == asset(100));
                BEAST_EXPECT(env.balance(vaultAccount(keylet), issue) == asset(50));

                env(vault.withdraw(
                    {.depositor = owner, .id = keylet.key, .amount = share(20'000'000)}));

                // transfer fees ignored on withdraw
                BEAST_EXPECT(env.balance(owner, issue) == asset(120));
                BEAST_EXPECT(env.balance(vaultAccount(keylet), issue) == asset(30));

                {
                    auto tx = vault.withdraw(
                        {.depositor = owner, .id = keylet.key, .amount = share(30'000'000)});
                    tx[sfDestination] = charlie.human();
                    env(tx);
                }

                // transfer fees ignored on withdraw to 3rd party
                BEAST_EXPECT(env.balance(owner, issue) == asset(120));
                BEAST_EXPECT(env.balance(charlie, issue) == asset(30));
                BEAST_EXPECT(env.balance(vaultAccount(keylet), issue) == asset(0));

                env(vault.del({.owner = owner, .id = keylet.key}));
                env.close();
            },
            CaseArgs{.transferRate = 1.25});

        testCase([&, this](
                     Env& env,
                     Account const& owner,
                     Account const& issuer,
                     Account const& charlie,
                     auto,
                     Vault& vault,
                     PrettyAsset const& asset,
                     auto&&...) {
            testcase("IOU frozen trust line to depositor");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            // Withdraw to 3rd party works
            auto const withdrawToCharlie = [&](xrpl::Keylet keylet) {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                tx[sfDestination] = charlie.human();
                return tx;
            }(keylet);
            env(withdrawToCharlie);

            // Freeze the owner
            env(trust(issuer, asset(0), owner, tfSetFreeze));
            env.close();

            // Cannot withdraw
            auto const withdraw =
                vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
            env(withdraw, Ter{tecFROZEN});

            // Cannot withdraw to 3rd party
            env(withdrawToCharlie, Ter{tecLOCKED});
            env.close();

            {
                // Cannot deposit some more
                auto tx =
                    vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                env(tx, Ter{tecFROZEN});
            }

            {
                // Clawback still works
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(0)});
                env(tx);
                env.close();
            }

            env(vault.del({.owner = owner, .id = keylet.key}));
            env.close();
        });

        testCase([&, this](
                     Env& env,
                     Account const& owner,
                     Account const& issuer,
                     Account const& charlie,
                     auto,
                     Vault& vault,
                     PrettyAsset const& asset,
                     auto&&...) {
            testcase("IOU no trust line to 3rd party");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            Account const erin{"erin"};
            env.fund(XRP(1000), erin);
            env.close();

            // Withdraw to 3rd party without trust line
            auto const tx1 = [&](xrpl::Keylet keylet) {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                tx[sfDestination] = erin.human();
                return tx;
            }(keylet);
            env(tx1, Ter{tecNO_LINE});
        });

        testCase([&, this](
                     Env& env,
                     Account const& owner,
                     Account const& issuer,
                     Account const& charlie,
                     auto,
                     Vault& vault,
                     PrettyAsset const& asset,
                     auto&&...) {
            testcase("IOU no trust line to depositor");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            // reset limit, so deposit of all funds will delete the trust line
            env.trust(asset(0), owner);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(200)}));
            env.close();

            auto trustline = env.le(keylet::line(owner, asset.raw().get<Issue>()));
            BEAST_EXPECT(trustline == nullptr);

            // Withdraw without trust line, will succeed
            auto const tx1 = [&](xrpl::Keylet keylet) {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                return tx;
            }(keylet);
            env(tx1);
        });

        testCase(
            [&, this](
                Env& env,
                Account const& owner,
                Account const& issuer,
                Account const& charlie,
                auto vaultAccount,
                Vault& vault,
                PrettyAsset const& asset,
                std::function<MPTID(xrpl::Keylet)> issuanceId) {
                testcase("IOU non-transferable");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                tx[sfScale] = 0;
                env(tx);
                env.close();

                // Turn on noripple on the pseudo account's trust line.
                // Charlie's is already set.
                env(trust(issuer, vaultAccount(keylet)["IOU"], tfSetNoRipple));

                {
                    // Charlie cannot deposit
                    auto tx = vault.deposit(
                        {.depositor = charlie, .id = keylet.key, .amount = asset(100)});
                    env(tx, Ter{terNO_RIPPLE});
                    env.close();
                }

                {
                    PrettyAsset const shares = issuanceId(keylet);
                    auto tx1 =
                        vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)});
                    env(tx1);
                    env.close();

                    // Charlie cannot receive funds
                    auto tx2 = vault.withdraw(
                        {.depositor = owner, .id = keylet.key, .amount = shares(100)});
                    tx2[sfDestination] = charlie.human();
                    env(tx2, Ter{terNO_RIPPLE});
                    env.close();

                    {
                        // Create MPToken for shares held by Charlie
                        json::Value tx{json::ValueType::Object};
                        tx[sfAccount] = charlie.human();
                        tx[sfMPTokenIssuanceID] =
                            to_string(shares.raw().get<MPTIssue>().getMptID());
                        tx[sfTransactionType] = jss::MPTokenAuthorize;
                        env(tx);
                        env.close();
                    }
                    // Behavioral shift introduced by share inheritance:
                    // before fixCleanup3_2_0 this share Payment succeeded
                    // and the underlying IOU's NoRipple restriction surfaced
                    // only later on Charlie's withdrawal (terNO_RIPPLE).
                    // Post-amendment, canTransfer reads the share's
                    // sfReferenceHolding and dispatches to the underlying IOU;
                    // rippling is disabled between owner and charlie so the
                    // share payment itself is now blocked. tecPATH_DRY is
                    // the path-find layer's translation of the underlying
                    // terNO_RIPPLE under featureMPTokensV2.
                    env(pay(owner, charlie, shares(100)), Ter{tecPATH_DRY});
                    env.close();
                }

                tx = vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(100)});
                env(tx);
                env.close();

                // Delete vault with zero balance
                env(vault.del({.owner = owner, .id = keylet.key}));
            },
            {.charlieRipple = false});

        testCase(
            [&, this](
                Env& env,
                Account const& owner,
                Account const& issuer,
                Account const& charlie,
                auto const& vaultAccount,
                Vault& vault,
                PrettyAsset const& asset,
                auto&&...) {
                testcase("IOU calculation rounding");

                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                tx[sfScale] = 1;
                env(tx);
                env.close();

                auto const startingOwnerBalance = env.balance(owner, asset);
                BEAST_EXPECT((startingOwnerBalance.value() == STAmount{asset, 11875, -2}));

                // This operation (first deposit 100, then 3.75 x 5) is known to
                // have triggered calculation rounding errors in Number
                // (addition and division), causing the last deposit to be
                // blocked by Vault invariants.
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));

                auto const tx1 = vault.deposit(
                    {.depositor = owner, .id = keylet.key, .amount = asset(Number(375, -2))});
                for (auto i = 0; i < 5; ++i)
                {
                    env(tx1);
                }
                env.close();

                {
                    STAmount const xfer{asset, 1185, -1};
                    BEAST_EXPECT(env.balance(owner, asset) == startingOwnerBalance.value() - xfer);
                    BEAST_EXPECT(env.balance(vaultAccount(keylet), asset) == xfer);

                    auto const vault = env.le(keylet);
                    BEAST_EXPECT(vault->at(sfAssetsAvailable) == xfer);
                    BEAST_EXPECT(vault->at(sfAssetsTotal) == xfer);
                }

                // Total vault balance should be 118.5 IOU. Withdraw and delete
                // the vault to verify this exact amount was deposited and the
                // owner has matching shares
                env(vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(Number(1000 + (37 * 5), -1))}));

                {
                    BEAST_EXPECT(env.balance(owner, asset) == startingOwnerBalance.value());
                    BEAST_EXPECT(env.balance(vaultAccount(keylet), asset) == beast::kZero);
                    auto const vault = env.le(keylet);
                    BEAST_EXPECT(vault->at(sfAssetsAvailable) == beast::kZero);
                    BEAST_EXPECT(vault->at(sfAssetsTotal) == beast::kZero);
                }

                env(vault.del({.owner = owner, .id = keylet.key}));
                env.close();
            },
            {.initialIOU = Number(11875, -2)});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env const env{*this, testableAmendments()};
            return {
                env.current()->fees().accountReserve(0).drops() / kDropsPerXrp.drops(),
                env.current()->fees().increment.drops() / kDropsPerXrp.drops()};
        }();

        testCase(
            [&, this](
                Env& env,
                Account const& owner,
                Account const& issuer,
                Account const& charlie,
                auto,
                Vault& vault,
                PrettyAsset const& asset,
                auto&&...) {
                testcase("IOU no trust line to depositor no reserve");
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                // reset limit, so deposit of all funds will delete the trust
                // line
                env.trust(asset(0), owner);
                env.close();

                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(200)}));
                env.close();

                auto trustline = env.le(keylet::line(owner, asset.raw().get<Issue>()));
                BEAST_EXPECT(trustline == nullptr);

                env(ticket::create(owner, 1));
                env.close();

                // Fail because not enough reserve to create trust line
                tx = vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                env(tx, Ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                env(pay(charlie, owner, XRP(incReserve)));
                env.close();

                // Withdraw can now create trust line, will succeed
                env(tx);
                env.close();
            },
            CaseArgs{.initialXRP = acctReserve + (incReserve * 4) + 1});

        testCase(
            [&, this](
                Env& env,
                Account const& owner,
                Account const& issuer,
                Account const& charlie,
                auto,
                Vault& vault,
                PrettyAsset const& asset,
                auto&&...) {
                testcase("IOU no reserve for share MPToken");
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                env(pay(owner, charlie, asset(100)));
                env.close();

                env(ticket::create(charlie, 3));
                env.close();

                // Fail because not enough reserve to create MPToken for shares
                tx = vault.deposit({.depositor = charlie, .id = keylet.key, .amount = asset(100)});
                env(tx, Ter{tecINSUFFICIENT_RESERVE});
                env.close();

                env(pay(issuer, charlie, XRP(incReserve)));
                env.close();

                // Deposit can now create MPToken, will succeed
                env(tx);
                env.close();
            },
            CaseArgs{.initialXRP = acctReserve + (incReserve * 4) + 1});

        testCase([&, this](
                     Env& env,
                     Account const& owner,
                     Account const& issuer,
                     Account const& charlie,
                     auto,
                     Vault& vault,
                     PrettyAsset const& asset,
                     auto&&...) {
            testcase("IOU frozen trust line to 3rd party");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            // Withdraw to 3rd party works
            auto const withdrawToCharlie = [&](xrpl::Keylet keylet) {
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                tx[sfDestination] = charlie.human();
                return tx;
            }(keylet);
            env(withdrawToCharlie);

            // Freeze the 3rd party
            env(trust(issuer, asset(0), charlie, tfSetFreeze));
            env.close();

            // Can withdraw
            auto const withdraw =
                vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
            env(withdraw);
            env.close();

            // Cannot withdraw to 3rd party
            env(withdrawToCharlie, Ter{tecFROZEN});
            env.close();

            env(vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(0)}));
            env.close();

            env(vault.del({.owner = owner, .id = keylet.key}));
            env.close();
        });

        testCase([&, this](
                     Env& env,
                     Account const& owner,
                     Account const& issuer,
                     Account const& charlie,
                     auto,
                     Vault& vault,
                     PrettyAsset const& asset,
                     auto&&...) {
            testcase("IOU global freeze");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            env(fset(issuer, asfGlobalFreeze));
            env.close();

            {
                // Cannot withdraw
                auto tx =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(10)});
                env(tx, Ter{tecFROZEN});

                // Cannot withdraw to 3rd party
                tx[sfDestination] = charlie.human();
                env(tx, Ter{tecFROZEN});
                env.close();

                // Cannot deposit some more
                tx = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(10)});

                env(tx, Ter{tecFROZEN});
            }

            // Clawback is permitted
            env(vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(0)}));
            env.close();

            env(vault.del({.owner = owner, .id = keylet.key}));
            env.close();
        });
    }

    void
    testWithDomainCheck()
    {
        using namespace test::jtx;

        testcase("private vault");

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const charlie{"charlie"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer1{"credIssuer1"};
        Account const credIssuer2{"credIssuer2"};
        std::string const credType = "credential";
        Vault const vault{env};
        env.fund(XRP(1000), issuer, owner, depositor, charlie, pdOwner, credIssuer1, credIssuer2);
        env.close();
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(issuer, asfAllowTrustLineClawback));

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(500)));
        env.trust(asset(1000), depositor);
        env(pay(issuer, depositor, asset(500)));
        env.trust(asset(1000), charlie);
        env(pay(issuer, charlie, asset(5)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
        env(tx);
        env.close();
        BEAST_EXPECT(env.le(keylet));

        {
            testcase("private vault owner can deposit");
            auto tx = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx);
        }

        {
            testcase("private vault depositor not authorized yet");
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
        }

        {
            testcase("private vault cannot set non-existing domain");
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = to_string(BaseUInt<256>(42ul));
            env(tx, Ter{tecOBJECT_NOT_FOUND});
        }

        {
            testcase("private vault set domainId");

            {
                pdomain::Credentials const credentials1{
                    {.issuer = credIssuer1, .credType = credType}};

                env(pdomain::setTx(pdOwner, credentials1));
                auto const domainId1 = [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::Values::None);
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
                    auto tx = env.tx()->getJson(JsonOptions::Values::None);
                    return pdomain::getNewDomain(env.meta());
                }();

                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(domainId);
                env(tx);
                env.close();

                // Should be idempotent
                tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfDomainID] = to_string(domainId);
                env(tx);
                env.close();
            }
        }

        {
            testcase("private vault depositor still not authorized");
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();
        }

        auto const credKeylet = credentials::keylet(depositor, credIssuer1, credType);
        {
            testcase("private vault depositor now authorized");
            env(credentials::create(depositor, credIssuer1, credType));
            env(credentials::accept(depositor, credIssuer1, credType));
            env(credentials::create(charlie, credIssuer1, credType));
            // charlie's credential not accepted
            env.close();
            auto credSle = env.le(credKeylet);
            BEAST_EXPECT(credSle != nullptr);

            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = charlie, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();
        }

        {
            testcase("private vault depositor lost authorization");
            env(credentials::deleteCred(credIssuer1, depositor, credIssuer1, credType));
            env(credentials::deleteCred(credIssuer1, charlie, credIssuer1, credType));
            env.close();
            auto credSle = env.le(credKeylet);
            BEAST_EXPECT(credSle == nullptr);

            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();
        }

        auto const shares = [&env, keylet = keylet, this]() -> Asset {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return MPTIssue(vault->at(sfShareMPTID));
        }();

        {
            testcase("private vault expired authorization");
            uint32_t const closeTime =
                env.current()->header().parentCloseTime.time_since_epoch().count();
            {
                auto tx0 = credentials::create(depositor, credIssuer2, credType);
                tx0[sfExpiration] = closeTime + 20;
                env(tx0);
                tx0 = credentials::create(charlie, credIssuer2, credType);
                tx0[sfExpiration] = closeTime + 20;
                env(tx0);
                env.close();

                env(credentials::accept(depositor, credIssuer2, credType));
                env(credentials::accept(charlie, credIssuer2, credType));
                env.close();
            }

            {
                auto tx1 =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
                env(tx1);
                env.close();

                auto const tokenKeylet =
                    keylet::mptoken(shares.get<MPTIssue>().getMptID(), depositor.id());
                BEAST_EXPECT(env.le(tokenKeylet) != nullptr);
            }

            {
                // time advance
                env.close();
                env.close();
                env.close();

                auto const credsKeylet = credentials::keylet(depositor, credIssuer2, credType);
                BEAST_EXPECT(env.le(credsKeylet) != nullptr);

                auto tx2 =
                    vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1)});
                env(tx2, Ter{tecEXPIRED});
                env.close();

                BEAST_EXPECT(env.le(credsKeylet) == nullptr);
            }

            {
                auto const credsKeylet = credentials::keylet(charlie, credIssuer2, credType);
                BEAST_EXPECT(env.le(credsKeylet) != nullptr);
                auto const tokenKeylet =
                    keylet::mptoken(shares.get<MPTIssue>().getMptID(), charlie.id());
                BEAST_EXPECT(env.le(tokenKeylet) == nullptr);

                auto tx3 =
                    vault.deposit({.depositor = charlie, .id = keylet.key, .amount = asset(2)});
                env(tx3, Ter{tecEXPIRED});

                env.close();
                BEAST_EXPECT(env.le(credsKeylet) == nullptr);
                BEAST_EXPECT(env.le(tokenKeylet) == nullptr);
            }
        }

        {
            testcase("private vault reset domainId");
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfDomainID] = "0";
            env(tx);
            env.close();

            tx = vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
            env.close();

            tx = vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();

            tx = vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = depositor, .amount = asset(0)});
            env(tx);

            tx = vault.clawback(
                {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(0)});
            env(tx);
            env.close();

            tx = vault.del({
                .owner = owner,
                .id = keylet.key,
            });
            env(tx);
        }
    }

    void
    testWithDomainChecXRP()
    {
        using namespace test::jtx;

        testcase("private XRP vault");

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const alice{"charlie"};
        std::string const credType = "credential";
        Vault const vault{env};
        env.fund(XRP(100000), owner, depositor, alice);
        env.close();

        PrettyAsset const asset = xrpIssue();
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset, .flags = tfVaultPrivate});
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
        PrettyAsset const shares{issuanceId};

        {
            testcase("private XRP vault owner can deposit");
            auto tx = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();
        }

        {
            testcase("private XRP vault cannot pay shares to depositor yet");
            env(pay(owner, depositor, shares(1)), Ter{tecNO_AUTH});
        }

        {
            testcase("private XRP vault depositor not authorized yet");
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx, Ter{tecNO_AUTH});
        }

        {
            testcase("private XRP vault set DomainID");
            pdomain::Credentials const credentials{{.issuer = owner, .credType = credType}};

            env(pdomain::setTx(owner, credentials));
            auto const domainId = [&]() {
                auto tx = env.tx()->getJson(JsonOptions::Values::None);
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
            auto tx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(50)});
            env(tx);
            env.close();
        }

        {
            testcase("private XRP vault can pay shares to depositor");
            env(pay(owner, depositor, shares(1)));
        }

        {
            testcase("private XRP vault cannot pay shares to 3rd party");
            json::Value jv;
            jv[sfAccount] = alice.human();
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfMPTokenIssuanceID] = to_string(issuanceId);
            env(jv);
            env.close();

            env(pay(owner, alice, shares(1)), Ter{tecNO_AUTH});
        }
    }

    void
    testFailedPseudoAccount()
    {
        using namespace test::jtx;

        testcase("fail pseudo-account allocation");
        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Vault const vault{env};
        env.fund(XRP(1000), owner);

        auto const keylet = keylet::vault(owner.id(), env.seq(owner));
        for (int i = 0; i < 256; ++i)
        {
            AccountID const accountId = xrpl::pseudoAccountAddress(*env.current(), keylet.key);

            env(pay(env.master.id(), accountId, XRP(1000)),
                Seq(kAutofill),
                Fee(kAutofill),
                Sig(kAutofill));
        }

        auto [tx, keylet1] = vault.create({.owner = owner, .asset = xrpIssue()});
        BEAST_EXPECT(keylet.key == keylet1.key);
        env(tx, Ter{terADDRESS_COLLISION});
    }

    void
    testScaleIOU()
    {
        using namespace test::jtx;

        struct Data
        {
            Account const& owner;
            Account const& issuer;
            Account const& depositor;
            Account const& vaultAccount;
            MPTIssue shares;
            PrettyAsset const& share;
            Vault& vault;
            xrpl::Keylet keylet;
            Issue assets;
            PrettyAsset const& asset;
            std::function<bool(std::function<bool(SLE&, SLE&)>)> peek;
        };

        auto testCase = [&, this](
                            std::uint8_t scale, std::function<void(Env & env, Data data)> test) {
            Env env{*this, testableAmendments()};
            Account const owner{"owner"};
            Account const issuer{"issuer"};
            Account const depositor{"depositor"};
            Vault vault{env};
            env.fund(XRP(1000), issuer, owner, depositor);
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1000), owner);
            env.trust(asset(1000), depositor);
            env(pay(issuer, owner, asset(200)));
            env(pay(issuer, depositor, asset(200)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = scale;
            env(tx);

            auto const [vaultAccount, issuanceId] =
                [&env](xrpl::Keylet keylet) -> std::tuple<Account, MPTID> {
                auto const vault = env.le(keylet);
                return {Account("vault", vault->at(sfAccount)), vault->at(sfShareMPTID)};
            }(keylet);
            MPTIssue const shares(issuanceId);
            env.memoize(vaultAccount);

            auto const peek = [keylet, &env, this](std::function<bool(SLE&, SLE&)> fn) -> bool {
                return env.app().getOpenLedger().modify(
                    [&](OpenView& view, beast::Journal j) -> bool {
                        Sandbox sb(&view, TapNone);
                        auto vault = sb.peek(keylet::vault(keylet.key));
                        if (!BEAST_EXPECT(vault))
                            return false;
                        auto shares = sb.peek(keylet::mptIssuance(vault->at(sfShareMPTID)));
                        if (!BEAST_EXPECT(shares))
                            return false;
                        if (fn(*vault, *shares))
                        {
                            sb.update(vault);
                            sb.update(shares);
                            sb.apply(view);
                            return true;
                        }
                        return false;
                    });
            };

            test(
                env,
                {.owner = owner,
                 .issuer = issuer,
                 .depositor = depositor,
                 .vaultAccount = vaultAccount,
                 .shares = shares,
                 .share = PrettyAsset(shares),
                 .vault = vault,
                 .keylet = keylet,
                 .assets = asset.raw().get<Issue>(),
                 .asset = asset,
                 .peek = peek});
        };

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale deposit overflow on first deposit");
            auto tx = d.vault.deposit(
                {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(10)});
            env(tx, Ter{tecPATH_DRY});
            env.close();
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale deposit overflow on second deposit");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(10)});
                env(tx, Ter{tecPATH_DRY});
                env.close();
            }
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale deposit overflow on total shares");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(5)});
                env(tx, Ter{tecPATH_DRY});
                env.close();
            }
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit exact");

            auto const start = env.balance(d.depositor, d.assets).number();
            auto tx = d.vault.deposit(
                {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(1)});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(10));
            BEAST_EXPECT(env.balance(d.depositor, d.assets) == STAmount(d.asset, start - 1));
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit insignificant amount");

            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(9, -2))});
            env(tx, Ter{tecPRECISION_LOSS});
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit exact, using full precision");

            auto const start = env.balance(d.depositor, d.assets).number();
            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(15, -1))});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(15));
            BEAST_EXPECT(
                env.balance(d.depositor, d.assets) == STAmount(d.asset, start - Number(15, -1)));
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit exact, truncating from .5");

            auto const start = env.balance(d.depositor, d.assets).number();
            // Each of the cases below will transfer exactly 1.2 IOU to the
            // vault and receive 12 shares in exchange
            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(125, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(12));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start - Number(12, -1)));
            }

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(1201, -3))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(24));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start - Number(24, -1)));
            }

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(1299, -3))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(36));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start - Number(36, -1)));
            }
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit exact, truncating from .01");

            auto const start = env.balance(d.depositor, d.assets).number();
            // round to 12
            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(1201, -3))});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(12));
            BEAST_EXPECT(
                env.balance(d.depositor, d.assets) == STAmount(d.asset, start - Number(12, -1)));

            {
                // round to 6
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(69, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(18));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start - Number(18, -1)));
            }
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit exact, truncating from .99");

            auto const start = env.balance(d.depositor, d.assets).number();
            // round to 12
            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(1299, -3))});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(12));
            BEAST_EXPECT(
                env.balance(d.depositor, d.assets) == STAmount(d.asset, start - Number(12, -1)));

            {
                // round to 6
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(62, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(18));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start - Number(18, -1)));
            }
        });

        testCase(1, [&, this](Env& env, Data d) {
            // initial setup: deposit 100 IOU, receive 1000 shares
            auto const start = env.balance(d.depositor, d.assets).number();
            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(100, 0))});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(1000));
            BEAST_EXPECT(
                env.balance(d.depositor, d.assets) == STAmount(d.asset, start - Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.assets) == STAmount(d.asset, Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.shares) == STAmount(d.share, Number(-1000, 0)));

            {
                testcase("Scale redeem exact");
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 100 * 100 / 1000 = 100 * 0.1 = 10

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.share, Number(100, 0))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(900));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) == STAmount(d.asset, start + Number(10, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) == STAmount(d.asset, Number(90, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) == STAmount(d.share, Number(-900, 0)));
            }

            {
                testcase("Scale redeem with rounding");
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 90 * 25 / 900 = 90 * 0.02777... = 2.5

                auto const start = env.balance(d.depositor, d.assets).number();
                d.peek([](SLE& vault, auto&) -> bool {
                    vault[sfAssetsAvailable] = Number(1);
                    return true;
                });

                // Note, this transaction fails first (because of above change
                // in the open ledger) but then succeeds when the ledger is
                // closed (because a modification like above is not persistent),
                // which is why the checks below are expected to pass.
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.share, Number(25, 0))});
                env(tx, Ter{tecINSUFFICIENT_FUNDS});
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(900 - 25));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(25, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(900 - 25, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(900 - 25, 0)));
            }

            {
                testcase("Scale redeem exact");
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 87.5 * 21 / 875 = 87.5 * 0.024 = 2.1

                auto const start = env.balance(d.depositor, d.assets).number();

                tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.share, Number(21, 0))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(875 - 21));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(21, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(875 - 21, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(875 - 21, 0)));
            }

            {
                testcase("Scale redeem rest");
                auto const rest = env.balance(d.depositor, d.shares).number();

                tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.share, rest)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares).number() == 0);
                BEAST_EXPECT(env.balance(d.vaultAccount, d.assets).number() == 0);
                BEAST_EXPECT(env.balance(d.vaultAccount, d.shares).number() == 0);
            }
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale withdraw overflow");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(10, 0))});
                env(tx, Ter{tecPATH_DRY});
                env.close();
            }
        });

        testCase(1, [&, this](Env& env, Data d) {
            // initial setup: deposit 100 IOU, receive 1000 shares
            auto const start = env.balance(d.depositor, d.assets).number();
            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(100, 0))});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(1000));
            BEAST_EXPECT(
                env.balance(d.depositor, d.assets) == STAmount(d.asset, start - Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.assets) == STAmount(d.asset, Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.shares) == STAmount(d.share, Number(-1000, 0)));

            {
                testcase("Scale withdraw exact");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 1000 * 10 / 100 = 1000 * 0.1 = 100
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 100 * 100 / 1000 = 100 * 0.1 = 10

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(10, 0))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(900));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) == STAmount(d.asset, start + Number(10, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) == STAmount(d.asset, Number(90, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) == STAmount(d.share, Number(-900, 0)));
            }

            {
                testcase("Scale withdraw insignificant amount");
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(4, -2))});
                env(tx, Ter{tecPRECISION_LOSS});
            }

            {
                testcase("Scale withdraw with rounding assets");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 900 * 2.5 / 90 = 900 * 0.02777... = 25
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 90 * 25 / 900 = 90 * 0.02777... = 2.5

                auto const start = env.balance(d.depositor, d.assets).number();
                d.peek([](SLE& vault, auto&) -> bool {
                    vault[sfAssetsAvailable] = Number(1);
                    return true;
                });

                // Note, this transaction fails first (because of above change
                // in the open ledger) but then succeeds when the ledger is
                // closed (because a modification like above is not persistent),
                // which is why the checks below are expected to pass.
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(25, -1))});
                env(tx, Ter{tecINSUFFICIENT_FUNDS});
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(900 - 25));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(25, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(900 - 25, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(900 - 25, 0)));
            }

            {
                testcase("Scale withdraw with rounding shares up");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 875 * 3.75 / 87.5 = 875 * 0.042857... = 37.5
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 87.5 * 38 / 875 = 87.5 * 0.043428... = 3.8

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(375, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(875 - 38));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(38, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(875 - 38, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(875 - 38, 0)));
            }

            {
                testcase("Scale withdraw with rounding shares down");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 837 * 3.72 / 83.7 = 837 * 0.04444... = 37.2
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 83.7 * 37 / 837 = 83.7 * 0.044205... = 3.7

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(372, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(837 - 37));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(37, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(837 - 37, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(837 - 37, 0)));
            }

            {
                testcase("Scale withdraw tiny amount");

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(9, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(800 - 1));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) == STAmount(d.asset, start + Number(1, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(800 - 1, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(800 - 1, 0)));
            }

            {
                testcase("Scale withdraw rest");
                auto const rest = env.balance(d.vaultAccount, d.assets).number();

                tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, rest)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares).number() == 0);
                BEAST_EXPECT(env.balance(d.vaultAccount, d.assets).number() == 0);
                BEAST_EXPECT(env.balance(d.vaultAccount, d.shares).number() == 0);
            }
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale clawback overflow");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor, .id = d.keylet.key, .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(10, 0))});
                env(tx, Ter{tecPATH_DRY});
                env.close();
            }
        });

        testCase(1, [&, this](Env& env, Data d) {
            // initial setup: deposit 100 IOU, receive 1000 shares
            auto const start = env.balance(d.depositor, d.assets).number();
            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(100, 0))});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(1000));
            BEAST_EXPECT(
                env.balance(d.depositor, d.assets) == STAmount(d.asset, start - Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.assets) == STAmount(d.asset, Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.shares) == STAmount(d.share, -Number(1000, 0)));
            {
                testcase("Scale clawback exact");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 1000 * 10 / 100 = 1000 * 0.1 = 100
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 100 * 100 / 1000 = 100 * 0.1 = 10

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(10, 0))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(900));
                BEAST_EXPECT(env.balance(d.depositor, d.assets) == STAmount(d.asset, start));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) == STAmount(d.asset, Number(90, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) == STAmount(d.share, -Number(900, 0)));
            }

            {
                testcase("Scale clawback insignificant amount");
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(4, -2))});
                env(tx, Ter{tecPRECISION_LOSS});
            }

            {
                testcase("Scale clawback with rounding assets");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 900 * 2.5 / 90 = 900 * 0.02777... = 25
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 90 * 25 / 900 = 90 * 0.02777... = 2.5

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(25, -1))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(900 - 25));
                BEAST_EXPECT(env.balance(d.depositor, d.assets) == STAmount(d.asset, start));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(900 - 25, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(900 - 25, 0)));
            }

            {
                testcase("Scale clawback with rounding shares up");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 875 * 3.75 / 87.5 = 875 * 0.042857... = 37.5
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 87.5 * 38 / 875 = 87.5 * 0.043428... = 3.8

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(375, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(875 - 38));
                BEAST_EXPECT(env.balance(d.depositor, d.assets) == STAmount(d.asset, start));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(875 - 38, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(875 - 38, 0)));
            }

            {
                testcase("Scale clawback with rounding shares down");
                // assetsToSharesWithdraw:
                //  shares = sharesTotal * (assets / assetsTotal)
                //  shares = 837 * 3.72 / 83.7 = 837 * 0.04444... = 37.2
                // sharesToAssetsWithdraw:
                //  assets = assetsTotal * (shares / sharesTotal)
                //  assets = 83.7 * 37 / 837 = 83.7 * 0.044205... = 3.7

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(372, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(837 - 37));
                BEAST_EXPECT(env.balance(d.depositor, d.assets) == STAmount(d.asset, start));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(837 - 37, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(837 - 37, 0)));
            }

            {
                testcase("Scale clawback tiny amount");

                auto const start = env.balance(d.depositor, d.assets).number();
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(9, -2))});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(800 - 1));
                BEAST_EXPECT(env.balance(d.depositor, d.assets) == STAmount(d.asset, start));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(800 - 1, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(800 - 1, 0)));
            }

            {
                testcase("Scale clawback rest");
                auto const rest = env.balance(d.vaultAccount, d.assets).number();
                d.peek([](SLE& vault, auto&) -> bool {
                    vault[sfAssetsAvailable] = Number(5);
                    return true;
                });

                // Note, this transaction yields two different results:
                // * in the open ledger, with AssetsAvailable = 5
                // * when the ledger is closed with unmodified AssetsAvailable
                //   because a modification like above is not persistent.
                tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, rest)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares).number() == 0);
                BEAST_EXPECT(env.balance(d.vaultAccount, d.assets).number() == 0);
                BEAST_EXPECT(env.balance(d.vaultAccount, d.shares).number() == 0);
            }
        });

        // Non-1:1 ratio (scale=1, 10:1 shares:assets) with an outstanding loan.
        // Deposit 100 IOU → 1000 shares. Borrow 40 → assetsAvailable=60.
        // Clawback 80 IOU → clamped to 60, then share math uses truncation.
        testCase(1, [&, this](Env& env, Data d) {
            using namespace loanBroker;
            using namespace loan;

            testcase("Scale clawback clamped with outstanding loan");

            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(100, 0))});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(1000));

            // Create a loan broker backed by this vault
            auto const brokerKeylet = keylet::loanbroker(d.owner.id(), env.seq(d.owner));
            env(set(d.owner, d.keylet.key));
            env.close();

            // Borrow 40: assetsAvailable=60, assetsTotal=100
            env(set(d.depositor, brokerKeylet.key, STAmount(d.asset, Number(40, 0))),
                loan::kInterestRate(TenthBips32(0)),
                kGracePeriod(60),
                kPaymentInterval(120),
                kPaymentTotal(10),
                Sig(sfCounterpartySignature, d.owner),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(d.keylet);
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == STAmount(d.asset, Number(60, 0)));
                BEAST_EXPECT(sle->at(sfAssetsTotal) == STAmount(d.asset, Number(100, 0)));
            }

            // Request 80 IOU clawback — clamped to assetsAvailable (60)
            // With scale=1 (10:1), 60 assets = 600 shares destroyed
            tx = d.vault.clawback(
                {.issuer = d.issuer,
                 .id = d.keylet.key,
                 .holder = d.depositor,
                 .amount = STAmount(d.asset, Number(80, 0))});
            env(tx, Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(d.keylet);
                BEAST_EXPECT(sle != nullptr);
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == STAmount(d.asset, Number(0, 0)));
                BEAST_EXPECT(sle->at(sfAssetsTotal) == STAmount(d.asset, Number(40, 0)));

                // 600 of 1000 shares destroyed, 400 remain
                BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(400));
            }
        });
    }

    void
    testRPC()
    {
        using namespace test::jtx;

        testcase("RPC");
        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const issuer{"issuer"};
        Vault const vault{env};
        env.fund(XRP(1000), issuer, owner);
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(200)));
        env.close();

        auto const sequence = env.seq(owner);
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        // Set some fields
        {
            auto tx1 = vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(50)});
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
                               json::Value const& vault,
                               json::Value const& issuance = json::ValueType::Null) {
            BEAST_EXPECT(vault.isObject());

            static constexpr auto kCheckString =
                [](auto& node, SField const& field, std::string v) -> bool {
                return node.isMember(field.fieldName) && node[field.fieldName].isString() &&
                    node[field.fieldName] == v;
            };
            static constexpr auto kCheckObject =
                [](auto& node, SField const& field, json::Value v) -> bool {
                return node.isMember(field.fieldName) && node[field.fieldName].isObject() &&
                    node[field.fieldName] == v;
            };
            static constexpr auto kCheckInt = [](auto& node, SField const& field, int v) -> bool {
                return node.isMember(field.fieldName) &&
                    ((node[field.fieldName].isInt() && node[field.fieldName] == json::Int(v)) ||
                     (node[field.fieldName].isUInt() && node[field.fieldName] == json::UInt(v)));
            };

            BEAST_EXPECT(vault["LedgerEntryType"].asString() == "Vault");
            BEAST_EXPECT(vault[jss::index].asString() == strHex(keylet.key));
            BEAST_EXPECT(kCheckInt(vault, sfFlags, 0));
            // Ignore all other standard fields, this test doesn't care

            BEAST_EXPECT(kCheckString(vault, sfAccount, toBase58(sle->at(sfAccount))));
            BEAST_EXPECT(kCheckObject(vault, sfAsset, toJson(sle->at(sfAsset))));
            BEAST_EXPECT(kCheckString(vault, sfAssetsAvailable, "50"));
            BEAST_EXPECT(kCheckString(vault, sfAssetsMaximum, "1000"));
            BEAST_EXPECT(kCheckString(vault, sfAssetsTotal, "50"));
            BEAST_EXPECT(!vault.isMember(sfLossUnrealized.getJsonName()));

            auto const strShareID = strHex(sle->at(sfShareMPTID));
            BEAST_EXPECT(kCheckString(vault, sfShareMPTID, strShareID));
            BEAST_EXPECT(kCheckString(vault, sfOwner, toBase58(owner.id())));
            BEAST_EXPECT(kCheckInt(vault, sfSequence, sequence));
            BEAST_EXPECT(kCheckInt(vault, sfWithdrawalPolicy, kVaultStrategyFirstComeFirstServe));

            if (issuance.isObject())
            {
                BEAST_EXPECT(issuance["LedgerEntryType"].asString() == "MPTokenIssuance");
                BEAST_EXPECT(issuance[jss::mpt_issuance_id].asString() == strShareID);
                BEAST_EXPECT(kCheckInt(issuance, sfSequence, 1));
                BEAST_EXPECT(kCheckInt(
                    issuance, sfFlags, int(lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer)));
                BEAST_EXPECT(kCheckString(issuance, sfOutstandingAmount, "50000000"));
            }
        };

        {
            testcase("RPC ledger_entry selected by key");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = strHex(keylet.key);
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(!jvVault[jss::result].isMember(jss::error));
            BEAST_EXPECT(jvVault[jss::result].isMember(jss::node));
            check(jvVault[jss::result][jss::node]);
        }

        {
            testcase("RPC ledger_entry selected by owner and seq");
            json::Value jvParams;
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
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = to_string(uint256(42));
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC ledger_entry cannot find vault by owner and seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = 1'000'000;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC ledger_entry malformed key");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault] = 42;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry malformed owner");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = 42;
            jvParams[jss::vault][jss::seq] = sequence;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedOwner");
        }

        {
            testcase("RPC ledger_entry malformed seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = "foo";
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry negative seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = -1;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry oversized seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = 1e20;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC ledger_entry bool seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = true;
            auto jvVault = env.rpc("json", "ledger_entry", to_string(jvParams));
            BEAST_EXPECT(jvVault[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC account_objects");

            json::Value jvParams;
            jvParams[jss::account] = owner.human();
            jvParams[jss::type] = jss::vault;
            auto jv = env.rpc("json", "account_objects", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jv[jss::account_objects].size() == 1);
            check(jv[jss::account_objects][0u]);
        }

        {
            testcase("RPC ledger_data");

            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::binary] = false;
            jvParams[jss::type] = jss::vault;
            json::Value jv = env.rpc("json", "ledger_data", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::state].size() == 1);
            check(jv[jss::result][jss::state][0u]);
        }

        {
            testcase("RPC vault_info command line");
            json::Value jv = env.rpc("vault_info", strHex(keylet.key), "validated");

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(jv[jss::result][jss::vault], jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info json");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(jv[jss::result][jss::vault], jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info invalid vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = "foobar";
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid index");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = 0;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json by owner and sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));

            BEAST_EXPECT(!jv[jss::result].isMember(jss::error));
            BEAST_EXPECT(jv[jss::result].isMember(jss::vault));
            check(jv[jss::result][jss::vault], jv[jss::result][jss::vault][jss::shares]);
        }

        {
            testcase("RPC vault_info json malformed sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = "foobar";
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = 0;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json negative sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = -1;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json oversized sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = 1e20;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json bool sequence");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = true;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json malformed owner");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = "foobar";
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination only owner");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination only seq");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination seq vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::seq] = sequence;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json invalid combination owner vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase(
                "RPC vault_info json invalid combination owner seq "
                "vault_id");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = strHex(keylet.key);
            jvParams[jss::seq] = sequence;
            jvParams[jss::owner] = owner.human();
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info json no input");
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info command line invalid index");
            json::Value jv = env.rpc("vault_info", "foobar", "validated");
            BEAST_EXPECT(jv[jss::error].asString() == "invalidParams");
        }

        {
            testcase("RPC vault_info command line invalid index");
            json::Value jv = env.rpc("vault_info", "0", "validated");
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "malformedRequest");
        }

        {
            testcase("RPC vault_info command line invalid index");
            json::Value jv = env.rpc("vault_info", strHex(uint256(42)), "validated");
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "entryNotFound");
        }

        {
            testcase("RPC vault_info command line invalid ledger");
            json::Value jv = env.rpc("vault_info", strHex(keylet.key), "0");
            BEAST_EXPECT(jv[jss::result][jss::error].asString() == "lgrNotFound");
        }
    }

    void
    testVaultClawbackBurnShares()
    {
        using namespace test::jtx;
        using namespace loanBroker;
        using namespace loan;
        Env env(*this, beast::Severity::Warning);

        auto const vaultAssetBalance = [&](Keylet const& vaultKeylet) {
            auto const sleVault = env.le(vaultKeylet);
            BEAST_EXPECT(sleVault != nullptr);

            return std::make_pair(sleVault->at(sfAssetsAvailable), sleVault->at(sfAssetsTotal));
        };

        auto const vaultShareBalance = [&](Keylet const& vaultKeylet) {
            auto const sleVault = env.le(vaultKeylet);
            BEAST_EXPECT(sleVault != nullptr);

            auto const sleIssuance = env.le(keylet::mptIssuance(sleVault->at(sfShareMPTID)));
            BEAST_EXPECT(sleIssuance != nullptr);

            return sleIssuance->at(sfOutstandingAmount);
        };

        auto const setupVault = [&](PrettyAsset const& asset,
                                    Account const& owner,
                                    Account const& depositor) -> std::pair<Vault, Keylet> {
            Vault const vault{env};

            auto const& [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            auto const& vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle != nullptr);

            Asset const share = vaultSle->at(sfShareMPTID);

            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            auto const& [availablePreDefault, totalPreDefault] = vaultAssetBalance(vaultKeylet);
            BEAST_EXPECT(availablePreDefault == totalPreDefault);
            BEAST_EXPECT(availablePreDefault == asset(100).value());

            // attempt to clawback shares while there are assets fails
            env(vault.clawback(
                    {.issuer = owner,
                     .id = vaultKeylet.key,
                     .holder = depositor,
                     .amount = share(0).value()}),
                Ter(tecNO_PERMISSION));
            env.close();

            auto const& sharesAvailable = vaultShareBalance(vaultKeylet);
            auto const& brokerKeylet = keylet::loanbroker(owner.id(), env.seq(owner));

            env(set(owner, vaultKeylet.key));
            env.close();

            auto const& loanKeylet = keylet::loan(brokerKeylet.key, 1);

            // Create a simple Loan for the full amount of Vault assets
            env(set(depositor, brokerKeylet.key, asset(100).value()),
                loan::kInterestRate(TenthBips32(0)),
                kGracePeriod(60),
                kPaymentInterval(120),
                kPaymentTotal(10),
                Sig(sfCounterpartySignature, owner),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            // attempt to clawback shares while there assetsAvailable == 0 and
            // assetsTotal > 0 fails
            env(vault.clawback(
                    {.issuer = owner,
                     .id = vaultKeylet.key,
                     .holder = depositor,
                     .amount = share(0).value()}),
                Ter(tecNO_PERMISSION));
            env.close();

            env.close(std::chrono::seconds{120 + 60});

            env(manage(owner, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));

            auto const& [availablePostDefault, totalPostDefault] = vaultAssetBalance(vaultKeylet);

            BEAST_EXPECT(availablePostDefault == totalPostDefault);
            BEAST_EXPECT(availablePostDefault == asset(0).value());
            BEAST_EXPECT(vaultShareBalance(vaultKeylet) == sharesAvailable);

            return std::make_pair(vault, vaultKeylet);
        };

        auto const testCase = [&](PrettyAsset const& asset,
                                  std::string const& prefix,
                                  Account const& owner,
                                  Account const& depositor) {
            {
                testcase("VaultClawback (share) - " + prefix + " owner asset clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                // when asset is XRP or owner is not issuer clawback fail
                // when owner is issuer precision loss occurs as vault is
                // empty
                auto const expectedTer = [&]() {
                    if (asset.native())
                        return Ter(temMALFORMED);
                    if (asset.raw().getIssuer() != owner.id())
                        return Ter(tecNO_PERMISSION);
                    return Ter(tecPRECISION_LOSS);
                }();
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(100).value(),
                    }),
                    expectedTer);
                env.close();
            }

            {
                testcase(
                    "VaultClawback (share) - " + prefix + " owner incomplete share clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = share(1).value(),
                    }),
                    Ter(tecLIMIT_EXCEEDED));
                env.close();
            }

            {
                testcase(
                    "VaultClawback (share) - " + prefix +
                    " owner implicit complete share clawback");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    // when owner is issuer implicit clawback fails
                    asset.native() || asset.raw().getIssuer() != owner.id() ? Ter(tesSUCCESS)
                                                                            : Ter(tecWRONG_ASSET));
                env.close();
            }

            {
                testcase(
                    "VaultClawback (share) - " + prefix +
                    " owner explicit complete share clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();
            }
            {
                testcase("VaultClawback (share) - " + prefix + " owner can clawback own shares");
                auto [vault, vaultKeylet] = setupVault(asset, owner, owner);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = owner,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();
            }

            {
                testcase("VaultClawback (share) - " + prefix + " empty vault share clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, owner);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = owner,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tesSUCCESS));

                // Now the vault is empty, clawback again fails
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = owner,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tecNO_PERMISSION));
                env.close();
            }
        };

        Account const owner{"alice"};
        Account const depositor{"bob"};
        Account const issuer{"issuer"};

        env.fund(XRP(10000), issuer, owner, depositor);
        env.close();

        // Test XRP
        PrettyAsset const xrp = xrpIssue();
        testCase(xrp, "XRP", owner, depositor);
        testCase(xrp, "XRP (depositor is owner)", owner, owner);

        // Test IOU
        PrettyAsset const iou = issuer["IOU"];
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        env.trust(iou(1000), owner);
        env.trust(iou(1000), depositor);
        env(pay(issuer, owner, iou(100)));
        env(pay(issuer, depositor, iou(100)));
        env.close();
        testCase(iou, "IOU", owner, depositor);
        testCase(iou, "IOU (owner is issuer)", issuer, depositor);

        // Test MPT
        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mpt = mptt.issuanceID();
        mptt.authorize({.account = owner});
        mptt.authorize({.account = depositor});
        env(pay(issuer, owner, mpt(1000)));
        env(pay(issuer, depositor, mpt(1000)));
        env.close();
        testCase(mpt, "MPT", owner, depositor);
        testCase(mpt, "MPT (owner is issuer)", issuer, depositor);
    }

    void
    testVaultClawbackAssets()
    {
        using namespace test::jtx;
        using namespace loanBroker;
        using namespace loan;
        Env env(*this);
        env.enableFeature(fixCleanup3_1_3);

        auto const setupVault = [&](PrettyAsset const& asset,
                                    Account const& owner,
                                    Account const& depositor,
                                    Account const& issuer) -> std::pair<Vault, Keylet> {
            Vault const vault{env};

            auto const& [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            auto const& vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle != nullptr);
            env.memoize(Account("vault", vaultSle->at(sfAccount)));
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            return std::make_pair(vault, vaultKeylet);
        };

        auto const testCase = [&](PrettyAsset const& asset,
                                  std::string const& prefix,
                                  Account const& owner,
                                  Account const& depositor,
                                  Account const& issuer) {
            if (asset.native())
            {
                testcase("VaultClawback (asset) - " + prefix + " issuer XRP clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);
                // If the asset is XRP, clawback with amount fails as malformed
                // when asset is specified.
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                        .amount = asset(1).value(),
                    }),
                    Ter(temMALFORMED));
                // When asset is implicit, clawback fails as no permission.
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                    }),
                    Ter(tecNO_PERMISSION));
                return;
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix + " clawback for different asset fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                Account const issuer2{"issuer2"};
                PrettyAsset const asset2 = issuer2["FOO"];
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset2(1).value(),
                    }),
                    Ter(tecWRONG_ASSET));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " ambiguous owner/issuer asset clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, issuer, depositor, issuer);
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                    }),
                    Ter(tecWRONG_ASSET));
            }

            {
                testcase("VaultClawback (asset) - " + prefix + " non-issuer asset clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tecNO_PERMISSION));

                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(1).value(),
                    }),
                    Ter(tecNO_PERMISSION));
            }

            {
                testcase("VaultClawback (asset) - " + prefix + " issuer clawback from self fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, issuer, issuer);
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                    }),
                    Ter(tecNO_PERMISSION));
            }

            {
                testcase("VaultClawback (asset) - " + prefix + " issuer share clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = share(1).value(),
                    }),
                    Ter(tecNO_PERMISSION));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " partial issuer asset clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(1).value(),
                    }),
                    Ter(tesSUCCESS));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix + " full issuer asset clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(100).value(),
                    }),
                    Ter(tesSUCCESS));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " implicit full issuer asset clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tesSUCCESS));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " zero-amount clawback clamped with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;

                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                // Create a loan broker backed by this vault
                auto const brokerKeylet = keylet::loanbroker(owner.id(), env.seq(owner));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units, reducing assetsAvailable to 60
                // while assetsTotal stays at 100
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                // Zero-amount clawback (= "clawback all") should succeed,
                // clamped to assetsAvailable (60) rather than the full
                // share value (100).
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tesSUCCESS));
                env.close();

                // Only 60 assets clawed back; loan's 40 still outstanding
                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());

                    // 60 of 100 shares destroyed (1:1 ratio), 40 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{4, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " non-zero clawback clamped with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                // Create a loan broker backed by this vault
                auto const brokerKeylet = keylet::loanbroker(owner.id(), env.seq(owner));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                // Request 100 but only 60 available — clamped to 60
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(100).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());

                    // 60 of 100 shares destroyed (1:1 ratio), 40 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{4, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " partial clawback below available with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                // Create a loan broker backed by this vault
                auto const brokerKeylet = keylet::loanbroker(owner.id(), env.seq(owner));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units: assetsAvailable=60, assetsTotal=100
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                // Clawback 30 — well under available (60), no clamping needed
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(30).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(30).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(70).value());

                    // 30 of 100 shares destroyed (1:1 ratio), 70 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{7, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " clawback exactly equal to available with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                auto const brokerKeylet = keylet::loanbroker(owner.id(), env.seq(owner));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units: assetsAvailable=60, assetsTotal=100
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                // Clawback exactly 60 — at the boundary, no clamping needed
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(60).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());

                    // 60 of 100 shares destroyed (1:1 ratio), 40 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{4, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " clawback with zero available (fully borrowed)");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                auto const brokerKeylet = keylet::loanbroker(owner.id(), env.seq(owner));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows all 100 units: assetsAvailable=0, assetsTotal=100
                env(set(depositor, brokerKeylet.key, asset(100).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                auto const sharesBefore = env.balance(depositor, shares);

                // Zero-amount clawback — nothing available, clamped to 0,
                // resulting in zero shares destroyed → tecPRECISION_LOSS
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tecPRECISION_LOSS));
                env.close();

                // Explicit amount clawback — also nothing available
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(50).value(),
                    }),
                    Ter(tecPRECISION_LOSS));
                env.close();

                {
                    // Nothing changed — vault and shares unchanged
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == sharesBefore);
                }
            }
        };

        Account const owner{"alice"};
        Account const depositor{"bob"};
        Account const issuer{"issuer"};

        env.fund(XRP(10000), issuer, owner, depositor);
        env.close();

        // Test XRP
        PrettyAsset const xrp = xrpIssue();
        testCase(xrp, "XRP", owner, depositor, issuer);

        // Test IOU
        PrettyAsset const iou = issuer["IOU"];
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env.trust(iou(2000), owner);
        env.trust(iou(2000), depositor);
        env(pay(issuer, owner, iou(2000)));
        env(pay(issuer, depositor, iou(2000)));
        env.close();
        testCase(iou, "IOU", owner, depositor, issuer);

        // Test MPT
        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});

        PrettyAsset const mpt = mptt.issuanceID();
        mptt.authorize({.account = owner});
        mptt.authorize({.account = depositor});
        env(pay(issuer, depositor, mpt(2000)));
        env.close();
        testCase(mpt, "MPT", owner, depositor, issuer);

        // Test pre-fixCleanup3_1_3 legacy path: zero-amount clawback
        // returns early without clamping to assetsAvailable.
        {
            testcase(
                "VaultClawback (asset) - IOU pre-fixCleanup3_1_3"
                " zero-amount clawback unclamped with outstanding loan");

            env.disableFeature(fixCleanup3_1_3);

            auto [vault, vaultKeylet] = setupVault(iou, owner, depositor, issuer);

            auto const vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle != nullptr);
            if (!vaultSle)
                return;

            PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

            // Create a loan broker backed by this vault
            auto const brokerKeylet = keylet::loanbroker(owner.id(), env.seq(owner));
            env(set(owner, vaultKeylet.key));
            env.close();

            // Depositor borrows 40 units, reducing assetsAvailable to 60
            // while assetsTotal stays at 100
            env(set(depositor, brokerKeylet.key, iou(40).value()),
                loan::kInterestRate(TenthBips32(0)),
                kGracePeriod(60),
                kPaymentInterval(120),
                kPaymentTotal(10),
                Sig(sfCounterpartySignature, owner),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == iou(60).value());
                BEAST_EXPECT(sle->at(sfAssetsTotal) == iou(100).value());
            }

            auto const sharesBefore = env.balance(depositor, shares);

            // Legacy: zero-amount clawback tries to recover the full
            // share value (100) without clamping to assetsAvailable (60).
            // This causes the vault balance to go negative, triggering
            // the sanity check in doApply → tefINTERNAL.
            env(vault.clawback({
                    .issuer = issuer,
                    .id = vaultKeylet.key,
                    .holder = depositor,
                }),
                Ter(tefINTERNAL));
            env.close();

            {
                // Transaction rolled back — vault and shares unchanged
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle != nullptr);
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == iou(60).value());
                BEAST_EXPECT(sle->at(sfAssetsTotal) == iou(100).value());
                auto const sharesAfter = env.balance(depositor, shares);
                BEAST_EXPECT(sharesAfter == sharesBefore);
            }

            env.enableFeature(fixCleanup3_1_3);
        }
    }

    void
    testAssetsMaximum()
    {
        testcase("Assets Maximum");

        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const issuer{"issuer"};

        Vault const vault{env};
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        auto const maxInt64 = std::to_string(std::numeric_limits<std::int64_t>::max());
        BEAST_EXPECT(maxInt64 == "9223372036854775807");

        // Naming things is hard
        auto const maxInt64Plus1 = std::to_string(
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1);
        BEAST_EXPECT(maxInt64Plus1 == "9223372036854775808");

        auto const initialXRP = to_string(kInitialXrp);
        BEAST_EXPECT(initialXRP == "100000000000000000");

        auto const initialXRPPlus1 = to_string(kInitialXrp + 1);
        BEAST_EXPECT(initialXRPPlus1 == "100000000000000001");

        {
            testcase("Assets Maximum: XRP");

            PrettyAsset const xrpAsset = xrpIssue();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpAsset});
            tx[sfData] = "4D65746144617461";

            tx[sfAssetsMaximum] = maxInt64;
            env(tx, Ter(tefEXCEPTION));
            env.close();

            tx[sfAssetsMaximum] = initialXRPPlus1;
            env(tx, Ter(tefEXCEPTION));
            env.close();

            tx[sfAssetsMaximum] = initialXRP;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = maxInt64Plus1;
            env(tx, Ter(tefEXCEPTION));
            env.close();

            // This value will be rounded
            auto const insertAt = maxInt64Plus1.size() - 3;
            auto const decimalTest = maxInt64Plus1.substr(0, insertAt) + "." +
                maxInt64Plus1.substr(insertAt);  // (max int64+1) / 1000
            BEAST_EXPECT(decimalTest == "9223372036854775.808");
            tx[sfAssetsMaximum] = decimalTest;
            auto const newKeylet = keylet::vault(owner.id(), env.seq(owner));
            env(tx);
            env.close();

            auto const vaultSle = env.le(newKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;

            BEAST_EXPECT(vaultSle->at(sfAssetsMaximum) == 9223372036854776);
        }

        {
            testcase("Assets Maximum: MPT");

            PrettyAsset const mptAsset = [&]() {
                MPTTester mptt{env, issuer, kMptInitNoFund};
                mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
                env.close();
                PrettyAsset const mptAsset = mptt["MPT"];
                mptt.authorize({.account = owner});
                env.close();
                return mptAsset;
            }();

            env(pay(issuer, owner, mptAsset(100'000)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = mptAsset});
            tx[sfData] = "4D65746144617461";

            tx[sfAssetsMaximum] = maxInt64;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = initialXRPPlus1;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = initialXRP;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = maxInt64Plus1;
            env(tx, Ter(tefEXCEPTION));
            env.close();

            // This value will be rounded
            auto const insertAt = maxInt64Plus1.size() - 1;
            auto const decimalTest = maxInt64Plus1.substr(0, insertAt) + "." +
                maxInt64Plus1.substr(insertAt);  // (max int64+1) / 10
            BEAST_EXPECT(decimalTest == "922337203685477580.8");
            tx[sfAssetsMaximum] = decimalTest;
            auto const newKeylet = keylet::vault(owner.id(), env.seq(owner));
            env(tx);
            env.close();

            auto const vaultSle = env.le(newKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;

            BEAST_EXPECT(vaultSle->at(sfAssetsMaximum) == 922337203685477581);
        }

        {
            testcase("Assets Maximum: IOU");

            // Almost anything goes with IOUs
            PrettyAsset const iouAsset = issuer["IOU"];
            env.trust(iouAsset(1000), owner);
            env(pay(issuer, owner, iouAsset(200)));
            env.close();

            auto [tx, keylet] = vault.create({.owner = owner, .asset = iouAsset});
            tx[sfData] = "4D65746144617461";

            tx[sfAssetsMaximum] = maxInt64;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = initialXRPPlus1;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = initialXRP;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = maxInt64Plus1;
            env(tx);
            env.close();

            tx[sfAssetsMaximum] = "1000000000000000e80";
            env.close();

            tx[sfAssetsMaximum] = "1000000000000000e-96";
            env.close();

            // These values will be rounded to 15 significant digits
            {
                auto const insertAt = maxInt64Plus1.size() - 1;
                auto const decimalTest = maxInt64Plus1.substr(0, insertAt) + "." +
                    maxInt64Plus1.substr(insertAt);  // (max int64+1) / 10
                BEAST_EXPECT(decimalTest == "922337203685477580.8");
                tx[sfAssetsMaximum] = decimalTest;
                auto const newKeylet = keylet::vault(owner.id(), env.seq(owner));
                env(tx);
                env.close();

                auto const vaultSle = env.le(newKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;

                BEAST_EXPECT(
                    (vaultSle->at(sfAssetsMaximum) ==
                     Number{9223372036854776, 2, Number::Normalized{}}));
            }
            {
                tx[sfAssetsMaximum] = "9223372036854775807e40";  // max int64 * 10^40
                auto const newKeylet = keylet::vault(owner.id(), env.seq(owner));
                env(tx);
                env.close();

                auto const vaultSle = env.le(newKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;

                BEAST_EXPECT(
                    (vaultSle->at(sfAssetsMaximum) ==
                     Number{9223372036854776, 43, Number::Normalized{}}));
            }
            {
                tx[sfAssetsMaximum] = "9223372036854775807e-40";  // max int64 * 10^-40
                auto const newKeylet = keylet::vault(owner.id(), env.seq(owner));
                env(tx);
                env.close();

                auto const vaultSle = env.le(newKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;

                BEAST_EXPECT(
                    (vaultSle->at(sfAssetsMaximum) ==
                     Number{9223372036854776, -37, Number::Normalized{}}));
            }
            {
                tx[sfAssetsMaximum] = "9223372036854775807e-100";  // max int64 * 10^-100
                auto const newKeylet = keylet::vault(owner.id(), env.seq(owner));
                env(tx);
                env.close();

                // Field 'AssetsMaximum' may not be explicitly set to default.
                auto const vaultSle = env.le(newKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;

                BEAST_EXPECT(vaultSle->at(sfAssetsMaximum) == kNumZero);
            }

            // What _can't_ IOUs do?
            // 1. Exceed maximum exponent / offset
            tx[sfAssetsMaximum] = "1000000000000000e81";
            env(tx, Ter(tefEXCEPTION));
            env.close();

            // 2. Mantissa larger than uint64 max
            env.setParseFailureExpected(true);
            try
            {
                tx[sfAssetsMaximum] = "18446744073709551617e5";  // uint64 max + 1
                env(tx);
                BEAST_EXPECTS(false, "Expected parse_error for mantissa larger than uint64 max");
            }
            catch (ParseError const& e)
            {
                using namespace std::string_literals;
                BEAST_EXPECT(
                    e.what() == "invalidParamsField 'tx_json.AssetsMaximum' has invalid data."s);
            }
            env.setParseFailureExpected(false);
        }
    }

    void
    testVaultEscrowedMPT()
    {
        using namespace test::jtx;
        using namespace std::literals;

        // Verify vault deposit/withdraw/clawback respect sfLockedAmount.
        // When MPT tokens are escrowed, sfMPTAmount is reduced and
        // sfLockedAmount is increased. Vault operations go through
        // accountSend/accountHolds which read sfMPTAmount, so escrowed
        // tokens are naturally excluded.

        {
            testcase("Vault deposit fails when MPT asset is escrowed");

            Env env{*this, testableAmendments()};
            auto const baseFee = env.current()->fees().base;
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const issuer{"issuer"};
            Account const bob{"bob"};

            env.fund(XRP(10000), issuer, owner, depositor, bob);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTCanEscrow});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            mptt.authorize({.account = bob});
            PrettyAsset const asset = mptt.issuanceID();
            env(pay(issuer, depositor, asset(100)));
            env.close();

            // Escrow 60 of 100 MPT tokens: sfMPTAmount drops to 40
            auto const escrowSeq = env.seq(depositor);
            env(escrow::create(depositor, bob, asset(60)),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            // Deposit 100 should fail — only 40 spendable
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // Deposit 40 (the unlocked balance) should succeed
            env(vault.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(40)}),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());
            }

            // Clean up escrow
            env(escrow::finish(bob, depositor, escrowSeq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();
        }

        {
            testcase("Vault withdraw respects escrowed shares");

            Env env{*this, testableAmendments()};
            auto const baseFee = env.current()->fees().base;
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const issuer{"issuer"};
            Account const bob{"bob"};

            env.fund(XRP(10000), issuer, owner, depositor, bob);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTCanEscrow});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            PrettyAsset const asset = mptt.issuanceID();
            env(pay(issuer, depositor, asset(100)));
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            // Deposit 100 → get shares
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            auto const vaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            env.memoize(Account("vault", vaultSle->at(sfAccount)));
            PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

            // Authorize bob for share MPT so he can receive escrowed shares
            auto const shareMPTID = vaultSle->at(sfShareMPTID);
            {
                json::Value jv;
                jv[jss::Account] = bob.human();
                jv[sfMPTokenIssuanceID] = to_string(shareMPTID);
                jv[jss::TransactionType] = jss::MPTokenAuthorize;
                env(jv, Ter(tesSUCCESS));
                env.close();
            }

            // Escrow 60% of shares
            auto const escrowAmount = shares(Number{6, vaultSle->at(sfScale) + 1});
            env(escrow::create(depositor, bob, escrowAmount),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            // Withdraw all 100 should fail — only 40% of shares are unlocked
            env(vault.withdraw(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // Withdraw 40 (matching unlocked shares) should succeed
            env(vault.withdraw(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(40)}),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(60).value());
            }
        }

        {
            testcase("Vault clawback only recovers unlocked shares");

            Env env{*this, testableAmendments() | fixCleanup3_1_3};
            auto const baseFee = env.current()->fees().base;
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const issuer{"issuer"};
            Account const bob{"bob"};

            env.fund(XRP(10000), issuer, owner, depositor, bob);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTCanEscrow});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            PrettyAsset const asset = mptt.issuanceID();
            env(pay(issuer, depositor, asset(100)));
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            // Deposit 100 → get shares
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            auto const vaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            env.memoize(Account("vault", vaultSle->at(sfAccount)));
            PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

            // Authorize bob for share MPT so he can receive escrowed shares
            auto const shareMPTID = vaultSle->at(sfShareMPTID);
            {
                json::Value jv;
                jv[jss::Account] = bob.human();
                jv[sfMPTokenIssuanceID] = to_string(shareMPTID);
                jv[jss::TransactionType] = jss::MPTokenAuthorize;
                env(jv, Ter(tesSUCCESS));
                env.close();
            }

            // Escrow 60% of shares
            auto const escrowAmount = shares(Number{6, vaultSle->at(sfScale) + 1});
            env(escrow::create(depositor, bob, escrowAmount),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            // Zero-amount clawback ("all") — should only recover assets
            // corresponding to unlocked shares (40%)
            env(vault.clawback({
                    .issuer = issuer,
                    .id = vaultKeylet.key,
                    .holder = depositor,
                }),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle != nullptr);
                // Only 40 of 100 assets recovered (matching 40% unlocked shares)
                BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(60).value());
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());

                // Depositor's unlocked shares are now 0
                auto const sharesAfter = env.balance(depositor, shares);
                BEAST_EXPECT(sharesAfter == shares(0));
            }
        }
    }

    // Reproduction: canWithdraw IOU limit check bypassed when
    // withdrawal amount is specified in shares (MPT) rather than in assets.
    void
    testBug6LimitBypassWithShares()
    {
        using namespace test::jtx;
        testcase("Bug6 - limit bypass with share-denominated withdrawal");

        auto const allAmendments = testableAmendments() | featureSingleAssetVault;

        for (auto const& features : {allAmendments, allAmendments - fixCleanup3_1_3})
        {
            bool const withFix = features[fixCleanup3_1_3];

            Env env{*this, features};
            Account const owner{"owner"};
            Account const issuer{"issuer"};
            Account const depositor{"depositor"};
            Account const charlie{"charlie"};
            Vault const vault{env};

            env.fund(XRP(1000), issuer, owner, depositor, charlie);
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1000), owner);
            env.trust(asset(1000), depositor);
            env(pay(issuer, owner, asset(200)));
            env(pay(issuer, depositor, asset(200)));
            env.close();

            // Charlie gets a LOW trustline limit of 5
            env.trust(asset(5), charlie);
            env.close();

            auto const [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const depositTx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(100)});
            env(depositTx);
            env.close();

            // Get the share MPT info
            auto const vaultSle = env.le(keylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            auto const mptIssuanceID = vaultSle->at(sfShareMPTID);
            MPTIssue const shares(mptIssuanceID);
            PrettyAsset const share(shares);

            // CONTROL: Withdraw 10 IOU (asset-denominated) to charlie.
            // Charlie's limit is 5, so this should be rejected with tecNO_LINE
            // regardless of the amendment.
            {
                auto withdrawTx =
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(10)});
                withdrawTx[sfDestination] = charlie.human();
                env(withdrawTx, Ter{tecNO_LINE});
                env.close();
            }
            auto const charlieBalanceBefore = env.balance(charlie, asset.raw().get<Issue>());

            // Withdraw the equivalent amount in shares to charlie.
            // Post-fix: rejected (tecNO_LINE) because the share amount is
            //   converted to assets and the trustline limit is checked.
            // Pre-fix: succeeds (tesSUCCESS) because the limit check was
            //   skipped for share-denominated withdrawals.
            {
                auto withdrawTx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = STAmount(share, 10'000'000)});
                withdrawTx[sfDestination] = charlie.human();
                env(withdrawTx, Ter{withFix ? TER{tecNO_LINE} : TER{tesSUCCESS}});
                env.close();

                auto const charlieBalanceAfter = env.balance(charlie, asset.raw().get<Issue>());
                if (withFix)
                {
                    // Post-fix: charlie's balance is unchanged — the withdrawal
                    // was correctly rejected despite being share-denominated.
                    BEAST_EXPECT(charlieBalanceAfter == charlieBalanceBefore);
                }
                else
                {
                    // Pre-fix: charlie received the assets, bypassing the
                    // trustline limit.
                    BEAST_EXPECT(charlieBalanceAfter > charlieBalanceBefore);
                }
            }
        }
    }

    void
    testRemoveEmptyHoldingLockedAmount()
    {
        testcase("removeEmptyHolding deletes MPToken with sfLockedAmount");
        using namespace test::jtx;
        using namespace std::literals;

        auto const amendments = testableAmendments();
        auto runTest = [&](FeatureBitset f) {
            Env env{*this, f};
            auto const baseFee = env.current()->fees().base;

            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const bob{"bob"};

            env.fund(XRP(100000), issuer, owner, depositor, bob);
            env.close();

            Vault const vault{env};

            // Create an MPT asset for the vault
            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1000)));
            env.close();

            // Create vault
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const vaultSle = env.le(keylet);
            BEAST_EXPECT(vaultSle != nullptr);
            auto const shareMptID = vaultSle->at(sfShareMPTID);
            MPTIssue const shareIssue{shareMptID};

            // Depositor deposits 1000 asset units into vault, receiving shares
            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1000)}));
            env.close();

            // Check depositor has shares
            {
                auto const sleMpt = env.le(keylet::mptoken(shareMptID, depositor));
                BEAST_EXPECT(sleMpt != nullptr);
                BEAST_EXPECT(sleMpt->at(sfMPTAmount) == 1000);
            }

            // Escrow 500 of those shares
            env(escrow::create(depositor, bob, STAmount{shareIssue, 500}),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            // Verify: sfMPTAmount=500, sfLockedAmount=500
            {
                auto const sleMpt = env.le(keylet::mptoken(shareMptID, depositor));
                BEAST_EXPECT(sleMpt != nullptr);
                BEAST_EXPECT(sleMpt->at(sfLockedAmount) == 500);
                BEAST_EXPECT(sleMpt->at(sfMPTAmount) == 500);
            }

            // Withdraw remaining spendable shares — triggers removeEmptyHolding
            env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(500)}),
                Ter(tesSUCCESS));
            env.close();

            auto const sleMptAfter = env.le(keylet::mptoken(shareMptID, depositor));
            if (!f[fixCleanup3_1_3])
            {
                // Without the fix, removeEmptyHolding deletes the MPToken
                // even though sfLockedAmount > 0, leaving the escrow's locked
                // amount untracked.
                BEAST_EXPECT(sleMptAfter == nullptr);
            }
            else
            {
                // With the fix, MPToken must still exist with sfLockedAmount > 0
                // and sfMPTAmount == 0 (all spendable shares withdrawn).
                BEAST_EXPECT(sleMptAfter != nullptr);
                if (sleMptAfter)
                {
                    BEAST_EXPECT(sleMptAfter->at(sfLockedAmount) == 500);
                    BEAST_EXPECT(sleMptAfter->at(sfMPTAmount) == 0);
                }
            }
        };

        runTest(amendments - fixCleanup3_1_3);
        runTest(amendments);
    }

    // -----------------------------------------------------------------------
    // Helpers and tests: sole-shareholder / stuck-depositor (XLS-0065 +
    // fixCleanup3_2_0). The vault-level withdraw behavior is tested here;
    // the loan-protocol setup is incidental.
    // -----------------------------------------------------------------------

    FeatureBitset const all_{test::jtx::testableAmendments()};
    std::string const iouCurrency_{"IOU"};

    // design doc:
    //     AssetsAvailable ≈ 3,333.50
    //     AssetsTotal     ≈ 6,666.50  (3,333.50 cash + 3,333 receivable)
    //     LossUnrealized  =  3,333
    //     OutstandingShares = sharesLender   (5e9 at IOU scale 1e6)
    struct StuckDepositorFixture
    {
        test::jtx::Account issuer{"issuer"};
        test::jtx::Account lender{"lender"};
        test::jtx::Account bob{"bob"};
        test::jtx::Account borrower{"borrower"};
        std::optional<PrettyAsset> asset;
        std::optional<Keylet> vaultKeylet;
        uint256 brokerID;
        std::optional<Keylet> loanKeylet;
        MPTID shareAsset;
        std::uint64_t sharesLender = 0;
    };

    static constexpr std::int64_t kStuckFunding = 1'000'000;
    static constexpr std::int64_t kStuckDepositorIOU = 1'000'000;
    static constexpr std::int64_t kStuckBorrowerIOU = 100'000;
    static constexpr std::int64_t kStuckDeposit = 5'000;
    static constexpr std::int64_t kStuckPrincipal = 3'333;
    static constexpr std::uint32_t kStuckPayInterval = 600;
    static constexpr std::uint32_t kStuckPayTotal = 2;

    [[nodiscard]] StuckDepositorFixture
    setupStuckDepositor(test::jtx::Env& env)
    {
        using namespace test::jtx;

        StuckDepositorFixture f;
        f.asset = f.issuer[iouCurrency_];

        env.fund(XRP(kStuckFunding), f.issuer, f.lender, f.bob, f.borrower);
        env.close();

        env(trust(f.lender, (*f.asset)(10'000'000)));
        env(trust(f.bob, (*f.asset)(10'000'000)));
        env(trust(f.borrower, (*f.asset)(10'000'000)));
        env.close();

        env(pay(f.issuer, f.lender, (*f.asset)(kStuckDepositorIOU)));
        env(pay(f.issuer, f.bob, (*f.asset)(kStuckDepositorIOU)));
        env(pay(f.issuer, f.borrower, (*f.asset)(kStuckBorrowerIOU)));
        env.close();

        // Vault: Lender creates and seeds it; Bob matches the deposit for a
        // clean 50/50 split.
        Vault const v{env};
        auto [createTx, vaultKeylet] = v.create({.owner = f.lender, .asset = *f.asset});
        env(createTx);
        env.close();
        if (!BEAST_EXPECT(env.le(vaultKeylet)))
            return f;
        f.vaultKeylet = vaultKeylet;

        env(v.deposit({
                .depositor = f.lender,
                .id = vaultKeylet.key,
                .amount = (*f.asset)(kStuckDeposit),
            }),
            Ter(tesSUCCESS));
        env(v.deposit({
                .depositor = f.bob,
                .id = vaultKeylet.key,
                .amount = (*f.asset)(kStuckDeposit),
            }),
            Ter(tesSUCCESS));
        env.close();

        // Loan broker: no cover, no management fee, debt cap 10x principal.
        f.brokerID = keylet::loanbroker(f.lender.id(), env.seq(f.lender)).key;
        {
            using namespace loanBroker;
            env(set(f.lender, vaultKeylet.key),
                kDebtMaximum((*f.asset)(kStuckPrincipal * 10).value()));
            env.close();
        }

        // Loan: 3,333 USD principal, impaired immediately.
        auto const sleBroker = env.le(keylet::loanbroker(f.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return f;
        f.loanKeylet = keylet::loan(f.brokerID, sleBroker->at(sfLoanSequence));

        {
            using namespace loan;
            env(set(f.borrower, f.brokerID, kStuckPrincipal),
                Sig(sfCounterpartySignature, f.lender),
                kPaymentTotal(kStuckPayTotal),
                kPaymentInterval(kStuckPayInterval),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();
            env(manage(f.lender, f.loanKeylet->key, tfLoanImpair), Ter(tesSUCCESS));
            env.close();
        }

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return f;
        BEAST_EXPECT(vaultSle->at(sfLossUnrealized) == (*f.asset)(kStuckPrincipal).value());

        f.shareAsset = vaultSle->at(sfShareMPTID);

        auto const tokenBob = env.le(keylet::mptoken(f.shareAsset, f.bob.id()));
        if (!BEAST_EXPECT(tokenBob))
            return f;
        std::uint64_t const sharesBob = tokenBob->getFieldU64(sfMPTAmount);

        // Bob (non-sole) exits at the discounted rate. Always succeeds.
        STAmount const bobShareAmt{MPTIssue{f.shareAsset}, Number(sharesBob)};
        env(v.withdraw({
                .depositor = f.bob,
                .id = vaultKeylet.key,
                .amount = bobShareAmt,
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const tokenLender = env.le(keylet::mptoken(f.shareAsset, f.lender.id()));
        if (!BEAST_EXPECT(tokenLender))
            return f;
        f.sharesLender = tokenLender->getFieldU64(sfMPTAmount);

        auto const sleIssuance = env.le(keylet::mptIssuance(f.shareAsset));
        if (!BEAST_EXPECT(sleIssuance))
            return f;
        BEAST_EXPECT(sleIssuance->getFieldU64(sfOutstandingAmount) == f.sharesLender);

        auto const vaultAfterBob = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultAfterBob))
            return f;
        // After Bob's exit: loss is unchanged (3,333 receivable), and the
        // gap between assetsTotal and assetsAvailable equals exactly that
        // receivable.
        BEAST_EXPECT(vaultAfterBob->at(sfLossUnrealized) == (*f.asset)(kStuckPrincipal).value());
        BEAST_EXPECT(
            vaultAfterBob->at(sfAssetsTotal) - vaultAfterBob->at(sfAssetsAvailable) ==
            vaultAfterBob->at(sfLossUnrealized));

        return f;
    }

    // Reproduces the worked example from the XLS-0065 design doc. The sole
    // remaining shareholder asks (via fixed-asset input) for the vault's
    // entire AssetsAvailable. Pre-fix this fails with the zero-sized-vault
    // invariant violation. Post-fix the full-price exchange rate burns
    // only a portion of the shares, the depositor receives all of
    // AssetsAvailable, and the residual shares remain backed by the
    // impaired-loan receivable.
    void
    testWithdrawSoleShareholderFixedAssetExit(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_2_0];
        testcase(
            std::string{"Vault withdraw: sole shareholder exits via "
                        "fixed-asset amount with impaired loan"} +
            (withFix ? " (fixCleanup3_2_0)" : " (pre-fix)"));

        std::string logs;
        Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || !f.asset || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;
        PrettyAsset const& asset = *f.asset;

        auto const vaultBefore = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        Number const availableBefore = vaultBefore->at(sfAssetsAvailable);
        Number const totalBefore = vaultBefore->at(sfAssetsTotal);
        Number const lossBefore = vaultBefore->at(sfLossUnrealized);

        STAmount const lenderBalanceBefore = env.balance(f.lender, asset);

        // The requested amount differs between feature regimes because
        // the two regimes are testing different behaviors:
        //
        // - Pre-fix: request the full AssetsAvailable (3,333.50). Under
        //   the discounted formula this would burn every outstanding
        //   share, hitting the zero-sized-vault invariant. The
        //   transaction is rejected with tecINVARIANT_FAILED — the
        //   stuck-depositor bug.
        //
        // - Post-fix: request a strictly smaller amount (1,000 USD).
        //   The full-price formula burns only ~30% of the outstanding
        //   shares; the vault retains the rest, backed by the impaired
        //   receivable. Requesting *exactly* AssetsAvailable post-fix
        //   would currently fail with tecINSUFFICIENT_FUNDS due to the
        //   round-to-nearest used by assetsToSharesWithdraw (the
        //   recomputed payout can overshoot the request by a few ULPs).
        //   The "force payout to AssetsAvailable" branch in doApply
        //   only triggers when every share is burned, which is covered
        //   by the loan-repayment test.
        STAmount const requestAssets =
            withFix ? asset(1000).value() : STAmount{asset.raw(), availableBefore};
        Vault const v{env};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = requestAssets,
            }),
            Ter(withFix ? TER{tesSUCCESS} : TER{tecINVARIANT_FAILED}));
        env.close();

        auto const vaultAfter = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        auto const issuanceAfter = env.le(keylet::mptIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceAfter))
            return;

        std::uint64_t const sharesAfter = issuanceAfter->getFieldU64(sfOutstandingAmount);
        Number const availableAfter = vaultAfter->at(sfAssetsAvailable);
        Number const totalAfter = vaultAfter->at(sfAssetsTotal);
        Number const lossAfter = vaultAfter->at(sfLossUnrealized);

        if (!withFix)
        {
            // Pre-fix: rejected — vault state unchanged.
            BEAST_EXPECT(sharesAfter == f.sharesLender);
            BEAST_EXPECT(availableAfter == availableBefore);
            BEAST_EXPECT(totalAfter == totalBefore);
            BEAST_EXPECT(lossAfter == lossBefore);
            return;
        }

        // Post-fix exact-value derivation (fixture: sharesLender=5e9,
        // totalBefore=6666.5, request=1000):
        //   sharesRedeemed = round(sharesLender * request / totalBefore)
        //                  = round(750,018,750.469) = 750,018,750
        //   received       = totalBefore * sharesRedeemed / sharesLender
        //                  = 999.999999375  (slightly under 1,000 due to
        //                                    integer-share rounding)
        constexpr std::uint64_t kExpectedSharesRedeemed = 750'018'750;
        Number const expectedReceived =
            totalBefore * Number(kExpectedSharesRedeemed) / Number(f.sharesLender);

        BEAST_EXPECT(sharesAfter == f.sharesLender - kExpectedSharesRedeemed);

        // LossUnrealized is unchanged: the loan-protocol side is untouched.
        BEAST_EXPECT(lossAfter == lossBefore);

        // The entire (total - available) gap is the impaired receivable,
        // i.e. equal to lossUnrealized.
        BEAST_EXPECT(totalAfter - availableAfter == lossAfter);

        STAmount const lenderBalanceAfter = env.balance(f.lender, asset);
        Number const received{lenderBalanceAfter - lenderBalanceBefore};
        BEAST_EXPECT(received == expectedReceived);

        // Conservation: assets removed from the vault equal what the
        // depositor received.
        BEAST_EXPECT(totalBefore - totalAfter == received);
        BEAST_EXPECT(availableBefore - availableAfter == received);
    }

    // Sole shareholder attempts to burn ALL outstanding shares via
    // fixed-shares input while the vault still holds an impaired
    // receivable. Pre-fix this fails with the zero-sized-vault invariant
    // violation. Post-fix the full-price rate causes assetsWithdrawn to
    // equal assetsTotal, which exceeds assetsAvailable, so the transaction
    // is rejected with tecINSUFFICIENT_FUNDS.
    void
    testWithdrawSoleShareholderFullSharesRejected(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_2_0];
        testcase(
            std::string{"Vault withdraw: sole shareholder full-shares "
                        "burn is rejected while loss outstanding"} +
            (withFix ? " (fixCleanup3_2_0)" : " (pre-fix)"));

        std::string logs;
        Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;

        auto const vaultBefore = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        Number const availableBefore = vaultBefore->at(sfAssetsAvailable);
        Number const totalBefore = vaultBefore->at(sfAssetsTotal);
        Number const lossBefore = vaultBefore->at(sfLossUnrealized);

        // Fixed-shares input: ask for ALL outstanding shares.
        STAmount const shareAmt{MPTIssue{f.shareAsset}, Number(f.sharesLender)};
        Vault const v{env};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = shareAmt,
            }),
            Ter(withFix ? TER{tecINSUFFICIENT_FUNDS} : TER{tecINVARIANT_FAILED}));
        env.close();

        // Either way the transaction was rejected; vault state unchanged.
        auto const vaultAfter = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        auto const issuanceAfter = env.le(keylet::mptIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceAfter))
            return;
        BEAST_EXPECT(issuanceAfter->getFieldU64(sfOutstandingAmount) == f.sharesLender);
        BEAST_EXPECT(vaultAfter->at(sfAssetsAvailable) == availableBefore);
        BEAST_EXPECT(vaultAfter->at(sfAssetsTotal) == totalBefore);
        BEAST_EXPECT(vaultAfter->at(sfLossUnrealized) == lossBefore);
    }

    // Post-fix end-to-end resolution: after the sole-shareholder partial
    // exit, the loan is repaid in full. With unrealized loss cleared and
    // all assets back as cash, the depositor can burn all remaining
    // shares and fully exit the vault. The final withdrawal hits the
    // "force payout to assetsAvailable" branch in doApply.
    void
    testWithdrawSoleShareholderLoanRepaymentExit()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Vault withdraw: sole shareholder fully exits after impaired "
            "loan is repaid (fixCleanup3_2_0)");

        Env env(*this, all_ | fixCleanup3_2_0);
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || !f.asset || !f.loanKeylet || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;
        Keylet const& loanKey = *f.loanKeylet;
        PrettyAsset const& asset = *f.asset;

        Vault const v{env};

        // Sole-shareholder partial exit (see comment in
        // testWithdrawSoleShareholderFixedAssetExit for why we request
        // less than full AssetsAvailable).
        {
            STAmount const requestAssets = asset(1000).value();
            env(v.withdraw({
                    .depositor = f.lender,
                    .id = vaultKey.key,
                    .amount = requestAssets,
                }),
                Ter(tesSUCCESS));
            env.close();
        }

        // Confirm the "dormant-but-alive" state from the design doc. The
        // partial exit burned exactly 750,018,750 shares (see derivation
        // in testWithdrawSoleShareholderFixedAssetExit).
        auto const tokenAfterExit = env.le(keylet::mptoken(f.shareAsset, f.lender.id()));
        if (!BEAST_EXPECT(tokenAfterExit))
            return;
        std::uint64_t const retainedShares = tokenAfterExit->getFieldU64(sfMPTAmount);
        BEAST_EXPECT(retainedShares == f.sharesLender - 750'018'750);

        // Borrower repays the loan in full (pays more than the outstanding
        // total; the loan transactor caps the receivable).
        env(pay(f.borrower, loanKey.key, asset(kStuckPrincipal * 2)), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfterRepay = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfterRepay))
            return;
        // Repayment converts the 3,333 receivable back to cash; assetsTotal
        // is unchanged but assetsAvailable jumps by exactly the same amount,
        // and lossUnrealized clears to zero.
        BEAST_EXPECT(vaultAfterRepay->at(sfLossUnrealized) == beast::kZero);
        BEAST_EXPECT(vaultAfterRepay->at(sfAssetsAvailable) == vaultAfterRepay->at(sfAssetsTotal));

        STAmount const lenderBalanceBeforeFinal = env.balance(f.lender, asset);
        Number const availableBeforeFinal = vaultAfterRepay->at(sfAssetsAvailable);

        // Burn all remaining shares — the clean-state preconditions of
        // the "final withdrawal" guard are now satisfied.
        STAmount const allShares{MPTIssue{f.shareAsset}, Number(retainedShares)};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = allShares,
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultFinal = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultFinal))
            return;
        auto const issuanceFinal = env.le(keylet::mptIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceFinal))
            return;

        // Zero-sized vault invariant satisfied: 0 shares, 0 assets.
        BEAST_EXPECT(issuanceFinal->getFieldU64(sfOutstandingAmount) == 0);
        BEAST_EXPECT(vaultFinal->at(sfAssetsTotal) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfAssetsAvailable) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfLossUnrealized) == beast::kZero);

        // The final payout equals exactly the AssetsAvailable that
        // existed before the call (the "force payout" branch).
        STAmount const lenderBalanceAfter = env.balance(f.lender, asset);
        Number const finalReceived{lenderBalanceAfter - lenderBalanceBeforeFinal};
        BEAST_EXPECT(finalReceived == availableBeforeFinal);
    }

    // Clean-state regression: with no impaired loan, a sole shareholder
    // burning all their shares fully empties the vault under both the
    // pre-fix and post-fix code paths. Confirms the new logic doesn't
    // break the existing happy-path close-out.
    void
    testWithdrawSoleShareholderCleanVaultUnaffected(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_2_0];
        testcase(
            std::string{"Vault withdraw: sole shareholder clean-state "
                        "close-out unchanged"} +
            (withFix ? " (fixCleanup3_2_0)" : " (pre-fix)"));

        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};

        env.fund(XRP(kStuckFunding), issuer, lender);
        env.close();

        PrettyAsset const asset = issuer[iouCurrency_];
        env(trust(lender, asset(10'000'000)));
        env.close();
        env(pay(issuer, lender, asset(kStuckDepositorIOU)));
        env.close();

        // Sole shareholder of a clean vault — no loan broker needed.
        Vault const v{env};
        auto [createTx, vaultKeylet] = v.create({.owner = lender, .asset = asset});
        env(createTx);
        env.close();

        env(v.deposit({
                .depositor = lender,
                .id = vaultKeylet.key,
                .amount = asset(kStuckDeposit),
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        auto const shareAsset = vaultBefore->at(sfShareMPTID);
        auto const tokenLender = env.le(keylet::mptoken(shareAsset, lender.id()));
        if (!BEAST_EXPECT(tokenLender))
            return;
        std::uint64_t const sharesLender = tokenLender->getFieldU64(sfMPTAmount);

        // Sole shareholder, no loans, no loss. Burn everything.
        STAmount const allShares{MPTIssue{shareAsset}, Number(sharesLender)};
        env(v.withdraw({
                .depositor = lender,
                .id = vaultKeylet.key,
                .amount = allShares,
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultFinal = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultFinal))
            return;
        auto const issuanceFinal = env.le(keylet::mptIssuance(shareAsset));
        if (!BEAST_EXPECT(issuanceFinal))
            return;
        BEAST_EXPECT(issuanceFinal->getFieldU64(sfOutstandingAmount) == 0);
        BEAST_EXPECT(vaultFinal->at(sfAssetsTotal) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfAssetsAvailable) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfLossUnrealized) == beast::kZero);

        // (Pre-fix path takes the regular code path; post-fix path enters
        // the new final-withdrawal guard, which forces payout to exactly
        // assetsAvailable. Either way the result is identical for a clean
        // vault.)
        (void)withFix;
    }

    // Sole shareholder in an impaired vault redeems a *partial* count of
    // shares via fixed-shares input. Pre-fix the discounted formula is
    // used; post-fix the full-price formula is used (waiveUnrealizedLoss
    // = Yes). The relative payout therefore differs, and post-fix the
    // depositor recovers proportionally more of the residual cash for
    // the shares burned. In both cases the vault is left in a valid
    // (non-empty) state.
    void
    testWithdrawSoleShareholderPartialFixedSharesUsesFullPrice()
    {
        using namespace test::jtx;

        testcase(
            "Vault withdraw: sole-shareholder partial fixed-shares uses "
            "full-price rate (fixCleanup3_2_0)");

        Env env(*this, all_ | fixCleanup3_2_0);
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || !f.asset || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;
        PrettyAsset const& asset = *f.asset;

        auto const vaultBefore = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        Number const totalBefore = vaultBefore->at(sfAssetsTotal);
        Number const availableBefore = vaultBefore->at(sfAssetsAvailable);
        Number const lossBefore = vaultBefore->at(sfLossUnrealized);

        // Burn exactly half of the outstanding shares.
        std::uint64_t const halfShares = f.sharesLender / 2;
        STAmount const halfAmt{MPTIssue{f.shareAsset}, Number(halfShares)};

        STAmount const lenderBalanceBefore = env.balance(f.lender, asset);

        Vault const v{env};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = halfAmt,
            }),
            Ter(tesSUCCESS));
        env.close();

        // Expected payout under the full-price formula:
        //   assets = totalBefore * halfShares / sharesLender
        // which (with halfShares == sharesLender/2) is roughly
        //   totalBefore / 2.
        STAmount const lenderBalanceAfter = env.balance(f.lender, asset);
        Number const received{lenderBalanceAfter - lenderBalanceBefore};
        Number const expected = totalBefore * Number(halfShares) / Number(f.sharesLender);
        BEAST_EXPECT(received == expected);

        // The full-price payout exceeds the discounted formula by exactly
        // lossBefore * halfShares / sharesLender — that's the whole point
        // of the waive.
        Number const discounted =
            (totalBefore - lossBefore) * Number(halfShares) / Number(f.sharesLender);
        Number const expectedDelta = lossBefore * Number(halfShares) / Number(f.sharesLender);
        BEAST_EXPECT(received - discounted == expectedDelta);

        auto const vaultAfter = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        auto const issuanceAfter = env.le(keylet::mptIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceAfter))
            return;

        // Vault remains valid: half the shares remain, lossUnrealized
        // is untouched, and the entire (total - available) gap is still
        // the impaired receivable.
        BEAST_EXPECT(
            issuanceAfter->getFieldU64(sfOutstandingAmount) == f.sharesLender - halfShares);
        BEAST_EXPECT(vaultAfter->at(sfAssetsTotal) == totalBefore - received);
        BEAST_EXPECT(vaultAfter->at(sfLossUnrealized) == lossBefore);
        BEAST_EXPECT(
            vaultAfter->at(sfAssetsTotal) - vaultAfter->at(sfAssetsAvailable) ==
            vaultAfter->at(sfLossUnrealized));

        // Conservation: vault delta matches the depositor's gain.
        BEAST_EXPECT(totalBefore - vaultAfter->at(sfAssetsTotal) == received);
        BEAST_EXPECT(availableBefore - vaultAfter->at(sfAssetsAvailable) == received);
    }

    // Bug: DeltaInfo::makeDelta uses max(scale(after), scale(before)) for the
    // sfAssetsTotal and sfAssetsAvailable deltas, and visitEntry applies the
    // same max() for the vault pseudo-account RippleState.  When
    // sfAssetsTotal sits exactly at 1e16 (IOU exponent 1, ULP = 10) and a
    // withdrawal of 5 USD brings it to 9.999...995e15 (IOU exponent 0,
    // ULP = 1), all three computations pick the anterior coarser scale 1.
    // roundToAsset(-5, scale=1) collapses to 0, so the invariant check
    // vaultPseudoDeltaAssets >= kZero fires even though the state change is
    // valid and fully consistent at IOU precision.
    //
    // Fix (fixCleanup3_2_0): finalize compares the vault pseudo-account and
    // sfAssetsTotal/Available deltas directly in Number space, bypassing
    // scale-coarsened rounding.
    void
    testBugMakeDeltaAnteriorScale()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};

            env.fund(XRP(100'000), issuer, alice);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            // Trust limit of 2e16, fund exactly 1e16 so deposit lands at the
            // IOU scale-1 boundary (exponent 1, ULP = 10).
            STAmount const fundAndDeposit{usd.raw(), Number{1, 16}};

            env(trust(alice, STAmount{usd.raw(), 2, 16}));
            env.close();
            env(pay(issuer, alice, fundAndDeposit));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            // sfAssetsTotal = sfAssetsAvailable = 1e16 (exponent 1, ULP = 10).
            env(vault.deposit(
                {.depositor = alice, .id = vaultKeylet.key, .amount = fundAndDeposit}));
            env.close();

            // Withdraw 5 USD: -5 is sub-ULP at the anterior scale (ULP = 10)
            // but exact at the posterior scale (ULP = 1).  The state change is
            // consistent; only the invariant's scale selection is wrong.
            env(vault.withdraw({.depositor = alice, .id = vaultKeylet.key, .amount = usd(5)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultWithdraw across IOU scale boundary fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw across IOU scale boundary succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tesSUCCESS);
        }
    }

    // Bug: DeltaInfo::makeDelta uses max(scale(after), scale(before)) for
    // sfAssetsTotal/Available deltas.  This is symmetric to
    // testBugMakeDeltaAnteriorScale but in the opposite direction: a deposit
    // pushes assetsTotal from just below 1e16 (IOU exponent 0, ULP = 1) to just
    // above it (exponent 1, ULP = 10).  makeDelta picks the coarser *posterior*
    // scale 1.  The trust line balance rounds from atEdge + 2 = 10,000,000,000,000,001
    // → 1e16, so the pseudo-account delta is only +1 in IOU space.
    // roundToAsset(+1, scale=1) = 0 fires "deposit must increase vault balance"
    // even though the state change is consistent at every precision boundary.
    //
    // Fix (fixCleanup3_2_0): computeVaultMinScale uses the posterior Number-space
    // scale of sfAssetsTotal (which retains the full value 10,000,000,000,000,001,
    // exponent 0), giving minScale = 0.  roundToAsset(+1, scale=0) = 1 > 0 and
    // the invariant passes.  However the transactor's own precision guard fires
    // first (bob pays 2 USD, vault receives only 1 due to IOU rounding), so the
    // post-amendment result is tecPRECISION_LOSS rather than tesSUCCESS —
    // the depositor is protected from silently losing 1 USD to rounding.
    void
    testBugMakeDeltaPosteriorScale()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            // atEdge is the largest IOU value with exponent 0 (ULP = 1).
            // A deposit of 2 USD brings assetsTotal to 10,000,000,000,000,001
            // in Number space, crossing the 1e16 boundary in IOU space.
            STAmount const atEdge{usd.raw(), Number{9'999'999'999'999'999LL}};

            env(trust(alice, STAmount{usd.raw(), 2, 16}));
            env(trust(bob, usd(100)));
            env.close();
            env(pay(issuer, alice, atEdge));
            env(pay(issuer, bob, usd(2)));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            // sfAssetsTotal = sfAssetsAvailable = atEdge (exponent 0, ULP = 1)
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = atEdge}));
            env.close();

            // Deposit 2 USD: +2 is sub-ULP at the posterior IOU scale (ULP = 10)
            // but exact at the Number scale retained by sfAssetsTotal.
            env(vault.deposit({.depositor = bob, .id = vaultKeylet.key, .amount = usd(2)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultDeposit across IOU scale boundary fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit across IOU scale boundary succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tecPRECISION_LOSS);
        }
    }

    // Bug: ValidVault::visitEntry computes destinationDelta.scale as
    // max(before_exponent, after_exponent) for RippleState entries.  When a
    // withdrawal credits a destination whose IOU balance sits just below a
    // power-of-10 boundary (atEdge = 9'999'999'999'999'999), the post-credit
    // STAmount rounds up one exponent (exponent 0 → 1), making
    // destinationDelta.scale = 1.  The invariant then calls
    // roundToAsset(+2 USD, scale=1) = 0 and incorrectly fires
    // "withdrawal must increase destination balance".
    //
    // Fix (fixCleanup3_2_0): finalize compares destination delta directly in
    // Number space, bypassing scale-coarsened rounding.  The transaction
    // itself succeeds because the effective IOU credit is non-trivial at
    // Number precision even though the STAmount exponent shifted.
    void
    testVaultWithdrawCanonicalizeToZero()
    {
        using namespace test::jtx;

        enum class DestKind : bool { ThirdParty = false, Self = true };

        auto runScenario = [this](FeatureBitset features, DestKind destKind, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            STAmount const aliceLimit{usd.raw(), 2, 16};
            STAmount const bobLimit{usd.raw(), 2, 16};
            STAmount const atEdge{usd.raw(), Number{9'999'999'999'999'999LL}};

            env(trust(alice, aliceLimit));
            if (destKind == DestKind::ThirdParty)
                env(trust(bob, bobLimit));
            env.close();

            env(pay(issuer, alice, usd(1'000)));
            if (destKind == DestKind::ThirdParty)
                env(pay(issuer, bob, atEdge));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = usd(1'000)}));
            env.close();

            // For the self-destination case, push alice's own trust line to
            // the IOU edge so the next withdraw inflow crosses the boundary.
            if (destKind == DestKind::Self)
            {
                env(pay(issuer, alice, atEdge));
                env.close();
            }

            auto tx = vault.withdraw({.depositor = alice, .id = vaultKeylet.key, .amount = usd(2)});
            if (destKind == DestKind::ThirdParty)
                tx[sfDestination] = bob.human();
            env(tx, Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultWithdraw to third-party at IOU edge fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(
                testableAmendments() - fixCleanup3_2_0, DestKind::ThirdParty, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw to third-party at IOU edge succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), DestKind::ThirdParty, tesSUCCESS);
        }
        {
            testcase(
                "bug: VaultWithdraw to self at IOU edge fires invariant "
                "(pre-fixCleanup3_2_0)");
            runScenario(
                testableAmendments() - fixCleanup3_2_0, DestKind::Self, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw to self at IOU edge succeeds "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), DestKind::Self, tesSUCCESS);
        }
    }

    // Bug: the equality check (vault outflow == destination inflow) was
    // skipped whenever the destination delta rounded to zero at localMinScale,
    // including cases where the vault outflow rounded to a non-zero value and
    // a representable amount of value was genuinely destroyed.
    //
    // Scenario: Bob's IOU balance sits 5 units below the 10^16 STAmount
    // precision boundary (atEdge2 = 9,999,999,999,999,995).  A withdrawal of
    // 6 USD shifts his balance across that boundary: the exponent increments
    // (0 → 1), so his effective inflow in Number space is only +5 — 1 USD is
    // consumed by the precision-boundary rounding and cannot be credited.
    //
    // The destroyed amount (1 USD) is sub-ULP at destinationScale=1 (step=10),
    // so the check treats it as an unavoidable IOU-precision artefact and
    // lets the transaction succeed.
    //
    // Contrast: if 15 USD were destroyed at the same scale (destroyed ≥ step),
    // floor(15/10)=1 ≠ 0 and the invariant would fire — that discrepancy IS
    // representable and indicates a real accounting bug.
    //
    // Pre-fixCleanup3_2_0: the "must increase destination balance" check fires
    // because roundedDestinationDelta = 0 ≤ 0.
    void
    testVaultWithdrawEqualityEnforced()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            STAmount const aliceLimit{usd.raw(), 2, 16};
            STAmount const bobLimit{usd.raw(), 2, 16};
            // Bob's balance sits 5 units below the 10^16 STAmount precision
            // boundary.  Receiving 6 USD shifts his exponent 0 → 1; the
            // STAmount records +5, not +6 (1 USD is lost to rounding).
            STAmount const atEdge2{usd.raw(), Number{9'999'999'999'999'995LL}};

            env(trust(alice, aliceLimit));
            env(trust(bob, bobLimit));
            env.close();

            env(pay(issuer, alice, usd(1'000)));
            env(pay(issuer, bob, atEdge2));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = usd(1'000)}));
            env.close();

            // Withdraw 6 USD to Bob: vault loses 6, Bob gains only 5.
            // Destroyed amount = 1 USD, which is sub-ULP at destinationScale=1.
            auto tx = vault.withdraw({.depositor = alice, .id = vaultKeylet.key, .amount = usd(6)});
            tx[sfDestination] = bob.human();
            env(tx, Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultWithdraw to destination at IOU precision boundary fires "
                "invariant (pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultWithdraw to destination at IOU precision boundary succeeds "
                "when destroyed amount is sub-ULP (post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tesSUCCESS);
        }
    }

    // Bug: when a depositor's IOU trustline balance is very large (e.g.
    // ~1e17), adding a small deposit (e.g. 1 USD) leaves sfAssetsTotal
    // unchanged at IOU precision because the increment is sub-ULP at the
    // vault's current asset scale.  The vault records the deposit, mints
    // shares, and decrements the depositor's trustline, but sfAssetsTotal
    // does not change — the conservation invariant fires because the rail
    // delta is zero.
    //
    // Two sub-cases are exercised:
    //   1. First-ever deposit into an empty vault: the depositor's own
    //      trustline has a large balance so 1 USD canonicalizes to zero
    //      when written back through the IOU rail.
    //   2. Subsequent deposit after the vault already holds a large
    //      sfAssetsTotal: a different depositor (bob, with a small balance)
    //      sends 1 USD, which again rounds to zero at the vault's coarse
    //      asset scale.
    //
    // Fix (fixCleanup3_2_0): the deposit transactor checks whether
    // roundToAsset(amount, vault_scale) == 0 and rejects early with
    // tecPRECISION_LOSS before any state is modified.
    void
    testVaultDepositCanonicalizeToZero()
    {
        using namespace test::jtx;
        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(100'000), issuer, alice, bob);
            env.close();

            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};

            STAmount const trustLimit{usd.raw(), Number{99'999'999'999'999'999LL}};
            STAmount const aliceFund{usd.raw(), Number{99'999'999'999'999'999LL}};

            env(trust(alice, trustLimit));
            env(trust(bob, trustLimit));
            env.close();

            env(pay(issuer, alice, aliceFund));
            env(pay(issuer, bob, usd(1000)));
            env.close();

            Vault const vault{env};

            // Scale=0 so sfAssetsTotal stores whole USD
            auto [vaultTx, vaultKeylet] = vault.create({.owner = alice, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();

            // Alice's deposit canonicalizes to zero at her own trustline scale
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = usd(1)}),
                Ter(expected));

            // Increase vault-scale
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = aliceFund}));
            env.close();

            env(vault.deposit({.depositor = bob, .id = vaultKeylet.key, .amount = usd(1)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultDeposit below Vault precision canonicalized to zero "
                "(pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit below Vault precision canonicalized to zero "
                "(post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tecPRECISION_LOSS);
        }
    }

    // VaultDeposit by issuer with the vault parked at the IOU 16-digit
    // edge (9.999e15). Issuer mints 2 more USD; the vault trust line
    // goes 9.999e15 → 10^16, gaining 1 unit instead of 2 (canonicalization).
    //
    // Pre-fixCleanup3_2_0: the proactive check is absent; the deposit
    // applies, then VaultInvariant's "deposit must increase vault
    // balance" assertion fires at finalize time on the rounded vault
    // delta of zero, returning tecINVARIANT_FAILED.
    // Post-amendment: reject deposit that is not representable at Vault scale.
    void
    testBugIssuerVaultDepositAtEdge()
    {
        using namespace test::jtx;

        auto runScenario = [this](FeatureBitset features, TER expected) {
            std::string logs;
            Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));

            Account const issuer{"issuer"};
            Account const owner{"owner"};

            env.fund(XRP(100'000), issuer, owner);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            STAmount const trustLimit{usd.raw(), 2, 16};
            STAmount const ownerFund{usd.raw(), Number{9'999'999'999'999'999LL}};

            env(trust(owner, trustLimit));
            env.close();
            env(pay(issuer, owner, ownerFund));
            env.close();

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = owner, .asset = usd});
            vaultTx[sfScale] = 0;
            env(vaultTx);
            env.close();
            env(vault.deposit({.depositor = owner, .id = vaultKeylet.key, .amount = ownerFund}));
            env.close();

            // Vault pseudo-account is now at 9.999e15. Issuer mints 2
            // more USD. Pre: tecINVARIANT_FAILED at finalize. Post:
            // tecPRECISION_LOSS proactively. Either way, no value moves.
            env(vault.deposit({.depositor = issuer, .id = vaultKeylet.key, .amount = usd(2)}),
                Ter(expected));
            env.close();
        };

        {
            testcase(
                "bug: VaultDeposit by issuer at IOU edge fires "
                "tecINVARIANT_FAILED at finalize (pre-fixCleanup3_2_0)");
            runScenario(testableAmendments() - fixCleanup3_2_0, tecINVARIANT_FAILED);
        }
        {
            testcase(
                "bug: VaultDeposit by issuer at IOU edge rejects with "
                "tecPRECISION_LOSS proactively (post-fixCleanup3_2_0)");
            runScenario(testableAmendments(), tecPRECISION_LOSS);
        }
    }

    void
    testReferenceHolding()
    {
        using namespace test::jtx;

        auto readReferenceHolding = [&](Env const& env,
                                        Keylet const& vaultKeylet) -> std::optional<uint256> {
            auto const sleVault = env.le(vaultKeylet);
            if (!sleVault)
                return std::nullopt;
            auto const sleIssuance = env.le(keylet::mptIssuance(sleVault->at(sfShareMPTID)));
            if (!sleIssuance || !sleIssuance->isFieldPresent(sfReferenceHolding))
                return std::nullopt;
            return sleIssuance->getFieldH256(sfReferenceHolding);
        };

        // Post-fixCleanup3_2_0: vault share carries sfReferenceHolding
        // pointing to the vault pseudo's MPToken (for MPT-backed vaults)
        // or RippleState (for IOU-backed vaults).
        {
            testcase("sfReferenceHolding: MPT-backed vault, post-amendment");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            auto const pseudoId = sleVault->at(sfAccount);
            auto const expected = keylet::mptoken(mptt.issuanceID(), pseudoId).key;

            auto const stored = readReferenceHolding(env, keylet);
            BEAST_EXPECT(stored.has_value());
            BEAST_EXPECT(stored && *stored == expected);
            // The pointed-to MPToken must actually exist.
            BEAST_EXPECT(env.le(keylet::mptoken(mptt.issuanceID(), pseudoId)) != nullptr);
        }

        {
            testcase("sfReferenceHolding: IOU-backed vault, post-amendment");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1'000'000), owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            auto const pseudoId = sleVault->at(sfAccount);
            auto const expected = keylet::line(pseudoId, asset.raw().get<Issue>()).key;

            auto const stored = readReferenceHolding(env, keylet);
            BEAST_EXPECT(stored.has_value());
            BEAST_EXPECT(stored && *stored == expected);
            // The pointed-to RippleState must actually exist.
            BEAST_EXPECT(env.le(keylet::line(pseudoId, asset.raw().get<Issue>())) != nullptr);
        }

        // XRP-backed vaults leave the field absent: XRP has no separate
        // holding ledger entry and no transferability concept to inherit.
        {
            testcase("sfReferenceHolding: XRP-backed vault, field absent");
            Env env{*this, testableAmendments()};
            Account const owner{"owner"};
            env.fund(XRP(10'000), owner);
            env.close();

            PrettyAsset const asset{xrpIssue(), 1'000'000};
            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(!readReferenceHolding(env, keylet).has_value());
        }

        // Pre-fixCleanup3_2_0: vault share has the field absent regardless
        // of underlying type.
        {
            testcase("sfReferenceHolding: vault share, pre-amendment");
            Env env{*this, testableAmendments() - fixCleanup3_2_0};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(!readReferenceHolding(env, keylet).has_value());
        }

        // Plain MPTokenIssuanceCreate (not a vault share) must never
        // populate the field. Only the post-amendment case is
        // interesting; pre-amendment nothing writes the field at all.
        {
            testcase("sfReferenceHolding: plain MPT issuance never set");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            env.fund(XRP(10'000), issuer);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            env.close();

            auto const sleIssuance = env.le(keylet::mptIssuance(mptt.issuanceID()));
            if (BEAST_EXPECT(sleIssuance))
                BEAST_EXPECT(!sleIssuance->isFieldPresent(sfReferenceHolding));
        }
    }

    // Probe every transactor surface that might delete the vault pseudo-
    // account's underlying holding (the MPToken or RippleState pointed to
    // by sfReferenceHolding). Each scenario asserts either that the
    // existing pseudo-account guards stop the deletion at preclaim, or
    // that the ledger leaves the holding intact afterwards. This is a
    // regression guard: if any of these guards regresses, the share's
    // sfReferenceHolding pointer would dangle and the new ValidMPTIssuance
    // invariant would catch it - but we want to fail much earlier, at
    // the transactor's preclaim / doApply, not at invariant time.
    void
    testHoldingDeletionBlocked()
    {
        using namespace test::jtx;

        // Helper: read the share's referenced holding and confirm the
        // pointed-to SLE still exists after the probe.
        auto referencedHoldingExists = [&](Env const& env, Keylet const& vaultKeylet) -> bool {
            auto const sleVault = env.le(vaultKeylet);
            if (!sleVault)
                return false;
            auto const sleIssuance = env.le(keylet::mptIssuance(sleVault->at(sfShareMPTID)));
            if (!sleIssuance || !sleIssuance->isFieldPresent(sfReferenceHolding))
                return false;
            auto const holdingKey = sleIssuance->getFieldH256(sfReferenceHolding);
            return env.le(keylet::unchecked(holdingKey)) != nullptr;
        };

        // ---- MPT-backed vault ----------------------------------------
        {
            testcase("vault pseudo MPToken: Clawback blocked by tecPSEUDO_ACCOUNT");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            env.fund(XRP(10'000), issuer, owner, depositor);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            Account const pseudoAccount{"vault-pseudo", env.le(keylet)->at(sfAccount)};
            // Issuer attempts to claw back the FULL underlying balance
            // (500) directly from the vault pseudo-account. With the
            // full amount, the doApply path would drain the pseudo's
            // MPToken to zero and removeEmptyHolding would erase it -
            // if doApply ever ran. SAV's pseudo-account guard at
            // Clawback.cpp:201 refuses at preclaim with
            // tecPSEUDO_ACCOUNT before any state change.
            env(claw(issuer, asset(500), pseudoAccount), Ter{tecPSEUDO_ACCOUNT});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
            // Sanity: pseudo's full balance is intact.
            BEAST_EXPECT(env.balance(pseudoAccount, asset).number() == 500);
        }

        {
            testcase("vault pseudo MPToken: Issuer cannot Unauthorize pseudo");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = issuer, .holder = owner});
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            auto const pseudoId = env.le(keylet)->at(sfAccount);
            // Issuer attempts MPTokenAuthorize against the pseudo with
            // tfMPTUnauthorize. MPTokenAuthorize.cpp blocks pseudo
            // accounts via isPseudoAccount; the pseudo's MPToken is
            // preserved. Construct the tx manually since the pseudo
            // lacks a signing key, and the issuer-driven flavour is
            // expressed via sfHolder.
            json::Value jv;
            jv[sfAccount] = issuer.human();
            jv[sfHolder] = toBase58(pseudoId);
            jv[sfMPTokenIssuanceID] = to_string(mptt.issuanceID());
            jv[sfFlags] = tfMPTUnauthorize;
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            env(jv, Ter{tecNO_PERMISSION});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
        }

        {
            testcase("vault pseudo MPToken: MPTokenIssuanceDestroy blocked while vault holds");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            env.fund(XRP(10'000), issuer, owner, depositor);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            // While the vault holds outstanding underlying, the issuer
            // cannot destroy the issuance. tecHAS_OBLIGATIONS confirms
            // the protection - and as a side effect, the share's
            // sfReferenceHolding pointer cannot be left pointing at a
            // ghost issuance.
            mptt.destroy({.id = mptt.issuanceID(), .err = tecHAS_OBLIGATIONS});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
        }

        // ---- IOU-backed vault ----------------------------------------
        {
            testcase("vault pseudo trust line: Clawback blocked by tecPSEUDO_ACCOUNT");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1'000'000), owner);
            env(pay(issuer, owner, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            Account const pseudoAccount{"vault-pseudo", env.le(keylet)->at(sfAccount)};
            // Issuer attempts to claw back the FULL IOU balance (500)
            // directly from the vault pseudo. With the full amount, the
            // doApply path would drain the trust line to zero and (if
            // both reserve flags clear) trustDelete would erase it - if
            // doApply ever ran. The same SAV pseudo-account guard
            // refuses at preclaim with tecPSEUDO_ACCOUNT. The amount's
            // STAmount issuer field is the holder, per IOU clawback
            // convention.
            env(claw(issuer, pseudoAccount["IOU"](500)), Ter{tecPSEUDO_ACCOUNT});
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
            // Sanity: pseudo's full balance is intact.
            BEAST_EXPECT(env.balance(pseudoAccount, asset).number() == 500);
        }

        {
            testcase("vault pseudo trust line: TrustSet limit=0 from issuer preserves line");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const asset = issuer["IOU"];
            env.trust(asset(1'000'000), owner);
            env(pay(issuer, owner, asset(1'000)));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(500)}));
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));

            // Issuer submits TrustSet with limit=0 against the vault
            // pseudo. The pseudo's side of the line still has the
            // original (non-zero) limit and a non-zero balance, so the
            // line is preserved - even though the issuer cleared its
            // own side. trustDelete only fires when both limits clear
            // and the balance is zero.
            Account const pseudoAccount{"vault-pseudo", env.le(keylet)->at(sfAccount)};
            env(trust(issuer, pseudoAccount["IOU"](0)));
            env.close();
            BEAST_EXPECT(referencedHoldingExists(env, keylet));
        }

        // ---- Positive control: VaultDelete is the only legitimate path
        {
            testcase("vault pseudo holding: VaultDelete is the legitimate cleanup path");
            Env env{*this, testableAmendments()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            env.fund(XRP(10'000), issuer, owner);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(referencedHoldingExists(env, keylet));
            auto const pseudoId = env.le(keylet)->at(sfAccount);
            auto const sharedMptId = env.le(keylet)->at(sfShareMPTID);
            auto const holdingKeylet = keylet::mptoken(mptt.issuanceID(), pseudoId);

            // VaultDelete tears down the vault pseudo's holding, the
            // share issuance, and the pseudo-account itself. Invariant
            // permits this because the tx is ttVAULT_DELETE.
            env(vault.del({.owner = owner, .id = keylet.key}));
            env.close();

            BEAST_EXPECT(env.le(keylet) == nullptr);
            BEAST_EXPECT(env.le(holdingKeylet) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptIssuance(sharedMptId)) == nullptr);
        }
    }

    // VaultDeposit::preclaim uses accountHolds(..., SpendableHandling::
    // shFULL_BALANCE), which for an IOU asset adds the counterparty's
    // LowLimit/HighLimit to the depositor's raw balance (TokenHelpers.cpp:
    // getTrustLineBalance with includeOppositeLimit=true). When the
    // depositor's raw balance < deposit amount but raw + opposite limit >=
    // amount, preclaim is satisfied. doApply then calls
    // directSendNoFeeIOU, which unconditionally subtracts saAmount from
    // saBalance — driving the trust line negative — and returns tesSUCCESS.
    // The post-send sanity check uses the default shSIMPLE_BALANCE (no
    // opposite-limit add), sees a negative balance, and returns tefINTERNAL.
    void
    testVaultDepositNegativeBalanceFromOppositeLimit()
    {
        auto runTest = [&](FeatureBitset f, TER expected) {
            using namespace test::jtx;
            using namespace std::literals;

            Env env{*this, f};
            Account const gw{"gateway"};
            Account const owner{"owner"};
            Account const depositor{"depositor"};

            env.fund(XRP(10000), gw, owner, depositor);
            env.close();

            // Gateway with DefaultRipple so vault creation on its IOU works.
            env(fset(gw, asfDefaultRipple));
            env.close();

            // Depositor opens a trust line to gateway and receives a small
            // balance.
            PrettyAsset const usd = gw["USD"];
            env.trust(usd(1000), depositor);
            env(pay(gw, depositor, usd(100)));  // raw trust-line balance: 100
            env.close();

            // Key precondition: gateway sets a non-zero limit on the same
            // RippleState — the "opposite field" from depositor's perspective.
            // This is what inflates shFULL_BALANCE in preclaim above the raw
            // balance.
            env(trust(gw, depositor["USD"](1000)));
            env.close();

            // Create the IOU vault.
            Vault const vault{env};
            auto [vaultTx, keylet] = vault.create({.owner = owner, .asset = usd});
            env(vaultTx);
            env.close();

            // Submit a deposit of 500 USD:
            //   - raw balance:                100 USD
            //   - opposite limit (gw's side): 1000 USD
            //   - preclaim sees 100 + 1000 = 1100, passes (>= 500)
            //   - doApply transfers 500, depositor's trust-line balance
            //     becomes -400
            //   - sanity check at VaultDeposit.cpp:256 fires
            //   - tx returns tefINTERNAL (BUG — should be tesSUCCESS.
            auto depositTx =
                vault.deposit({.depositor = depositor, .id = keylet.key, .amount = usd(500)});
            env(depositTx, Ter(expected));
            env.close();
        };

        {
            testcase(
                "IOU vault deposit exceeding depositor's balance but "
                "within counterparty's trust limit, pre-fixCleanup3_2_0 "
                "(tefINTERNAL)");
            runTest(test::jtx::testableAmendments() - fixCleanup3_2_0, tefINTERNAL);
        }
        {
            testcase(
                "IOU vault deposit exceeding depositor's balance but "
                "within counterparty's trust limit, post-fixCleanup3_2_0 "
                "(tesSUCCESS)");
            runTest(test::jtx::testableAmendments(), tesSUCCESS);
        }
    }

public:
    void
    run() override
    {
        testVaultWithdrawEqualityEnforced();
        testBugIssuerVaultDepositAtEdge();
        testBugMakeDeltaPosteriorScale();
        testBugMakeDeltaAnteriorScale();
        testVaultDepositCanonicalizeToZero();
        testVaultWithdrawCanonicalizeToZero();
        testVaultDepositNegativeBalanceFromOppositeLimit();
        testSequences();
        testPreflight();
        testCreateFailXRP();
        testCreateFailIOU();
        testCreateFailMPT();
        testWithMPT();
        testWithIOU();
        testWithDomainCheck();
        testWithDomainChecXRP();
        testNonTransferableShares();
        testFailedPseudoAccount();
        testScaleIOU();
        testRPC();
        testVaultClawbackBurnShares();
        testVaultClawbackAssets();
        testVaultEscrowedMPT();
        testAssetsMaximum();
        testBug6LimitBypassWithShares();
        testRemoveEmptyHoldingLockedAmount();

        testWithdrawSoleShareholderFixedAssetExit(all_ - fixCleanup3_2_0);
        testWithdrawSoleShareholderFixedAssetExit(all_);
        testWithdrawSoleShareholderFullSharesRejected(all_ - fixCleanup3_2_0);
        testWithdrawSoleShareholderFullSharesRejected(all_);
        testWithdrawSoleShareholderCleanVaultUnaffected(all_ - fixCleanup3_2_0);
        testWithdrawSoleShareholderCleanVaultUnaffected(all_);
        testWithdrawSoleShareholderPartialFixedSharesUsesFullPrice();
        testWithdrawSoleShareholderLoanRepaymentExit();

        testReferenceHolding();
        testHoldingDeletionBlocked();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, app, xrpl, 1);

}  // namespace xrpl

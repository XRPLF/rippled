#include <test/jtx.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
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

#include <optional>

namespace ripple {

class Vault_test : public beast::unit_test::suite
{
    using PrettyAsset = ripple::test::jtx::PrettyAsset;
    using PrettyAmount = ripple::test::jtx::PrettyAmount;

    static auto constexpr negativeAmount =
        [](PrettyAsset const& asset) -> PrettyAmount {
        return {STAmount{asset.raw(), 1ul, 0, true, STAmount::unchecked{}}, ""};
    };

    void
    testSequences()
    {
        using namespace test::jtx;
        Account issuer{"issuer"};
        Account owner{"owner"};
        Account depositor{"depositor"};
        Account charlie{"charlie"};  // authorized 3rd party
        Account dave{"dave"};

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
                [&env,
                 keylet = keylet,
                 asset,
                 this]() -> std::tuple<PrettyAsset, Account> {
                auto const vault = env.le(keylet);
                BEAST_EXPECT(vault != nullptr);
                if (asset.raw().holds<Issue>() && !asset.raw().native())
                    BEAST_EXPECT(vault->at(sfScale) == 6);
                else
                    BEAST_EXPECT(vault->at(sfScale) == 0);
                auto const shares =
                    env.le(keylet::mptIssuance(vault->at(sfShareMPTID)));
                BEAST_EXPECT(shares != nullptr);
                if (asset.raw().holds<Issue>() && !asset.raw().native())
                    BEAST_EXPECT(shares->at(sfAssetScale) == 6);
                else
                    BEAST_EXPECT(shares->at(sfAssetScale) == 0);
                return {
                    MPTIssue(vault->at(sfShareMPTID)),
                    Account("vault", vault->at(sfAccount))};
            }();
            auto const shares = share.raw().get<MPTIssue>();
            env.memoize(vaultAccount);

            // Several 3rd party accounts which cannot receive funds
            Account alice{"alice"};
            Account erin{"erin"};  // not authorized by issuer
            env.fund(XRP(1000), alice, erin);
            env(fset(alice, asfDepositAuth));
            env.close();

            {
                testcase(prefix + " fail to deposit more than assets held");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(10000)});
                env(tx, ter(tecINSUFFICIENT_FUNDS));
                env.close();
            }

            {
                testcase(prefix + " deposit non-zero amount");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                env(tx);
                env.close();
                BEAST_EXPECT(
                    env.balance(depositor, shares) == share(50 * scale));
            }

            {
                testcase(prefix + " deposit non-zero amount again");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                env(tx);
                env.close();
                BEAST_EXPECT(
                    env.balance(depositor, shares) == share(100 * scale));
            }

            {
                testcase(prefix + " fail to delete non-empty vault");
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx, ter(tecHAS_OBLIGATIONS));
                env.close();
            }

            {
                testcase(prefix + " fail to update because wrong owner");
                auto tx = vault.set({.owner = issuer, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(50).number();
                env(tx, ter(tecNO_PERMISSION));
                env.close();
            }

            {
                testcase(
                    prefix + " fail to set maximum lower than current amount");
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = asset(50).number();
                env(tx, ter(tecLIMIT_EXCEEDED));
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
                tx[sfDomainID] = to_string(base_uint<256>(42ul));
                env(tx, ter{tecNO_PERMISSION});
                env.close();
            }

            {
                testcase(prefix + " fail to deposit more than maximum");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx, ter(tecLIMIT_EXCEEDED));
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
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1000)});
                env(tx, ter(tecINSUFFICIENT_FUNDS));
                env.close();
            }

            {
                testcase(prefix + " deposit some more");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx);
                env.close();
                BEAST_EXPECT(
                    env.balance(depositor, shares) == share(200 * scale));
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
                env.close();
                if (!asset.raw().native())
                {
                    BEAST_EXPECT(
                        env.balance(depositor, shares) == share(190 * scale));
                }
            }

            {
                testcase(prefix + " clawback all");
                auto code = asset.raw().native() ? ter(tecNO_PERMISSION)
                                                 : ter(tesSUCCESS);
                auto tx = vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = depositor});
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
                        env(tx, ter{tecPRECISION_LOSS});
                        env.close();
                    }

                    {
                        auto tx = vault.withdraw(
                            {.depositor = depositor,
                             .id = keylet.key,
                             .amount = asset(10)});
                        env(tx, ter{tecPRECISION_LOSS});
                        env.close();
                    }
                }
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " deposit again");
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(200)});
                env(tx);
                env.close();
                BEAST_EXPECT(
                    env.balance(depositor, shares) == share(200 * scale));
            }
            else
            {
                testcase(prefix + " deposit/withdrawal same or less than fee");
                auto const amount = env.current()->fees().base;

                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = amount});
                env(tx);
                env.close();

                tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = amount});
                env(tx);
                env.close();

                tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = amount});
                env(tx);
                env.close();

                // Withdraw to 3rd party
                tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = amount});
                tx[sfDestination] = charlie.human();
                env(tx);
                env.close();

                tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = amount - 1});
                env(tx);
                env.close();

                tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = amount - 1});
                env(tx);
                env.close();
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
                env.close();
            }

            {
                testcase(prefix + " fail to withdraw to zero destination");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1000)});
                tx[sfDestination] = "0";
                env(tx, ter(temMALFORMED));
                env.close();
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
                env.close();
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
                env.close();
            }

            {
                testcase(prefix + " withdraw to 3rd party lsfRequireDestTag");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                tx[sfDestination] = dave.human();
                tx[sfDestinationTag] = "0";
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " deposit again");
                auto tx = vault.deposit(
                    {.depositor = dave, .id = keylet.key, .amount = asset(50)});
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " fail to withdraw lsfRequireDestTag");
                auto tx = vault.withdraw(
                    {.depositor = dave, .id = keylet.key, .amount = asset(50)});
                env(tx, ter{tecDST_TAG_NEEDED});
                env.close();
            }

            {
                testcase(prefix + " withdraw with tag");
                auto tx = vault.withdraw(
                    {.depositor = dave, .id = keylet.key, .amount = asset(50)});
                tx[sfDestinationTag] = "0";
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " withdraw to authorized 3rd party");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                tx[sfDestination] = charlie.human();
                env(tx);
                env.close();
                BEAST_EXPECT(
                    env.balance(depositor, shares) == share(100 * scale));
            }

            {
                testcase(prefix + " withdraw to issuer");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                tx[sfDestination] = issuer.human();
                env(tx);
                env.close();
                BEAST_EXPECT(
                    env.balance(depositor, shares) == share(50 * scale));
            }

            if (!asset.raw().native())
            {
                testcase(prefix + " issuer deposits");
                auto tx = vault.deposit(
                    {.depositor = issuer,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(issuer, shares) == share(10 * scale));

                testcase(prefix + " issuer withdraws");
                tx = vault.withdraw(
                    {.depositor = issuer,
                     .id = keylet.key,
                     .amount = share(10 * scale)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(issuer, shares) == share(0 * scale));
            }

            {
                testcase(prefix + " withdraw remaining assets");
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
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
                    env(tx, ter{tecPRECISION_LOSS});
                    env.close();
                }

                {
                    auto tx = vault.withdraw(
                        {.depositor = depositor,
                         .id = keylet.key,
                         .amount = share(10)});
                    env(tx, ter{tecINSUFFICIENT_FUNDS});
                    env.close();
                }
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
                env.close();
                {
                    auto tx = pay(erin, depositor, share(10 * scale));

                    // depositor no longer has MPToken for shares
                    env(tx, ter{tecNO_AUTH});
                    env.close();

                    // depositor will gain MPToken for shares again
                    env(vault.deposit(
                        {.depositor = depositor,
                         .id = keylet.key,
                         .amount = asset(1)}));
                    env.close();

                    env(tx);
                    env.close();
                }

                testcase(prefix + " withdraw to authorized 3rd party");
                // Depositor withdraws assets, destined to Erin
                tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(10)});
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
                env(pay(depositor, erin, share(1)), ter{tecNO_LINE});
                env.close();

                // Depositor withdraws remaining single asset
                tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1)});
                env(tx);
                env.close();
            }

            {
                testcase(prefix + " fail to delete because wrong owner");
                auto tx = vault.del({.owner = issuer, .id = keylet.key});
                env(tx, ter(tecNO_PERMISSION));
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
                             std::string prefix,
                             std::function<PrettyAsset(Env & env)> setup) {
            Env env{*this, testable_amendments() | featureSingleAssetVault};

            Vault vault{env};
            env.fund(XRP(1000), issuer, owner, depositor, charlie, dave);
            env.close();
            env(fset(issuer, asfAllowTrustLineClawback));
            env(fset(issuer, asfRequireAuth));
            env(fset(dave, asfRequireDest));
            env.close();
            env.require(flags(issuer, asfAllowTrustLineClawback));
            env.require(flags(issuer, asfRequireAuth));

            PrettyAsset asset = setup(env);
            testSequence(prefix, env, vault, asset);
        };

        testCases("XRP", [&](Env& env) -> PrettyAsset {
            return {xrpIssue(), 1'000'000};
        });

        testCases("IOU", [&](Env& env) -> Asset {
            PrettyAsset asset = issuer["IOU"];
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
            MPTTester mptt{env, issuer, mptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
            PrettyAsset asset = mptt.issuanceID();
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
            FeatureBitset features =
                testable_amendments() | featureSingleAssetVault;
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
            Account issuer{"issuer"};
            Account owner{"owner"};
            Vault vault{env};
            env.fund(XRP(1000), issuer, owner);
            env.close();

            env(fset(issuer, asfAllowTrustLineClawback));
            env(fset(issuer, asfRequireAuth));
            env.close();

            PrettyAsset asset = issuer["IOU"];
            env(trust(owner, asset(1000)));
            env(trust(issuer, asset(0), owner, tfSetfAuth));
            env(pay(issuer, owner, asset(1000)));
            env.close();

            test(env, issuer, owner, asset, vault);
        };

        testCase(
            [&](Env& env,
                Account const& issuer,
                Account const& owner,
                Asset const& asset,
                Vault& vault) {
                testcase("disabled single asset vault");

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx, ter{temDISABLED});

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
            },
            {.features = testable_amendments() - featureSingleAssetVault});

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid flags");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfFlags] = tfClearDeepFreeze;
            env(tx, ter{temINVALID_FLAG});

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
        });

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid fee");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[jss::Fee] = "-1";
            env(tx, ter{temBAD_FEE});

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[jss::Fee] = "-1";
                env(tx, ter{temBAD_FEE});
            }

            {
                auto tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[jss::Fee] = "-1";
                env(tx, ter{temBAD_FEE});
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[jss::Fee] = "-1";
                env(tx, ter{temBAD_FEE});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = asset(10)});
                tx[jss::Fee] = "-1";
                env(tx, ter{temBAD_FEE});
            }

            {
                auto tx = vault.del({.owner = owner, .id = keylet.key});
                tx[jss::Fee] = "-1";
                env(tx, ter{temBAD_FEE});
            }
        });

        testCase(
            [&](Env& env,
                Account const&,
                Account const& owner,
                Asset const&,
                Vault& vault) {
                testcase("disabled permissioned domain");

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = xrpIssue()});
                tx[sfDomainID] = to_string(base_uint<256>(42ul));
                env(tx, ter{temDISABLED});

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    tx[sfDomainID] = to_string(base_uint<256>(42ul));
                    env(tx, ter{temDISABLED});
                }

                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});
                    tx[sfDomainID] = "0";
                    env(tx, ter{temDISABLED});
                }
            },
            {.features = (testable_amendments() | featureSingleAssetVault) -
                 featurePermissionedDomains});

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("use zero vault");

            auto [tx, keylet] =
                vault.create({.owner = owner, .asset = xrpIssue()});

            {
                auto tx = vault.set({
                    .owner = owner,
                    .id = beast::zero,
                });
                env(tx, ter{temMALFORMED});
            }

            {
                auto tx = vault.deposit(
                    {.depositor = owner,
                     .id = beast::zero,
                     .amount = asset(10)});
                env(tx, ter(temMALFORMED));
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = beast::zero,
                     .amount = asset(10)});
                env(tx, ter{temMALFORMED});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = beast::zero,
                     .holder = owner,
                     .amount = asset(10)});
                env(tx, ter{temMALFORMED});
            }

            {
                auto tx = vault.del({
                    .owner = owner,
                    .id = beast::zero,
                });
                env(tx, ter{temMALFORMED});
            }
        });

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("clawback from self");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = issuer,
                     .amount = asset(10)});
                env(tx, ter{temMALFORMED});
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("withdraw to bad destination");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[jss::Destination] = "0";
                env(tx, ter{temMALFORMED});
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("create with Scale");

            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                tx[sfScale] = 255;
                env(tx, ter(temMALFORMED));
            }

            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                tx[sfScale] = 19;
                env(tx, ter(temMALFORMED));
            }

            // accepted range from 0 to 18
            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                tx[sfScale] = 18;
                env(tx);
                env.close();
                auto const sleVault = env.le(keylet);
                BEAST_EXPECT(sleVault);
                BEAST_EXPECT((*sleVault)[sfScale] == 18);
            }

            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                tx[sfScale] = 0;
                env(tx);
                env.close();
                auto const sleVault = env.le(keylet);
                BEAST_EXPECT(sleVault);
                BEAST_EXPECT((*sleVault)[sfScale] == 0);
            }

            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();
                auto const sleVault = env.le(keylet);
                BEAST_EXPECT(sleVault);
                BEAST_EXPECT((*sleVault)[sfScale] == 6);
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("create or set invalid data");

            auto [tx1, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = tx1;
                tx[sfData] = "";
                env(tx, ter(temMALFORMED));
            }

            {
                auto tx = tx1;
                // A hexadecimal string of 257 bytes.
                tx[sfData] = std::string(514, 'A');
                env(tx, ter(temMALFORMED));
            }

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfData] = "";
                env(tx, ter{temMALFORMED});
            }

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                // A hexadecimal string of 257 bytes.
                tx[sfData] = std::string(514, 'A');
                env(tx, ter{temMALFORMED});
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("set nothing updated");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                env(tx, ter{temMALFORMED});
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("create with invalid metadata");

            auto [tx1, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = tx1;
                tx[sfMPTokenMetadata] = "";
                env(tx, ter(temMALFORMED));
            }

            {
                auto tx = tx1;
                // This metadata is for the share token.
                // A hexadecimal string of 1025 bytes.
                tx[sfMPTokenMetadata] = std::string(2050, 'B');
                env(tx, ter(temMALFORMED));
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("set negative maximum");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfAssetsMaximum] = negativeAmount(asset).number();
                env(tx, ter{temMALFORMED});
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid deposit amount");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = negativeAmount(asset)});
                env(tx, ter(temBAD_AMOUNT));
            }

            {
                auto tx = vault.deposit(
                    {.depositor = owner, .id = keylet.key, .amount = asset(0)});
                env(tx, ter(temBAD_AMOUNT));
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid set immutable flag");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.set({.owner = owner, .id = keylet.key});
                tx[sfFlags] = tfVaultPrivate;
                env(tx, ter(temINVALID_FLAG));
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid withdraw amount");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = negativeAmount(asset)});
                env(tx, ter(temBAD_AMOUNT));
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = owner, .id = keylet.key, .amount = asset(0)});
                env(tx, ter(temBAD_AMOUNT));
            }
        });

        testCase([&](Env& env,
                     Account const& issuer,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid clawback");

            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = vault.clawback(
                    {.issuer = owner,
                     .id = keylet.key,
                     .holder = issuer,
                     .amount = asset(50)});
                env(tx, ter(temMALFORMED));
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = negativeAmount(asset)});
                env(tx, ter(temBAD_AMOUNT));
            }
        });

        testCase([&](Env& env,
                     Account const&,
                     Account const& owner,
                     Asset const& asset,
                     Vault& vault) {
            testcase("invalid create");

            auto [tx1, keylet] = vault.create({.owner = owner, .asset = asset});

            {
                auto tx = tx1;
                tx[sfWithdrawalPolicy] = 0;
                env(tx, ter(temMALFORMED));
            }

            {
                auto tx = tx1;
                tx[sfDomainID] = to_string(base_uint<256>(42ul));
                env(tx, ter{temMALFORMED});
            }

            {
                auto tx = tx1;
                tx[sfAssetsMaximum] = negativeAmount(asset).number();
                env(tx, ter{temMALFORMED});
            }

            {
                auto tx = tx1;
                tx[sfFlags] = tfVaultPrivate;
                tx[sfDomainID] = "0";
                env(tx, ter{temMALFORMED});
            }
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
            Env env{*this, testable_amendments() | featureSingleAssetVault};
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
            env(tx, fee(env.current()->fees().base - 1), ter(telINSUF_FEE_P));
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
            tx[sfFlags] = tfVaultPrivate;
            tx[sfDomainID] = to_string(base_uint<256>(42ul));
            testcase("non-existing domain");
            env(tx, ter{tecOBJECT_NOT_FOUND});
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
            env(tx, ter{temMALFORMED});
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
            env(tx, ter{temMALFORMED});
        });
    }

    void
    testCreateFailIOU()
    {
        using namespace test::jtx;
        {
            {
                testcase("IOU fail because MPT is disabled");
                Env env{
                    *this,
                    (testable_amendments() - featureMPTokensV1) |
                        featureSingleAssetVault};
                Account issuer{"issuer"};
                Account owner{"owner"};
                env.fund(XRP(1000), issuer, owner);
                env.close();

                Vault vault{env};
                Asset asset = issuer["IOU"].asset();
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});

                env(tx, ter(temDISABLED));
                env.close();
            }

            {
                testcase("IOU fail create frozen");
                Env env{*this, testable_amendments() | featureSingleAssetVault};
                Account issuer{"issuer"};
                Account owner{"owner"};
                env.fund(XRP(1000), issuer, owner);
                env.close();
                env(fset(issuer, asfGlobalFreeze));
                env.close();

                Vault vault{env};
                Asset asset = issuer["IOU"].asset();
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});

                env(tx, ter(tecFROZEN));
                env.close();
            }

            {
                testcase("IOU fail create no ripling");
                Env env{*this, testable_amendments() | featureSingleAssetVault};
                Account issuer{"issuer"};
                Account owner{"owner"};
                env.fund(XRP(1000), issuer, owner);
                env.close();
                env(fclear(issuer, asfDefaultRipple));
                env.close();

                Vault vault{env};
                Asset asset = issuer["IOU"].asset();
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx, ter(terNO_RIPPLE));
                env.close();
            }

            {
                testcase("IOU no issuer");
                Env env{*this, testable_amendments() | featureSingleAssetVault};
                Account issuer{"issuer"};
                Account owner{"owner"};
                env.fund(XRP(1000), owner);
                env.close();

                Vault vault{env};
                Asset asset = issuer["IOU"].asset();
                {
                    auto [tx, keylet] =
                        vault.create({.owner = owner, .asset = asset});
                    env(tx, ter(terNO_ACCOUNT));
                    env.close();
                }
            }
        }

        {
            testcase("IOU fail create vault for AMM LPToken");
            Env env{*this, testable_amendments() | featureSingleAssetVault};
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

        auto testCase = [this](std::function<void(
                                   Env & env,
                                   Account const& issuer,
                                   Account const& owner,
                                   Account const& depositor,
                                   Asset const& asset,
                                   Vault& vault)> test) {
            Env env{*this, testable_amendments() | featureSingleAssetVault};
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
            env(tx, ter(tecNO_AUTH));
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
            env(tx, ter{temMALFORMED});
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
            env(tx, ter{temMALFORMED});
        });
    }

    void
    testNonTransferableShares()
    {
        using namespace test::jtx;

        Env env{*this, testable_amendments() | featureSingleAssetVault};
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
                       [sfOutstandingAmount] == "100000000");

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
                       [sfOutstandingAmount] == "50000000");
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
            Env env{*this, testable_amendments() | featureSingleAssetVault};
            Account issuer{"issuer"};
            Account owner{"owner"};
            Account depositor{"depositor"};
            env.fund(XRP(args.initialXRP), issuer, owner, depositor);
            env.close();
            Vault vault{env};

            MPTTester mptt{env, issuer, mptInitNoFund};
            auto const none = LedgerSpecificFlags(0);
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock |
                     (args.enableClawback ? tfMPTCanClawback : none) |
                     (args.requireAuth ? tfMPTRequireAuth : none)});
            PrettyAsset asset = mptt.issuanceID();
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
            testcase("MPT global lock blocks create");
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
            testcase("MPT global lock blocks deposit");
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            mptt.set({.account = issuer, .flags = tfMPTLock});
            env.close();

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx, ter{tecLOCKED});
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
            env.close();

            tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx, ter(tecLOCKED));

            tx[sfDestination] = issuer.human();
            env(tx, ter(tecLOCKED));

            // Clawback is still permitted, even with global lock
            tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = depositor,
                 .amount = asset(0)});
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

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(100)});
            env(tx);
            env.close();

            {
                auto tx = vault.clawback(
                    {.issuer = owner, .id = keylet.key, .holder = depositor});
                env(tx, ter(tecNO_PERMISSION));
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

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1000)});
                env(tx);
                env.close();

                {
                    // Remove depositor MPToken and it will not be re-created
                    mptt.authorize(
                        {.account = depositor, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken =
                        keylet::mptoken(mptt.issuanceID(), depositor);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 == nullptr);

                    tx = vault.withdraw(
                        {.depositor = depositor,
                         .id = keylet.key,
                         .amount = asset(100)});
                    env(tx, ter{tecNO_AUTH});
                    env.close();

                    auto const sleMPT2 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT2 == nullptr);
                }

                {
                    // Set destination to 3rd party without MPToken
                    Account charlie{"charlie"};
                    env.fund(XRP(1000), charlie);
                    env.close();

                    tx = vault.withdraw(
                        {.depositor = depositor,
                         .id = keylet.key,
                         .amount = asset(100)});
                    tx[sfDestination] = charlie.human();
                    env(tx, ter(tecNO_AUTH));
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

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
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
                    mptt.authorize(
                        {.account = depositor, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken =
                        keylet::mptoken(mptt.issuanceID(), depositor);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 == nullptr);

                    tx = vault.withdraw(
                        {.depositor = depositor,
                         .id = keylet.key,
                         .amount = asset(100)});
                    env(tx);
                    env.close();

                    auto const sleMPT2 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT2 != nullptr);
                    BEAST_EXPECT(sleMPT2->at(sfMPTAmount) == 100);
                }

                {
                    // Remove 3rd party MPToken and it will not be re-created
                    mptt.authorize(
                        {.account = owner, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken =
                        keylet::mptoken(mptt.issuanceID(), owner);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 == nullptr);

                    tx = vault.withdraw(
                        {.depositor = depositor,
                         .id = keylet.key,
                         .amount = asset(100)});
                    tx[sfDestination] = owner.human();
                    env(tx, ter(tecNO_AUTH));
                    env.close();

                    auto const sleMPT2 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT2 == nullptr);
                }
            },
            {.requireAuth = false});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env env{*this, testable_amendments()};
            return {
                env.current()->fees().accountReserve(0).drops() /
                    DROPS_PER_XRP.drops(),
                env.current()->fees().increment.drops() /
                    DROPS_PER_XRP.drops()};
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
                testcase("MPT failed reserve to re-create MPToken");

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
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
                    mptt.authorize(
                        {.account = owner, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken =
                        keylet::mptoken(mptt.issuanceID(), owner);
                    auto const sleMPT = env.le(mptoken);
                    BEAST_EXPECT(sleMPT == nullptr);

                    // Use one reserve so the next transaction fails
                    env(ticket::create(owner, 1));
                    env.close();

                    // No reserve to create MPToken for asset in VaultWithdraw
                    tx = vault.withdraw(
                        {.depositor = owner,
                         .id = keylet.key,
                         .amount = asset(100)});
                    env(tx, ter{tecINSUFFICIENT_RESERVE});
                    env.close();

                    env(pay(depositor, owner, XRP(incReserve)));
                    env.close();

                    // Withdraw can now create asset MPToken, tx will succeed
                    env(tx);
                    env.close();
                }
            },
            {.requireAuth = false,
             .initialXRP = acctReserve + incReserve * 4 + 1});

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

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(1000)});
            env(tx);
            env.close();

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = depositor,
                     .amount = asset(0)});
                env(tx);
            }

            mptt.destroy({.issuer = issuer, .id = mptt.issuanceID()});
            env.close();

            {
                auto [tx, keylet] =
                    vault.create({.owner = depositor, .asset = asset});
                env(tx, ter{tecOBJECT_NOT_FOUND});
            }

            {
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx, ter{tecOBJECT_NOT_FOUND});
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx, ter{tecOBJECT_NOT_FOUND});
            }

            {
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = depositor,
                     .amount = asset(0)});
                env(tx, ter{tecOBJECT_NOT_FOUND});
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

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(1000)});
            env(tx);
            env.close();

            auto const issuanceId = [&env](ripple::Keylet keylet) -> MPTID {
                auto const vault = env.le(keylet);
                return vault->at(sfShareMPTID);
            }(keylet);
            PrettyAsset shares = MPTIssue(issuanceId);

            {
                // owner has MPToken for shares they did not explicitly create
                env(pay(depositor, owner, shares(1)));
                env.close();

                tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = shares(1)});
                env(tx);
                env.close();

                // owner's MPToken for vault shares not destroyed by withdraw
                env(pay(depositor, owner, shares(1)));
                env.close();

                tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = asset(0)});
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
                    Json::Value jv;
                    jv[sfAccount] = owner.human();
                    jv[sfMPTokenIssuanceID] = to_string(issuanceId);
                    jv[sfFlags] = tfMPTUnauthorize;
                    jv[sfTransactionType] = jss::MPTokenAuthorize;
                    env(jv);
                    env.close();
                }

                // owner no longer has MPToken for vault shares
                tx = pay(depositor, owner, shares(1));
                env(tx, ter{tecNO_AUTH});
                env.close();

                // destroy all remaining shares, so we can delete vault
                tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = depositor,
                     .amount = asset(0)});
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

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1000)});
                env(tx);
                env.close();

                {
                    auto tx = vault.clawback(
                        {.issuer = issuer,
                         .id = keylet.key,
                         .holder = depositor,
                         .amount = asset(0)});
                    env(tx, ter{tecNO_PERMISSION});
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
            env.close();

            {
                auto tx = vault.withdraw(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx, ter(tecNO_AUTH));

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
                auto tx = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx, ter(tecNO_AUTH));
            }

            // Clawback works
            tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = depositor,
                 .amount = asset(800)});
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

            // Can delete an empty vault even when asset is locked.
            tx = vault.del({.owner = owner, .id = keylet.key});
            env(tx);
        });

        {
            testcase("MPT shares to a vault");

            Env env{*this, testable_amendments() | featureSingleAssetVault};
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
    testWithIOU()
    {
        using namespace test::jtx;

        struct CaseArgs
        {
            int initialXRP = 1000;
            Number initialIOU = 200;
            double transferRate = 1.0;
        };

        auto testCase =
            [&, this](
                std::function<void(
                    Env & env,
                    Account const& owner,
                    Account const& issuer,
                    Account const& charlie,
                    std::function<Account(ripple::Keylet)> vaultAccount,
                    Vault& vault,
                    PrettyAsset const& asset,
                    std::function<MPTID(ripple::Keylet)> issuanceId)> test,
                CaseArgs args = {}) {
                Env env{*this, testable_amendments() | featureSingleAssetVault};
                Account const owner{"owner"};
                Account const issuer{"issuer"};
                Account const charlie{"charlie"};
                Vault vault{env};
                env.fund(XRP(args.initialXRP), issuer, owner, charlie);
                env(fset(issuer, asfAllowTrustLineClawback));
                env.close();

                PrettyAsset const asset = issuer["IOU"];
                env.trust(asset(1000), owner);
                env.trust(asset(1000), charlie);
                env(pay(issuer, owner, asset(args.initialIOU)));
                env(rate(issuer, args.transferRate));
                env.close();

                auto const vaultAccount =
                    [&env](ripple::Keylet keylet) -> Account {
                    return Account("vault", env.le(keylet)->at(sfAccount));
                };
                auto const issuanceId = [&env](ripple::Keylet keylet) -> MPTID {
                    return env.le(keylet)->at(sfShareMPTID);
                };

                test(
                    env,
                    owner,
                    issuer,
                    charlie,
                    vaultAccount,
                    vault,
                    asset,
                    issuanceId);
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
                    Json::Value jv;
                    jv[jss::Account] = issuer.human();
                    {
                        auto& ja = jv[jss::LimitAmount] =
                            foo(0).value().getJson(JsonOptions::none);
                        ja[jss::issuer] = toBase58(account);
                    }
                    jv[jss::TransactionType] = jss::TrustSet;
                    jv[jss::Flags] = tfSetFreeze;
                    return jv;
                }();
                env(tx, ter{tecNO_PERMISSION});
                env.close();
            }

            {
                auto tx = vault.deposit(
                    {.depositor = issuer, .id = keylet.key, .amount = foo(20)});
                env(tx, ter{tecWRONG_ASSET});
                env.close();
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = issuer, .id = keylet.key, .amount = foo(20)});
                env(tx, ter{tecWRONG_ASSET});
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

            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            Asset const share = Asset(issuanceId(keylet));

            // Freeze the trustline to the vault
            auto trustSet = [&, account = vaultAccount(keylet)]() {
                Json::Value jv;
                jv[jss::Account] = issuer.human();
                {
                    auto& ja = jv[jss::LimitAmount] =
                        asset(0).value().getJson(JsonOptions::none);
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
                auto tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(80)});
                env(tx, ter{tecLOCKED});
            }

            {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx, ter{tecLOCKED});

                // also when trying to withdraw to a 3rd party
                tx[sfDestination] = charlie.human();
                env(tx, ter{tecLOCKED});
                env.close();
            }

            {
                // Clawback works, even when locked
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = asset(50)});
                env(tx);
                env.close();
            }

            // Clear the frozen state
            trustSet[jss::Flags] = tfClearFreeze;
            env(trustSet);
            env.close();

            env(vault.withdraw(
                {.depositor = owner,
                 .id = keylet.key,
                 .amount = share(50'000'000)}));

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

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                env(vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(100)}));
                env.close();

                auto const issue = asset.raw().get<Issue>();
                Asset const share = Asset(issuanceId(keylet));

                // transfer fees ignored on deposit
                BEAST_EXPECT(env.balance(owner, issue) == asset(100));
                BEAST_EXPECT(
                    env.balance(vaultAccount(keylet), issue) == asset(100));

                {
                    auto tx = vault.clawback(
                        {.issuer = issuer,
                         .id = keylet.key,
                         .holder = owner,
                         .amount = asset(50)});
                    env(tx);
                    env.close();
                }

                // transfer fees ignored on clawback
                BEAST_EXPECT(env.balance(owner, issue) == asset(100));
                BEAST_EXPECT(
                    env.balance(vaultAccount(keylet), issue) == asset(50));

                env(vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = share(20'000'000)}));

                // transfer fees ignored on withdraw
                BEAST_EXPECT(env.balance(owner, issue) == asset(120));
                BEAST_EXPECT(
                    env.balance(vaultAccount(keylet), issue) == asset(30));

                {
                    auto tx = vault.withdraw(
                        {.depositor = owner,
                         .id = keylet.key,
                         .amount = share(30'000'000)});
                    tx[sfDestination] = charlie.human();
                    env(tx);
                }

                // transfer fees ignored on withdraw to 3rd party
                BEAST_EXPECT(env.balance(owner, issue) == asset(120));
                BEAST_EXPECT(env.balance(charlie, issue) == asset(30));
                BEAST_EXPECT(
                    env.balance(vaultAccount(keylet), issue) == asset(0));

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

            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            // Withdraw to 3rd party works
            auto const withdrawToCharlie = [&](ripple::Keylet keylet) {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[sfDestination] = charlie.human();
                return tx;
            }(keylet);
            env(withdrawToCharlie);

            // Freeze the owner
            env(trust(issuer, asset(0), owner, tfSetFreeze));
            env.close();

            // Cannot withdraw
            auto const withdraw = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(10)});
            env(withdraw, ter{tecFROZEN});

            // Cannot withdraw to 3rd party
            env(withdrawToCharlie, ter{tecLOCKED});
            env.close();

            {
                // Cannot deposit some more
                auto tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx, ter{tecFROZEN});
            }

            {
                // Clawback still works
                auto tx = vault.clawback(
                    {.issuer = issuer,
                     .id = keylet.key,
                     .holder = owner,
                     .amount = asset(0)});
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

            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            Account const erin{"erin"};
            env.fund(XRP(1000), erin);
            env.close();

            // Withdraw to 3rd party without trust line
            auto const tx1 = [&](ripple::Keylet keylet) {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[sfDestination] = erin.human();
                return tx;
            }(keylet);
            env(tx1, ter{tecNO_LINE});
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

            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(200)}));
            env.close();

            auto trustline =
                env.le(keylet::line(owner, asset.raw().get<Issue>()));
            BEAST_EXPECT(trustline == nullptr);

            // Withdraw without trust line, will succeed
            auto const tx1 = [&](ripple::Keylet keylet) {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
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
                auto const& vaultAccount,
                Vault& vault,
                PrettyAsset const& asset,
                auto&&...) {
                testcase("IOU calculation rounding");

                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                tx[sfScale] = 1;
                env(tx);
                env.close();

                auto const startingOwnerBalance = env.balance(owner, asset);
                BEAST_EXPECT(
                    (startingOwnerBalance.value() ==
                     STAmount{asset, 11875, -2}));

                // This operation (first deposit 100, then 3.75 x 5) is known to
                // have triggered calculation rounding errors in Number
                // (addition and division), causing the last deposit to be
                // blocked by Vault invariants.
                env(vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(100)}));

                auto const tx1 = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(Number(375, -2))});
                for (auto i = 0; i < 5; ++i)
                {
                    env(tx1);
                }
                env.close();

                {
                    STAmount const xfer{asset, 1185, -1};
                    BEAST_EXPECT(
                        env.balance(owner, asset) ==
                        startingOwnerBalance.value() - xfer);
                    BEAST_EXPECT(
                        env.balance(vaultAccount(keylet), asset) == xfer);

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
                     .amount = asset(Number(1000 + 37 * 5, -1))}));

                {
                    BEAST_EXPECT(
                        env.balance(owner, asset) ==
                        startingOwnerBalance.value());
                    BEAST_EXPECT(
                        env.balance(vaultAccount(keylet), asset) ==
                        beast::zero);
                    auto const vault = env.le(keylet);
                    BEAST_EXPECT(vault->at(sfAssetsAvailable) == beast::zero);
                    BEAST_EXPECT(vault->at(sfAssetsTotal) == beast::zero);
                }

                env(vault.del({.owner = owner, .id = keylet.key}));
                env.close();
            },
            {.initialIOU = Number(11875, -2)});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env env{*this, testable_amendments()};
            return {
                env.current()->fees().accountReserve(0).drops() /
                    DROPS_PER_XRP.drops(),
                env.current()->fees().increment.drops() /
                    DROPS_PER_XRP.drops()};
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
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                // reset limit, so deposit of all funds will delete the trust
                // line
                env.trust(asset(0), owner);
                env.close();

                env(vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(200)}));
                env.close();

                auto trustline =
                    env.le(keylet::line(owner, asset.raw().get<Issue>()));
                BEAST_EXPECT(trustline == nullptr);

                env(ticket::create(owner, 1));
                env.close();

                // Fail because not enough reserve to create trust line
                tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx, ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                env(pay(charlie, owner, XRP(incReserve)));
                env.close();

                // Withdraw can now create trust line, will succeed
                env(tx);
                env.close();
            },
            CaseArgs{.initialXRP = acctReserve + incReserve * 4 + 1});

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
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset});
                env(tx);
                env.close();

                env(pay(owner, charlie, asset(100)));
                env.close();

                env(ticket::create(charlie, 3));
                env.close();

                // Fail because not enough reserve to create MPToken for shares
                tx = vault.deposit(
                    {.depositor = charlie,
                     .id = keylet.key,
                     .amount = asset(100)});
                env(tx, ter{tecINSUFFICIENT_RESERVE});
                env.close();

                env(pay(issuer, charlie, XRP(incReserve)));
                env.close();

                // Deposit can now create MPToken, will succeed
                env(tx);
                env.close();
            },
            CaseArgs{.initialXRP = acctReserve + incReserve * 4 + 1});

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

            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            // Withdraw to 3rd party works
            auto const withdrawToCharlie = [&](ripple::Keylet keylet) {
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                tx[sfDestination] = charlie.human();
                return tx;
            }(keylet);
            env(withdrawToCharlie);

            // Freeze the 3rd party
            env(trust(issuer, asset(0), charlie, tfSetFreeze));
            env.close();

            // Can withdraw
            auto const withdraw = vault.withdraw(
                {.depositor = owner, .id = keylet.key, .amount = asset(10)});
            env(withdraw);
            env.close();

            // Cannot withdraw to 3rd party
            env(withdrawToCharlie, ter{tecFROZEN});
            env.close();

            env(vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = owner,
                 .amount = asset(0)}));
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

            env(vault.deposit(
                {.depositor = owner, .id = keylet.key, .amount = asset(100)}));
            env.close();

            env(fset(issuer, asfGlobalFreeze));
            env.close();

            {
                // Cannot withdraw
                auto tx = vault.withdraw(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});
                env(tx, ter{tecFROZEN});

                // Cannot withdraw to 3rd party
                tx[sfDestination] = charlie.human();
                env(tx, ter{tecFROZEN});
                env.close();

                // Cannot deposit some more
                tx = vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(10)});

                env(tx, ter{tecFROZEN});
            }

            // Clawback is permitted
            env(vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = owner,
                 .amount = asset(0)}));
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

        Env env{*this, testable_amendments() | featureSingleAssetVault};
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
        env.trust(asset(1000), charlie);
        env(pay(issuer, charlie, asset(5)));
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

                // Should be idempotent
                tx = vault.set({.owner = owner, .id = keylet.key});
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
            // charlie's credential not accepted
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
            env(tx, ter{tecNO_AUTH});
            env.close();
        }

        {
            testcase("private vault depositor lost authorization");
            env(credentials::deleteCred(
                credIssuer1, depositor, credIssuer1, credType));
            env(credentials::deleteCred(
                credIssuer1, charlie, credIssuer1, credType));
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

        auto const shares = [&env, keylet = keylet, this]() -> Asset {
            auto const vault = env.le(keylet);
            BEAST_EXPECT(vault != nullptr);
            return MPTIssue(vault->at(sfShareMPTID));
        }();

        {
            testcase("private vault expired authorization");
            uint32_t const closeTime = env.current()
                                           ->info()
                                           .parentCloseTime.time_since_epoch()
                                           .count();
            {
                auto tx0 =
                    credentials::create(depositor, credIssuer2, credType);
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
                auto tx1 = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(50)});
                env(tx1);
                env.close();

                auto const tokenKeylet = keylet::mptoken(
                    shares.get<MPTIssue>().getMptID(), depositor.id());
                BEAST_EXPECT(env.le(tokenKeylet) != nullptr);
            }

            {
                // time advance
                env.close();
                env.close();
                env.close();

                auto const credsKeylet =
                    credentials::keylet(depositor, credIssuer2, credType);
                BEAST_EXPECT(env.le(credsKeylet) != nullptr);

                auto tx2 = vault.deposit(
                    {.depositor = depositor,
                     .id = keylet.key,
                     .amount = asset(1)});
                env(tx2, ter{tecEXPIRED});
                env.close();

                BEAST_EXPECT(env.le(credsKeylet) == nullptr);
            }

            {
                auto const credsKeylet =
                    credentials::keylet(charlie, credIssuer2, credType);
                BEAST_EXPECT(env.le(credsKeylet) != nullptr);
                auto const tokenKeylet = keylet::mptoken(
                    shares.get<MPTIssue>().getMptID(), charlie.id());
                BEAST_EXPECT(env.le(tokenKeylet) == nullptr);

                auto tx3 = vault.deposit(
                    {.depositor = charlie,
                     .id = keylet.key,
                     .amount = asset(2)});
                env(tx3, ter{tecEXPIRED});

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

            tx = vault.deposit(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx, ter{tecNO_AUTH});
            env.close();

            tx = vault.withdraw(
                {.depositor = depositor,
                 .id = keylet.key,
                 .amount = asset(50)});
            env(tx);
            env.close();

            tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = depositor,
                 .amount = asset(0)});
            env(tx);

            tx = vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = owner,
                 .amount = asset(0)});
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
    testWithDomainCheckXRP()
    {
        using namespace test::jtx;

        testcase("private XRP vault");

        Env env{*this, testable_amendments() | featureSingleAssetVault};
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
            env.close();
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
    testFailedPseudoAccount()
    {
        using namespace test::jtx;

        testcase("fail pseudo-account allocation");
        Env env{*this, testable_amendments() | featureSingleAssetVault};
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
            ripple::Keylet keylet;
            Issue assets;
            PrettyAsset const& asset;
            std::function<bool(std::function<bool(SLE&, SLE&)>)> peek;
        };

        auto testCase = [&, this](
                            std::uint8_t scale,
                            std::function<void(Env & env, Data data)> test) {
            Env env{*this, testable_amendments() | featureSingleAssetVault};
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
                [&env](ripple::Keylet keylet) -> std::tuple<Account, MPTID> {
                auto const vault = env.le(keylet);
                return {
                    Account("vault", vault->at(sfAccount)),
                    vault->at(sfShareMPTID)};
            }(keylet);
            MPTIssue shares(issuanceId);
            env.memoize(vaultAccount);

            auto const peek =
                [=, &env, this](std::function<bool(SLE&, SLE&)> fn) -> bool {
                return env.app().openLedger().modify(
                    [&](OpenView& view, beast::Journal j) -> bool {
                        Sandbox sb(&view, tapNONE);
                        auto vault = sb.peek(keylet::vault(keylet.key));
                        if (!BEAST_EXPECT(vault != nullptr))
                            return false;
                        auto shares = sb.peek(
                            keylet::mptIssuance(vault->at(sfShareMPTID)));
                        if (!BEAST_EXPECT(shares != nullptr))
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
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = d.asset(10)});
            env(tx, ter{tecPATH_DRY});
            env.close();
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale deposit overflow on second deposit");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = d.asset(10)});
                env(tx, ter{tecPATH_DRY});
                env.close();
            }
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale deposit overflow on total shares");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = d.asset(5)});
                env(tx, ter{tecPATH_DRY});
                env.close();
            }
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit exact");

            auto const start = env.balance(d.depositor, d.assets).number();
            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = d.asset(1)});
            env(tx);
            env.close();
            BEAST_EXPECT(env.balance(d.depositor, d.shares) == d.share(10));
            BEAST_EXPECT(
                env.balance(d.depositor, d.assets) ==
                STAmount(d.asset, start - 1));
        });

        testCase(1, [&, this](Env& env, Data d) {
            testcase("Scale deposit insignificant amount");

            auto tx = d.vault.deposit(
                {.depositor = d.depositor,
                 .id = d.keylet.key,
                 .amount = STAmount(d.asset, Number(9, -2))});
            env(tx, ter{tecPRECISION_LOSS});
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
                env.balance(d.depositor, d.assets) ==
                STAmount(d.asset, start - Number(15, -1)));
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
                env.balance(d.depositor, d.assets) ==
                STAmount(d.asset, start - Number(12, -1)));

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
                env.balance(d.depositor, d.assets) ==
                STAmount(d.asset, start - Number(12, -1)));

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
                env.balance(d.depositor, d.assets) ==
                STAmount(d.asset, start - Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.assets) ==
                STAmount(d.asset, Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.shares) ==
                STAmount(d.share, Number(-1000, 0)));

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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(900));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(10, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(90, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, Number(-900, 0)));
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
                env(tx, ter{tecINSUFFICIENT_FUNDS});
                env.close();
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(900 - 25));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(875 - 21));
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
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets).number() == 0);
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares).number() == 0);
            }
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale withdraw overflow");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(10, 0))});
                env(tx, ter{tecPATH_DRY});
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
                env.balance(d.depositor, d.assets) ==
                STAmount(d.asset, start - Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.assets) ==
                STAmount(d.asset, Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.shares) ==
                STAmount(d.share, Number(-1000, 0)));

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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(900));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(10, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(90, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, Number(-900, 0)));
            }

            {
                testcase("Scale withdraw insignificant amount");
                auto tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, Number(4, -2))});
                env(tx, ter{tecPRECISION_LOSS});
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
                env(tx, ter{tecINSUFFICIENT_FUNDS});
                env.close();
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(900 - 25));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(875 - 38));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(837 - 37));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(800 - 1));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start + Number(1, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(800 - 1, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(800 - 1, 0)));
            }

            {
                testcase("Scale withdraw rest");
                auto const rest =
                    env.balance(d.vaultAccount, d.assets).number();

                tx = d.vault.withdraw(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = STAmount(d.asset, rest)});
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(d.depositor, d.shares).number() == 0);
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets).number() == 0);
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares).number() == 0);
            }
        });

        testCase(18, [&, this](Env& env, Data d) {
            testcase("Scale clawback overflow");

            {
                auto tx = d.vault.deposit(
                    {.depositor = d.depositor,
                     .id = d.keylet.key,
                     .amount = d.asset(5)});
                env(tx);
                env.close();
            }

            {
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(10, 0))});
                env(tx, ter{tecPATH_DRY});
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
                env.balance(d.depositor, d.assets) ==
                STAmount(d.asset, start - Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.assets) ==
                STAmount(d.asset, Number(100, 0)));
            BEAST_EXPECT(
                env.balance(d.vaultAccount, d.shares) ==
                STAmount(d.share, -Number(1000, 0)));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(900));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(90, 0)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(900, 0)));
            }

            {
                testcase("Scale clawback insignificant amount");
                auto tx = d.vault.clawback(
                    {.issuer = d.issuer,
                     .id = d.keylet.key,
                     .holder = d.depositor,
                     .amount = STAmount(d.asset, Number(4, -2))});
                env(tx, ter{tecPRECISION_LOSS});
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(900 - 25));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(875 - 38));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(837 - 37));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start));
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
                BEAST_EXPECT(
                    env.balance(d.depositor, d.shares) == d.share(800 - 1));
                BEAST_EXPECT(
                    env.balance(d.depositor, d.assets) ==
                    STAmount(d.asset, start));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets) ==
                    STAmount(d.asset, Number(800 - 1, -1)));
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares) ==
                    STAmount(d.share, -Number(800 - 1, 0)));
            }

            {
                testcase("Scale clawback rest");
                auto const rest =
                    env.balance(d.vaultAccount, d.assets).number();
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
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.assets).number() == 0);
                BEAST_EXPECT(
                    env.balance(d.vaultAccount, d.shares).number() == 0);
            }
        });
    }

    void
    testRPC()
    {
        using namespace test::jtx;

        testcase("RPC");
        Env env{*this, testable_amendments() | featureSingleAssetVault};
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
                BEAST_EXPECT(
                    checkString(issuance, sfOutstandingAmount, "50000000"));
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
            testcase("RPC ledger_entry bool seq");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault][jss::owner] = issuer.human();
            jvParams[jss::vault][jss::seq] = true;
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
            testcase("RPC vault_info invalid vault_id");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::vault_id] = "foobar";
            auto jv = env.rpc("json", "vault_info", to_string(jvParams));
            BEAST_EXPECT(
                jv[jss::result][jss::error].asString() == "malformedRequest");
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
            testcase("RPC vault_info json bool sequence");
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::owner] = owner.human();
            jvParams[jss::seq] = true;
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
                "RPC vault_info json invalid combination owner seq "
                "vault_id");
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

    void
    testDelegate()
    {
        using namespace test::jtx;

        Env env(*this, testable_amendments());
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};

        struct CaseArgs
        {
            PrettyAsset asset = xrpIssue();
        };

        auto const xrpBalance =
            [this](
                Env const& env, Account const& account) -> std::optional<long> {
            auto sle = env.le(keylet::account(account.id()));
            if (BEAST_EXPECT(sle != nullptr))
                return sle->getFieldAmount(sfBalance).xrp().drops();
            return std::nullopt;
        };

        auto testCase = [&, this](auto test, CaseArgs args = {}) {
            Env env{*this, testable_amendments() | featureSingleAssetVault};

            Vault vault{env};

            // use different initial amount to distinguish the source balance
            env.fund(XRP(10000), alice);
            env.fund(XRP(20000), bob);
            env.fund(XRP(30000), carol);
            env.close();

            env(delegate::set(
                carol,
                alice,
                {"Payment",
                 "VaultCreate",
                 "VaultSet",
                 "VaultDelete",
                 "VaultDeposit",
                 "VaultWithdraw",
                 "VaultClawback"}));

            test(env, vault, args.asset);
        };

        testCase([&, this](Env& env, Vault& vault, PrettyAsset const& asset) {
            testcase("delegated vault creation");
            auto startBalance = xrpBalance(env, carol);
            if (!BEAST_EXPECT(startBalance.has_value()))
                return;

            auto [tx, keylet] = vault.create({.owner = carol, .asset = asset});
            env(tx, delegate::as(alice));
            env.close();
            BEAST_EXPECT(xrpBalance(env, carol) == *startBalance);
        });

        testCase([&, this](Env& env, Vault& vault, PrettyAsset const& asset) {
            testcase("delegated deposit and withdrawal");
            auto [tx, keylet] = vault.create({.owner = carol, .asset = asset});
            env(tx);
            env.close();

            auto const amount = 1513;
            auto const baseFee = env.current()->fees().base;

            auto startBalance = xrpBalance(env, carol);
            if (!BEAST_EXPECT(startBalance.has_value()))
                return;

            tx = vault.deposit(
                {.depositor = carol,
                 .id = keylet.key,
                 .amount = asset(amount)});
            env(tx, delegate::as(alice));
            env.close();
            BEAST_EXPECT(xrpBalance(env, carol) == *startBalance - amount);

            tx = vault.withdraw(
                {.depositor = carol,
                 .id = keylet.key,
                 .amount = asset(amount - 1)});
            env(tx, delegate::as(alice));
            env.close();
            BEAST_EXPECT(xrpBalance(env, carol) == *startBalance - 1);

            tx = vault.withdraw(
                {.depositor = carol, .id = keylet.key, .amount = asset(1)});
            env(tx);
            env.close();
            BEAST_EXPECT(xrpBalance(env, carol) == *startBalance - baseFee);
        });

        testCase([&, this](Env& env, Vault& vault, PrettyAsset const& asset) {
            testcase("delegated withdrawal same as base fee and deletion");
            auto [tx, keylet] = vault.create({.owner = carol, .asset = asset});
            env(tx);
            env.close();

            auto const amount = 25537;
            auto const baseFee = env.current()->fees().base;

            auto startBalance = xrpBalance(env, carol);
            if (!BEAST_EXPECT(startBalance.has_value()))
                return;

            tx = vault.deposit(
                {.depositor = carol,
                 .id = keylet.key,
                 .amount = asset(amount)});
            env(tx);
            env.close();
            BEAST_EXPECT(
                xrpBalance(env, carol) == *startBalance - amount - baseFee);

            tx = vault.withdraw(
                {.depositor = carol,
                 .id = keylet.key,
                 .amount = asset(baseFee)});
            env(tx, delegate::as(alice));
            env.close();
            BEAST_EXPECT(xrpBalance(env, carol) == *startBalance - amount);

            tx = vault.withdraw(
                {.depositor = carol,
                 .id = keylet.key,
                 .amount = asset(amount - baseFee)});
            env(tx, delegate::as(alice));
            env.close();
            BEAST_EXPECT(xrpBalance(env, carol) == *startBalance - baseFee);

            tx = vault.del({.owner = carol, .id = keylet.key});
            env(tx, delegate::as(alice));
            env.close();
        });
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
        testScaleIOU();
        testRPC();
        testDelegate();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, app, ripple, 1);

}  // namespace ripple

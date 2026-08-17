#include <test/app/vault/VaultTestBase.h>
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
#include <test/jtx/noop.h>
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
#include <xrpl/basics/chrono.h>
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
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultLifecycle_test : public VaultTestBase
{
private:
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
                auto const shares = env.le(keylet::mptokenIssuance(vault->at(sfShareMPTID)));
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
    testWithMPT()
    {
        using namespace test::jtx;

        struct CaseArgs
        {
            bool enableClawback = true;
            bool requireAuth = true;
            int initialXRP = 1000;
            FeatureBitset features = testableAmendments();
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
            Env env{*this, args.features};
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
                     (args.requireAuth ? tfMPTRequireAuth : kNone)});
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
                env.current()->fees().accountReserve(0, 1).drops() / kDropsPerXrp.drops(),
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
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
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

            // CanTrade is not set on the underlying, both the asset and
            // the vault share are blocked on the DEX.
            env(offer(alice, XRP(1), asset(10)), Ter{tecNO_PERMISSION});
            env(offer(alice, XRP(1), shares(1)), Ter{tecNO_PERMISSION});
            env.close();

            // Deposit still works before enabling CanTrade.
            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(100)}));
            env.close();

            // Peer-to-peer share transfers still work (CanTransfer is set on
            // both layers).
            env(pay(alice, bob, shares(1)));
            env.close();

            // Withdraw still works before enabling CanTrade.
            env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = asset(100)}));
            env.close();

            // Enable CanTrade on the underlying.
            mptt.set({.flags = tfMPTSetCanTrade});
            env.close();

            env(offer(alice, XRP(1), asset(10)));
            env(offer(alice, XRP(1), shares(1)));
            env.close();

            AMM const ammUnderlying(env, alice, XRP(1'000), asset(1'000));
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
            FeatureBitset features = testableAmendments();
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
            Env env{*this, args.features};
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

            auto trustline = env.le(keylet::trustLine(owner, asset.raw().get<Issue>()));
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
                env.current()->fees().accountReserve(0, 1).drops() / kDropsPerXrp.drops(),
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

                auto trustline = env.le(keylet::trustLine(owner, asset.raw().get<Issue>()));
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
    }

public:
    void
    run() override
    {
        testSequences();
        testWithMPT();
        testWithIOU();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(VaultLifecycle, app, xrpl, 1);

}  // namespace xrpl

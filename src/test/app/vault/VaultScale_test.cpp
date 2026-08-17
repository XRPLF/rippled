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

class VaultScale_test : public VaultTestBase
{
private:
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
                        auto shares = sb.peek(keylet::mptokenIssuance(vault->at(sfShareMPTID)));
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
            using namespace loan_broker;
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
            auto const brokerKeylet =
                keylet::loanBroker(d.owner.id(), SeqProxy::rawSequence(env.seq(d.owner)));
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

        auto const maxInt64Plus1 = std::to_string(
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1);
        BEAST_EXPECT(maxInt64Plus1 == "9223372036854775808");

        // Naming things is hard
        auto const maxInt64Plus2 = std::to_string(
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 2);
        BEAST_EXPECT(maxInt64Plus2 == "9223372036854775809");

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

            // There are several parse failures expected in this function, so just disable it once.
            env.setParseFailureExpected(true);
            try
            {
                tx[sfAssetsMaximum] = maxInt64Plus1;
                env(tx, Ter(tefEXCEPTION));
                env.close();
                // should throw in parser
                fail();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "invalidParamsField 'tx_json.AssetsMaximum' has invalid data.");
            }

            try
            {
                tx[sfAssetsMaximum] = maxInt64Plus2;
                env(tx, Ter(tefEXCEPTION));
                // should throw in parser
                fail();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "invalidParamsField 'tx_json.AssetsMaximum' has invalid data.");
            }

            auto const newKeylet = keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
            try
            {
                auto const insertAt = maxInt64Plus2.size() - 3;
                auto const decimalTest = maxInt64Plus2.substr(0, insertAt) + "." +
                    maxInt64Plus2.substr(insertAt);  // (max int64+2) / 1000
                BEAST_EXPECT(decimalTest == "9223372036854775.809");
                tx[sfAssetsMaximum] = decimalTest;
                env(tx);
                // should throw in parser
                fail();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "invalidParamsField 'tx_json.AssetsMaximum' has invalid data.");
            }

            auto const vaultSle = env.le(newKeylet);
            BEAST_EXPECT(!vaultSle);
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

            try
            {
                tx[sfAssetsMaximum] = maxInt64Plus2;
                env(tx, Ter(tefEXCEPTION));
                // should throw in parser
                fail();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "invalidParamsField 'tx_json.AssetsMaximum' has invalid data.");
            }

            auto const newKeylet = keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
            try
            {
                auto const insertAt = maxInt64Plus2.size() - 1;
                auto const decimalTest = maxInt64Plus2.substr(0, insertAt) + "." +
                    maxInt64Plus2.substr(insertAt);  // (max int64+2) / 10
                BEAST_EXPECT(decimalTest == "922337203685477580.9");
                tx[sfAssetsMaximum] = decimalTest;
                env(tx);
                // should throw in parser
                fail();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "invalidParamsField 'tx_json.AssetsMaximum' has invalid data.");
            }

            auto const vaultSle = env.le(newKeylet);
            BEAST_EXPECT(!vaultSle);
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

            // Since several tests are expected to have parser failures, leave this flag set for the
            // remainder of this function.
            env.setParseFailureExpected(true);
            try
            {
                tx[sfAssetsMaximum] = maxInt64Plus2;
                env(tx);
                // should throw in parser
                fail();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "invalidParamsField 'tx_json.AssetsMaximum' has invalid data.");
            }

            tx[sfAssetsMaximum] = "1000000000000000e80";
            env.close();

            tx[sfAssetsMaximum] = "1000000000000000e-96";
            env.close();

            // These values will be rounded to 15 significant digits
            {
                auto const newKeylet =
                    keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
                try
                {
                    auto const insertAt = maxInt64Plus2.size() - 1;
                    auto const decimalTest = maxInt64Plus2.substr(0, insertAt) + "." +
                        maxInt64Plus2.substr(insertAt);  // (max int64+2) / 10
                    BEAST_EXPECT(decimalTest == "922337203685477580.9");
                    tx[sfAssetsMaximum] = decimalTest;
                    env(tx);
                    // should throw in parser
                    fail();
                }
                catch (std::exception const& e)
                {
                    BEAST_EXPECT(
                        std::string(e.what()) ==
                        "invalidParamsField 'tx_json.AssetsMaximum' has invalid data.");
                }

                auto const vaultSle = env.le(newKeylet);
                BEAST_EXPECT(!vaultSle);
            }
            {
                tx[sfAssetsMaximum] = "9223372036854775807e40";  // max int64 * 10^40
                auto const newKeylet =
                    keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
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
                auto const newKeylet =
                    keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
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
                auto const newKeylet =
                    keylet::vault(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
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
            try
            {
                auto const g = env.getParseFailureGuard(true);
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
        }
    }

public:
    void
    run() override
    {
        testScaleIOU();
        testAssetsMaximum();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(VaultScale, app, xrpl, 1);

}  // namespace xrpl

#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <optional>
#include <source_location>
#include <utility>

namespace xrpl {

/**
 * Tests for XLS-65.4: the optional, immutable EarlyExitFeeRate on a
 * closed-ended vault, which permits VaultWithdraw during the Investment phase
 * and retains the fee in the vault for the remaining shareholders.
 */
class VaultEarlyExitFee_test : public VaultTestBase
{
    static FeatureBitset
    features()
    {
        return test::jtx::testableAmendments() | featureLendingProtocolV1_1 |
            featureLendingProtocolV1_2;
    }

    static constexpr std::uint32_t kTwoPercent = 2'000;
    static constexpr std::uint32_t kMaxRate = kMaxEarlyExitFeeRate.value();

    // Vault holds an Env& so no default initializer is possible; the
    // struct is always aggregate-initialized by makeVault.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    struct Setup
    {
        test::jtx::Vault vault;
        Keylet keylet;
        MPTIssue shares;
        std::uint32_t sub = 0;
        std::uint32_t red = 0;
    };
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)

    // Create a closed-ended vault with SubscriptionDate 60s from now and a
    // window wide enough to originate a single-payment loan during Investment.
    Setup
    makeVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        Asset const& asset,
        std::optional<std::uint32_t> feeRate)
    {
        auto const sub = env.now().time_since_epoch().count() + 60;
        auto const red = sub + kMinInvestmentPeriod + 3600;
        test::jtx::Vault const vault{env};
        auto [tx, keylet] = vault.create(
            {.owner = owner,
             .asset = asset,
             .vaultKind = std::to_underlying(VaultKind::ClosedEnded),
             .subscriptionDate = sub,
             .redemptionDate = red,
             .earlyExitFeeRate = feeRate});
        env(tx);
        env.close();
        auto const sle = env.le(keylet);
        BEAST_EXPECT(sle);
        MPTIssue const shares{sle ? sle->at(sfShareMPTID) : uint192{}};
        return {.vault = vault, .keylet = keylet, .shares = shares, .sub = sub, .red = red};
    }

    void
    enterInvestment(test::jtx::Env& env, Setup const& s)
    {
        closeToTime(env, Tp{D{s.sub}} + getLedgerTimeResolution(env));
    }

    // env.balance(account, mptIssue) cannot resolve the vault pseudo-account
    // issuer, so read the MPToken directly.
    static std::uint64_t
    sharesOf(test::jtx::Env& env, Setup const& s, test::jtx::Account const& holder)
    {
        auto const sle = env.le(keylet::mptoken(s.shares.getMptID(), holder.id()));
        return sle ? sle->getFieldU64(sfMPTAmount) : 0u;
    }

    static std::uint64_t
    sharesOutstanding(test::jtx::Env& env, Setup const& s)
    {
        auto const sle = env.le(keylet::mptokenIssuance(s.shares.getMptID()));
        return sle ? sle->getFieldU64(sfOutstandingAmount) : 0u;
    }

    void
    expectTotals(
        test::jtx::Env& env,
        Setup const& s,
        STAmount const& total,
        STAmount const& available,
        std::source_location const& loc = std::source_location::current())
    {
        auto const sle = env.le(s.keylet);
        if (!BEAST_EXPECT(sle))
            return;
        expect(sle->at(sfAssetsTotal) == total, "AssetsTotal", loc.file_name(), loc.line());
        expect(
            sle->at(sfAssetsAvailable) == available,
            "AssetsAvailable",
            loc.file_name(),
            loc.line());
    }

    void
    testCreate()
    {
        using namespace test::jtx;
        auto const closedEnded = std::to_underlying(VaultKind::ClosedEnded);
        Asset const asset = xrpIssue();

        auto const create = [&](FeatureBitset features,
                                std::optional<std::uint8_t> kind,
                                std::optional<std::uint32_t> feeRate,
                                TER expected,
                                std::source_location const& loc = std::source_location::current()) {
            Env env{*this, features};
            Account const owner{"owner"};
            env.fund(XRP(1000), owner);
            env.close();

            Vault const vault{env};
            auto const sub = env.now().time_since_epoch().count() + 60;
            bool const closed = kind == closedEnded;
            std::optional<std::uint32_t> subscription;
            std::optional<std::uint32_t> redemption;
            if (closed)
            {
                subscription = sub;
                redemption = sub + kMinInvestmentPeriod;
            }
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = kind,
                 .subscriptionDate = subscription,
                 .redemptionDate = redemption,
                 .earlyExitFeeRate = feeRate});
            env(WithSourceLocation{tx, loc}, Ter{expected});
            env.close();

            auto const sle = env.le(keylet);
            if (!isTesSuccess(expected))
            {
                expect(!sle, "vault must not exist", loc.file_name(), loc.line());
                return;
            }
            if (!expect(sle != nullptr, "vault must exist", loc.file_name(), loc.line()))
                return;
            expect(
                sle->at(~sfEarlyExitFeeRate) == feeRate,
                "EarlyExitFeeRate stored as submitted",
                loc.file_name(),
                loc.line());
        };

        testcase("VaultCreate stores EarlyExitFeeRate");
        create(features(), closedEnded, kTwoPercent, tesSUCCESS);
        // Zero is stored, distinct from an absent field.
        create(features(), closedEnded, 0u, tesSUCCESS);
        create(features(), closedEnded, kMaxRate, tesSUCCESS);
        create(features(), closedEnded, std::nullopt, tesSUCCESS);

        testcase("VaultCreate rejects malformed EarlyExitFeeRate");
        create(features(), closedEnded, kMaxRate + 1, temMALFORMED);
        create(features(), std::to_underlying(VaultKind::OpenEnded), 0u, temMALFORMED);
        create(features(), std::nullopt, 0u, temMALFORMED);
        create(features(), std::nullopt, kTwoPercent, temMALFORMED);

        testcase("VaultCreate EarlyExitFeeRate amendment gates");
        create(features() - featureLendingProtocolV1_2, closedEnded, kTwoPercent, temDISABLED);
        create(features() - featureLendingProtocolV1_2, closedEnded, 0u, temDISABLED);
    }

    // The worked example of XLS-65.4 3.4.3.1, scaled to XRP: shares are burned
    // against the pre-fee amount, only the post-fee amount leaves the vault,
    // and the retained fee raises the value of every remaining share.
    void
    testWithdrawChargesFee()
    {
        using namespace test::jtx;

        Env env{*this, features()};
        Account const owner{"owner"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), owner, alice, bob);
        env.close();
        XRPAmount const baseFee = env.current()->fees().base;

        auto const s = makeVault(env, owner, xrpIssue(), kTwoPercent);
        env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = XRP(900)}));
        env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = XRP(100)}));
        env.close();
        enterInvestment(env, s);

        {
            testcase("asset-denominated early exit is charged the fee");
            auto const before = env.balance(bob).value().xrp();
            env(s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(50)}));
            env.close();

            // 2% of 50 XRP is 1 XRP, so 49 XRP is paid and 1 XRP stays behind.
            BEAST_EXPECT(
                env.balance(bob).value().xrp() == before + XRP(49).value().xrp() - baseFee);
            BEAST_EXPECT(sharesOf(env, s, bob) == 50'000'000);
            BEAST_EXPECT(sharesOutstanding(env, s) == 950'000'000);
            expectTotals(env, s, XRP(951).value(), XRP(951).value());
        }

        {
            testcase("share-denominated early exit is charged the fee");
            auto const before = env.balance(bob).value().xrp();
            // The retained fee raised each share's value: 9.5M shares were worth 9.5 XRP at the
            // start and are now worth 9.51 XRP (951 XRP / 950M shares) before the fee.
            env(s.vault.withdraw(
                {.depositor = bob,
                 .id = s.keylet.key,
                 .amount = STAmount{s.shares, std::uint64_t{9'500'000}}}));
            env.close();

            XRPAmount const preFee{9'510'000};
            XRPAmount const fee{190'200};
            BEAST_EXPECT(env.balance(bob).value().xrp() == before + (preFee - fee) - baseFee);
            BEAST_EXPECT(sharesOf(env, s, bob) == 40'500'000);
            BEAST_EXPECT(sharesOutstanding(env, s) == 940'500'000);
            STAmount const total{XRP(951).value().xrp() - (preFee - fee)};
            expectTotals(env, s, total, total);
        }
    }

    // Absent, zero and 100% rates during Investment.
    void
    testWithdrawRates()
    {
        using namespace test::jtx;

        auto const withVault = [&](std::optional<std::uint32_t> rate, auto&& body) {
            Env env{*this, features()};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), owner, alice, bob);
            env.close();
            auto const s = makeVault(env, owner, xrpIssue(), rate);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = XRP(900)}));
            env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = XRP(100)}));
            env.close();
            enterInvestment(env, s);
            body(env, s, bob, env.current()->fees().base);
        };

        testcase("absent rate keeps the Investment-phase gate");
        withVault(std::nullopt, [&](Env& env, Setup const& s, Account const& bob, XRPAmount) {
            env(s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(10)}),
                Ter{tecTOO_SOON});
            env(s.vault.withdraw(
                    {.depositor = bob,
                     .id = s.keylet.key,
                     .amount = STAmount{s.shares, std::uint64_t{10'000'000}}}),
                Ter{tecTOO_SOON});
            auto tx = s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(10)});
            tx[sfDestination] = Account{"alice"}.human();
            env(tx, Ter{tecTOO_SOON});
        });

        testcase("zero rate permits a free early exit");
        withVault(0u, [&](Env& env, Setup const& s, Account const& bob, XRPAmount baseFee) {
            auto const before = env.balance(bob).value().xrp();
            env(s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(10)}));
            env.close();
            BEAST_EXPECT(
                env.balance(bob).value().xrp() == before + XRP(10).value().xrp() - baseFee);
            expectTotals(env, s, XRP(990).value(), XRP(990).value());
        });

        testcase("100% rate burns shares and pays nothing");
        withVault(kMaxRate, [&](Env& env, Setup const& s, Account const& bob, XRPAmount baseFee) {
            auto const before = env.balance(bob).value().xrp();
            env(s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(10)}));
            env.close();
            BEAST_EXPECT(env.balance(bob).value().xrp() == before - baseFee);
            BEAST_EXPECT(sharesOf(env, s, bob) == 90'000'000);
            BEAST_EXPECT(sharesOutstanding(env, s) == 990'000'000);
            expectTotals(env, s, XRP(1000).value(), XRP(1000).value());
        });

        testcase("early exit to a destination delivers the post-fee amount");
        withVault(
            kTwoPercent, [&](Env& env, Setup const& s, Account const& bob, XRPAmount baseFee) {
                Account const carol{"carol"};
                env.fund(XRP(1000), carol);
                env.close();
                auto const bobBefore = env.balance(bob).value().xrp();
                auto const carolBefore = env.balance(carol).value().xrp();
                auto tx =
                    s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(50)});
                tx[sfDestination] = carol.human();
                env(tx);
                env.close();
                BEAST_EXPECT(env.balance(bob).value().xrp() == bobBefore - baseFee);
                BEAST_EXPECT(
                    env.balance(carol).value().xrp() == carolBefore + XRP(49).value().xrp());
            });
    }

    void
    testWithdrawRounding()
    {
        using namespace test::jtx;

        testcase("fee rounds up to at least one drop");
        {
            Env env{*this, features()};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), owner, alice, bob);
            env.close();
            XRPAmount const baseFee = env.current()->fees().base;

            // 0.001%: the exact fee on 1,000 drops is 0.01 drops.
            auto const s = makeVault(env, owner, xrpIssue(), 1u);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = XRP(900)}));
            env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = XRP(100)}));
            env.close();
            enterInvestment(env, s);

            auto const before = env.balance(bob).value().xrp();
            env(s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = drops(1'000)}));
            env.close();
            BEAST_EXPECT(env.balance(bob).value().xrp() == before + XRPAmount{999} - baseFee);
        }

        // A fresh vault keeps the exchange rate at 1:1, so the single unit
        // redeems exactly one share rather than truncating to zero shares.
        testcase("fee consuming the whole withdrawal below 100% pays nothing");
        {
            Env env{*this, features()};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), owner, alice, bob);
            env.close();
            XRPAmount const baseFee = env.current()->fees().base;

            // 0.001%: the exact payout on 1 drop is 0.99999 drops, which
            // rounds down to zero, so the whole drop is retained as the fee.
            auto const s = makeVault(env, owner, xrpIssue(), 1u);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = XRP(900)}));
            env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = XRP(100)}));
            env.close();
            enterInvestment(env, s);

            auto const before = env.balance(bob).value().xrp();
            env(s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = drops(1)}));
            env.close();
            BEAST_EXPECT(env.balance(bob).value().xrp() == before - baseFee);
            BEAST_EXPECT(sharesOf(env, s, bob) == 99'999'999);
            BEAST_EXPECT(sharesOutstanding(env, s) == 999'999'999);
            expectTotals(env, s, XRP(1000).value(), XRP(1000).value());
        }

        testcase("IOU fee rounds up at the posterior live scale");
        {
            Env env{*this, features()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), issuer, owner, alice, bob);
            env.close();
            PrettyAsset const iou = issuer[iouCurrency_];
            env.trust(iou(10'000), alice, bob);
            env(pay(issuer, alice, iou(1'000)));
            env(pay(issuer, bob, iou(1'000)));
            env.close();

            auto const s = makeVault(env, owner, iou, 1u);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = iou(1'000)}));
            env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = iou(100)}));
            env.close();
            enterInvestment(env, s);

            // Scale 6: the exact fee on 1.234567 is 0.00001234567, which
            // rounds up to 0.000013, so 1.234554 is paid.
            env(s.vault.withdraw(
                {.depositor = bob, .id = s.keylet.key, .amount = iou(Number{1'234'567, -6})}));
            env.close();
            BEAST_EXPECT(env.balance(bob, iou) == iou(Number{901'234'554, -6}));
            STAmount const total = iou(Number{1'098'765'446, -6});
            expectTotals(env, s, total, total);
        }

        // The exact fee 965,408,809.7121080004 needs more digits than Number
        // keeps. Rounding the product down would pay 6,854,832,662.236924 and
        // charge one unit below ceil(amount * rate); rounding it up charges
        // 965,408,809.712109 and pays 6,854,832,662.236923.
        testcase("IOU fee at an odd rate rounds up when the product exceeds 16 digits");
        {
            Env env{*this, features()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), issuer, owner, alice, bob);
            env.close();
            PrettyAsset const iou = issuer[iouCurrency_];
            env.trust(iou(10'000'000'000), alice, bob);
            env(pay(issuer, alice, iou(1'000)));
            env(pay(issuer, bob, iou(8'000'000'000)));
            env.close();

            // 12.345%
            auto const s = makeVault(env, owner, iou, 12'345u);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = iou(1'000)}));
            env(s.vault.deposit(
                {.depositor = bob, .id = s.keylet.key, .amount = iou(8'000'000'000)}));
            env.close();
            enterInvestment(env, s);

            env(s.vault.withdraw(
                {.depositor = bob,
                 .id = s.keylet.key,
                 .amount = iou(Number{7'820'241'471'949'032, -6})}));
            env.close();
            BEAST_EXPECT(env.balance(bob, iou) == iou(Number{6'854'832'662'236'923, -6}));
            BEAST_EXPECT(sharesOf(env, s, bob) == 179'758'528'050'968);
            STAmount const total = iou(Number{1'145'168'337'763'077, -6});
            expectTotals(env, s, total, total);
        }

        testcase("IOU fee consuming the whole withdrawal below 100% pays nothing");
        {
            Env env{*this, features()};
            Account const issuer{"issuer"};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), issuer, owner, alice, bob);
            env.close();
            PrettyAsset const iou = issuer[iouCurrency_];
            env.trust(iou(10'000), alice, bob);
            env(pay(issuer, alice, iou(1'000)));
            env(pay(issuer, bob, iou(1'000)));
            env.close();

            auto const s = makeVault(env, owner, iou, 1u);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = iou(1'000)}));
            env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = iou(100)}));
            env.close();
            enterInvestment(env, s);

            // One unit at scale 6 leaves a payout below the grid, which rounds
            // to zero, so the unit is retained as the fee.
            env(s.vault.withdraw(
                {.depositor = bob, .id = s.keylet.key, .amount = iou(Number{1, -6})}));
            env.close();
            BEAST_EXPECT(env.balance(bob, iou) == iou(900));
            BEAST_EXPECT(sharesOf(env, s, bob) == 99'999'999);
            BEAST_EXPECT(sharesOutstanding(env, s) == 1'099'999'999);
            expectTotals(env, s, iou(1'100).value(), iou(1'100).value());
        }
    }

    // Fee applies only strictly inside the Investment phase. Each boundary uses a fresh vault so
    // the exchange rate is still 1:1 and the payout is exact.
    void
    testPhaseBoundaries()
    {
        testcase("no fee outside the Investment phase");
        using namespace test::jtx;

        enum class At { Subscription, AfterSubscription, BeforeRedemption, Redemption };

        auto const run = [&](At at,
                             bool expectFee,
                             std::source_location const& loc = std::source_location::current()) {
            Env env{*this, features()};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10'000), owner, alice, bob);
            env.close();
            XRPAmount const baseFee = env.current()->fees().base;

            auto const s = makeVault(env, owner, xrpIssue(), kTwoPercent);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = XRP(900)}));
            env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = XRP(100)}));
            env.close();

            auto const resolution = getLedgerTimeResolution(env);
            switch (at)
            {
                case At::Subscription:
                    closeToTime(env, Tp{D{s.sub}});
                    break;
                case At::AfterSubscription:
                    closeToTime(env, Tp{D{s.sub}} + resolution);
                    break;
                case At::BeforeRedemption:
                    closeToTime(env, Tp{D{s.red}} - resolution);
                    break;
                case At::Redemption:
                    closeToTime(env, Tp{D{s.red}});
                    break;
            }

            auto const before = env.balance(bob).value().xrp();
            env(WithSourceLocation{
                s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(10)}), loc});
            env.close();

            XRPAmount const fee = expectFee ? XRPAmount{200'000} : XRPAmount{0};
            expect(
                env.balance(bob).value().xrp() == before + XRP(10).value().xrp() - fee - baseFee,
                "withdrawal payout",
                loc.file_name(),
                loc.line());
        };

        // now == SubscriptionDate is still Subscription; now == RedemptionDate is Redemption.
        run(At::Subscription, false);
        run(At::AfterSubscription, true);
        run(At::BeforeRedemption, true);
        run(At::Redemption, false);
    }

    // Liquidity is measured against the post-fee payout when a fee applies.
    void
    testLiquidity()
    {
        using namespace test::jtx;

        auto const run = [&](std::uint32_t rate,
                             int loanXrp,
                             TER expected,
                             std::source_location const& loc = std::source_location::current()) {
            Env env{*this, features()};
            Account const owner{"owner"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const borrower{"borrower"};
            env.fund(XRP(10'000), owner, alice, bob, borrower);
            env.close();

            auto const s = makeVault(env, owner, xrpIssue(), rate);
            env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = XRP(500)}));
            env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = XRP(100)}));
            env.close();

            auto const brokerKeylet =
                keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
            env(loan_broker::set(owner, s.keylet.key));
            env.close();

            enterInvestment(env, s);
            env(loan::set(borrower, brokerKeylet.key, XRP(loanXrp).value()),
                loan::kInterestRate(TenthBips32(0)),
                loan::kGracePeriod(60),
                loan::kPaymentInterval(60),
                loan::kPaymentTotal(1),
                Sig(sfCounterpartySignature, owner),
                Fee(env.current()->fees().base * 2));
            env.close();

            env(
                WithSourceLocation{
                    s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = XRP(100)}),
                    loc},
                Ter{expected});
            env.close();
        };

        testcase("post-fee payout equal to AssetsAvailable succeeds");
        // AssetsAvailable is 98 XRP; the pre-fee amount is 100 XRP.
        run(kTwoPercent, 502, tesSUCCESS);

        testcase("post-fee payout above AssetsAvailable fails");
        run(kTwoPercent, 503, tecINSUFFICIENT_FUNDS);

        testcase("without a fee the pre-fee amount must be available");
        run(0u, 502, tecINSUFFICIENT_FUNDS);

        testcase("100% fee succeeds with no liquidity");
        run(kMaxRate, 600, tesSUCCESS);
    }

    void
    testFullExitWaiver()
    {
        using namespace test::jtx;

        Env env{*this, features()};
        Account const owner{"owner"};
        Account const alice{"alice"};
        env.fund(XRP(10'000), owner, alice);
        env.close();
        XRPAmount const baseFee = env.current()->fees().base;

        auto const s = makeVault(env, owner, xrpIssue(), kTwoPercent);
        env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = XRP(100)}));
        env.close();
        enterInvestment(env, s);

        testcase("sole shareholder leaving shares behind pays the fee");
        auto before = env.balance(alice).value().xrp();
        env(s.vault.withdraw({.depositor = alice, .id = s.keylet.key, .amount = XRP(50)}));
        env.close();
        BEAST_EXPECT(env.balance(alice).value().xrp() == before + XRP(49).value().xrp() - baseFee);
        expectTotals(env, s, XRP(51).value(), XRP(51).value());

        testcase("burning the whole share supply pays no fee");
        before = env.balance(alice).value().xrp();
        env(s.vault.withdraw(
            {.depositor = alice,
             .id = s.keylet.key,
             .amount = STAmount{s.shares, sharesOf(env, s, alice)}}));
        env.close();
        BEAST_EXPECT(env.balance(alice).value().xrp() == before + XRP(51).value().xrp() - baseFee);
        BEAST_EXPECT(sharesOutstanding(env, s) == 0);
        expectTotals(env, s, XRP(0).value(), XRP(0).value());

        // Nothing is left behind, so the vault can be deleted.
        env(s.vault.del({.owner = owner, .id = s.keylet.key}));
        env.close();
        BEAST_EXPECT(!env.le(s.keylet));
    }

    void
    testClawbackIsExempt()
    {
        testcase("VaultClawback is exempt from the early-exit fee");
        using namespace test::jtx;

        Env env{*this, features()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10'000), issuer, owner, alice, bob);
        env.close();
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const iou = issuer[iouCurrency_];
        env.trust(iou(10'000), alice, bob);
        env(pay(issuer, alice, iou(1'000)));
        env(pay(issuer, bob, iou(1'000)));
        env.close();

        auto const s = makeVault(env, owner, iou, kMaxRate);
        env(s.vault.deposit({.depositor = alice, .id = s.keylet.key, .amount = iou(300)}));
        env(s.vault.deposit({.depositor = bob, .id = s.keylet.key, .amount = iou(100)}));
        env.close();
        enterInvestment(env, s);

        // A withdrawal at 100% moves no assets.
        env(s.vault.withdraw({.depositor = bob, .id = s.keylet.key, .amount = iou(10)}));
        env.close();
        expectTotals(env, s, iou(400).value(), iou(400).value());

        // A clawback removes the full pre-fee amount.
        env(s.vault.clawback(
            {.issuer = issuer, .id = s.keylet.key, .holder = alice, .amount = iou(10).value()}));
        env.close();
        expectTotals(env, s, iou(390).value(), iou(390).value());
    }

public:
    void
    run() override
    {
        testCreate();
        testWithdrawChargesFee();
        testWithdrawRates();
        testWithdrawRounding();
        testPhaseBoundaries();
        testLiquidity();
        testFullExitWaiver();
        testClawbackIsExempt();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(VaultEarlyExitFee, app, xrpl, 1);

}  // namespace xrpl

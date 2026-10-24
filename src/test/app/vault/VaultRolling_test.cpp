#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace xrpl {

class VaultRolling_test : public VaultTestBase
{
private:
    static constexpr std::uint32_t kInterval = 86'400;  // a day between windows
    static constexpr std::uint32_t kWindow = 3'600;     // open for an hour

    // VaultCreate validation for VaultKind::Rolling and the fee fields, plus
    // the featureVaultContinuousAccrual gate.
    void
    testVaultCreateRolling()
    {
        testcase("rolling VaultCreate");
        using namespace test::jtx;

        auto const withEnv = [this](FeatureBitset features, auto&& body) {
            Env env{*this, features};
            Account const owner{"owner"};
            env.fund(XRP(1000), owner);
            env.close();
            Vault vault{env};
            body(env, owner, vault);
        };

        Asset const asset = xrpIssue();
        auto const rolling = std::to_underlying(VaultKind::Rolling);
        auto const openEnded = std::to_underlying(VaultKind::OpenEnded);

        // Gate: the dealing and fee fields require featureVaultContinuousAccrual.
        withEnv(
            testableAmendments() - featureVaultContinuousAccrual,
            [&](Env& env, Account const& owner, Vault& vault) {
                auto const sub = env.now().time_since_epoch().count() + 60;
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .subscriptionDate = sub,
                    .dealingInterval = kInterval,
                    .dealingWindow = kWindow,
                });
                env(tx, Ter{temDISABLED});
                env.close();
            });

        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;

            // A rolling vault needs a first window and both durations.
            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .subscriptionDate = sub,
                    .dealingInterval = kInterval,
                });
                env(tx, Ter{temMALFORMED});
                env.close();
            }
            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .subscriptionDate = sub,
                    .dealingWindow = kWindow,
                });
                env(tx, Ter{temMALFORMED});
                env.close();
            }
            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .dealingInterval = kInterval,
                    .dealingWindow = kWindow,
                });
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // 0 < DealingWindow < DealingInterval.
            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .subscriptionDate = sub,
                    .dealingInterval = kInterval,
                    .dealingWindow = 0,
                });
                env(tx, Ter{temMALFORMED});
                env.close();
            }
            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .subscriptionDate = sub,
                    .dealingInterval = kInterval,
                    .dealingWindow = kInterval,
                });
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // RedemptionDate belongs to the closed-ended structure.
            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .subscriptionDate = sub,
                    .redemptionDate = sub + kInterval,
                    .dealingInterval = kInterval,
                    .dealingWindow = kWindow,
                });
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // The dealing fields mean nothing on a vault that is not rolling.
            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = openEnded,
                    .dealingInterval = kInterval,
                    .dealingWindow = kWindow,
                });
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // A redemption period with no fee to gate would never be read.
            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset, .redemptionPeriod = kInterval});
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // Neither fee may retain more than half of what is moved.
            {
                auto [tx, keylet] =
                    vault.create({.owner = owner, .asset = asset, .depositFee = kMaxVaultFee + 1});
                env(tx, Ter{temMALFORMED});
                env.close();
            }
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner, .asset = asset, .redemptionFee = kMaxVaultFee + 1});
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // The happy path stores every field. The rejected cases above each
            // closed a ledger, so take the first window from the clock as it is
            // now rather than the value read before them.
            {
                auto const subNow =
                    static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = asset,
                    .vaultKind = rolling,
                    .subscriptionDate = subNow,
                    .dealingInterval = kInterval,
                    .dealingWindow = kWindow,
                    .depositFee = 100,
                    .redemptionFee = 250,
                    .redemptionPeriod = kInterval,
                });
                env(tx);
                env.close();

                auto const sleVault = env.le(keylet);
                BEAST_EXPECT(sleVault != nullptr);
                if (!sleVault)
                    return;
                BEAST_EXPECT(sleVault->at(sfVaultKind) == rolling);
                BEAST_EXPECT(sleVault->at(sfSubscriptionDate) == subNow);
                BEAST_EXPECT(sleVault->at(sfDealingInterval) == kInterval);
                BEAST_EXPECT(sleVault->at(sfDealingWindow) == kWindow);
                BEAST_EXPECT(sleVault->at(sfDepositFee) == 100);
                BEAST_EXPECT(sleVault->at(sfRedemptionFee) == 250);
                BEAST_EXPECT(sleVault->at(sfRedemptionPeriod) == kInterval);
                BEAST_EXPECT(getVaultKind(sleVault) == VaultKind::Rolling);
            }
        });
    }

    // VaultDeposit and VaultWithdraw are accepted only inside a dealing window.
    void
    testDealingWindow()
    {
        testcase("rolling dealing window");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(10'000), owner, depositor);
        env.close();

        Vault const vault{env};
        Asset const asset = xrpIssue();
        auto const start = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;

        auto [tx, keylet] = vault.create({
            .owner = owner,
            .asset = asset,
            .vaultKind = std::to_underlying(VaultKind::Rolling),
            .subscriptionDate = start,
            .dealingInterval = kInterval,
            .dealingWindow = kWindow,
        });
        env(tx);
        env.close();

        auto const vaultId = keylet.key;
        auto const atTime = [&](std::uint32_t when) {
            env.close(NetClock::time_point{NetClock::duration{when}});
        };

        // Before the first window opens.
        atTime(start - 30);
        env(vault.deposit({.depositor = depositor, .id = vaultId, .amount = XRP(10)}),
            Ter{tecTOO_SOON});
        env.close();

        // Inside the first window.
        atTime(start + 10);
        env(vault.deposit({.depositor = depositor, .id = vaultId, .amount = XRP(10)}));
        env.close();

        // After the window has closed, before the next one opens.
        atTime(start + kWindow + 10);
        env(vault.deposit({.depositor = depositor, .id = vaultId, .amount = XRP(10)}),
            Ter{tecTOO_SOON});
        env.close();

        // The window reopens one interval later.
        atTime(start + kInterval + 10);
        env(vault.deposit({.depositor = depositor, .id = vaultId, .amount = XRP(10)}));
        env.close();

        // Withdrawal obeys the same window.
        atTime(start + kInterval + kWindow + 10);
        env(vault.withdraw({.depositor = depositor, .id = vaultId, .amount = XRP(5)}),
            Ter{tecTOO_SOON});
        env.close();

        atTime(start + (2 * kInterval) + 10);
        env(vault.withdraw({.depositor = depositor, .id = vaultId, .amount = XRP(5)}));
        env.close();
    }

    // The deposit fee is retained by the vault, so the vault gains the gross
    // while the depositor is issued shares only for the net.
    void
    testDepositFee()
    {
        testcase("deposit fee");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const first{"first"};
        Account const second{"second"};
        env.fund(XRP(10'000), owner, first, second);
        env.close();

        Vault const vault{env};
        // 10% of what comes in.
        auto [tx, keylet] =
            vault.create({.owner = owner, .asset = xrpIssue(), .depositFee = 10'000});
        env(tx);
        env.close();
        auto const vaultId = keylet.key;

        // The first deposit into an empty vault has no holders to lift, so it
        // pays no fee: the vault gains exactly what was sent.
        env(vault.deposit({.depositor = first, .id = vaultId, .amount = XRP(1'000)}));
        env.close();
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(sleVault->at(sfAssetsTotal) == Number{1'000'000'000});
        }

        // The second deposit pays the fee. The vault still gains the gross,
        // which is what lifts the first depositor.
        env(vault.deposit({.depositor = second, .id = vaultId, .amount = XRP(1'000)}));
        env.close();
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(sleVault->at(sfAssetsTotal) == Number{2'000'000'000});
        }
    }

    // The first deal of a window fixes the price for that window, and the next
    // window strikes a fresh one.
    void
    testStruckPrice()
    {
        testcase("struck price");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const first{"first"};
        Account const second{"second"};
        env.fund(XRP(10'000), owner, first, second);
        env.close();

        Vault const vault{env};
        auto const start = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;
        auto [tx, keylet] = vault.create({
            .owner = owner,
            .asset = xrpIssue(),
            .vaultKind = std::to_underlying(VaultKind::Rolling),
            .subscriptionDate = start,
            .dealingInterval = kInterval,
            .dealingWindow = kWindow,
        });
        env(tx);
        env.close();
        auto const vaultId = keylet.key;

        auto const atTime = [&](std::uint32_t when) {
            env.close(NetClock::time_point{NetClock::duration{when}});
        };

        // Nothing is struck before the first deal.
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault && sleVault->at(sfStruckUntil) == 0);
        }

        // The first window seeds the vault. An empty vault has no outstanding
        // shares, so there is no ratio to strike and the window passes without a strike.
        atTime(start + 10);
        env(vault.deposit({.depositor = first, .id = vaultId, .amount = XRP(1'000)}));
        env.close();
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault && sleVault->at(sfStruckUntil) == 0);
        }

        // The first deal of the next window strikes, against that window's end.
        auto const secondWindow = start + kInterval;
        atTime(secondWindow + 10);
        env(vault.deposit({.depositor = second, .id = vaultId, .amount = XRP(500)}));
        env.close();

        Number struckPrice;
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(sleVault->at(sfStruckUntil) == secondWindow + kWindow);
            struckPrice = sleVault->at(sfStruckPrice);
            BEAST_EXPECT(struckPrice > Number{});
        }

        // A later deal in the same window converts at the same price and does
        // not restrike it.
        env(vault.deposit({.depositor = first, .id = vaultId, .amount = XRP(100)}));
        env.close();
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(sleVault->at(sfStruckUntil) == secondWindow + kWindow);
            BEAST_EXPECT(sleVault->at(sfStruckPrice) == struckPrice);
        }

        // The window after that strikes afresh.
        auto const thirdWindow = start + (2 * kInterval);
        atTime(thirdWindow + 10);
        env(vault.deposit({.depositor = second, .id = vaultId, .amount = XRP(100)}));
        env.close();
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(sleVault->at(sfStruckUntil) == thirdWindow + kWindow);
        }
    }

    // VaultSet changes the three fee fields, within the same bounds as
    // VaultCreate, and never moves a holder's existing redemption clock.
    void
    testVaultSetFees()
    {
        testcase("VaultSet fees");
        using namespace test::jtx;

        // Without the amendment the fee fields are refused, even beside a
        // field VaultSet already accepts.
        {
            Env env{*this, testableAmendments() - featureVaultContinuousAccrual};
            Account const owner{"owner"};
            env.fund(XRP(10'000), owner);
            env.close();
            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
            env(tx);
            env.close();

            for (auto const* field : {&sfDepositFee, &sfRedemptionFee, &sfRedemptionPeriod})
            {
                auto set = vault.set({.owner = owner, .id = keylet.key});
                set[sfData] = "AB";
                set[*field] = 100;
                env(set, Ter(temDISABLED));
                env.close();
            }
        }

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const holder{"holder"};
        env.fund(XRP(10'000), owner, holder);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create(
            {.owner = owner, .asset = xrpIssue(), .redemptionFee = 100, .redemptionPeriod = 600});
        env(tx);
        env.close();
        auto const vaultId = keylet.key;

        env(vault.deposit({.depositor = holder, .id = vaultId, .amount = XRP(1'000)}));
        env.close();

        auto const shareMptId = [&] {
            auto const sleVault = env.le(keylet);
            return sleVault ? sleVault->at(sfShareMPTID) : MPTID{};
        }();
        auto const redemptionAfter = [&] {
            auto const sleMpt = env.le(keylet::mptoken(shareMptId, holder.id()));
            return sleMpt ? sleMpt->at(sfRedemptionAfter) : std::uint32_t{0};
        };
        std::uint32_t const clockBefore = redemptionAfter();
        BEAST_EXPECT(clockBefore != 0);

        // Each fee field on its own is an update.
        {
            auto set = vault.set({.owner = owner, .id = vaultId});
            set[sfDepositFee] = 2'000;
            env(set);
            env.close();
        }
        {
            auto set = vault.set({.owner = owner, .id = vaultId});
            set[sfRedemptionFee] = 3'000;
            env(set);
            env.close();
        }
        {
            auto set = vault.set({.owner = owner, .id = vaultId});
            set[sfRedemptionPeriod] = 7'200;
            env(set);
            env.close();
        }
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(sleVault->at(sfDepositFee) == 2'000);
            BEAST_EXPECT(sleVault->at(sfRedemptionFee) == 3'000);
            BEAST_EXPECT(sleVault->at(sfRedemptionPeriod) == 7'200);
        }

        // A new period does not reprice the holder's existing clock.
        BEAST_EXPECT(redemptionAfter() == clockBefore);

        // Zero turns a fee off.
        {
            auto set = vault.set({.owner = owner, .id = vaultId});
            set[sfDepositFee] = 0;
            env(set);
            env.close();
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault && sleVault->at(sfDepositFee) == 0);
        }

        // The same ceiling as VaultCreate.
        {
            auto set = vault.set({.owner = owner, .id = vaultId});
            set[sfData] = "AB";
            set[sfDepositFee] = kMaxVaultFee + 1;
            env(set, Ter(temMALFORMED));
        }
        {
            auto set = vault.set({.owner = owner, .id = vaultId});
            set[sfData] = "AB";
            set[sfRedemptionFee] = kMaxVaultFee + 1;
            env(set, Ter(temMALFORMED));
        }
        {
            auto set = vault.set({.owner = owner, .id = vaultId});
            set[sfRedemptionFee] = kMaxVaultFee;
            env(set);
            env.close();
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault && sleVault->at(sfRedemptionFee) == kMaxVaultFee);
        }
    }

    // Accrual needs a CashBasis Vault and fixCleanup3_4_0: elsewhere it is
    // refused when asked for, and cash basis is the default.
    void
    testAccountingMethodAvailability()
    {
        testcase("accounting method availability");
        using namespace test::jtx;

        auto const check = [this](FeatureBitset features, TER accrualResult, std::uint8_t method) {
            Env env{*this, features};
            Account const owner{"owner"};
            env.fund(XRP(10'000), owner);
            env.close();
            Vault const vault{env};

            {
                auto [tx, keylet] = vault.create({
                    .owner = owner,
                    .asset = xrpIssue(),
                    .accountingMethod = kVaultAccountingAccrual,
                });
                env(tx, Ter(accrualResult));
                env.close();
            }

            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
            env(tx);
            env.close();
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(sleVault->at(~sfAccountingMethod) == method);
            BEAST_EXPECT(getAccountingMethod(sleVault) == method);
        };

        FeatureBitset const cashBasis{testableAmendments() - featureLendingProtocolV1_2};
        check(cashBasis, tesSUCCESS, kVaultAccountingAccrual);
        check(cashBasis - fixCleanup3_4_0, temMALFORMED, kVaultAccountingCash);
        check(testableAmendments(), temMALFORMED, kVaultAccountingCash);
    }

    // Each dealing, fee or accounting field on its own needs the amendment,
    // and the fields are checked for the kind and method they belong to.
    void
    testVaultCreateFieldChecks()
    {
        testcase("VaultCreate field checks");
        using namespace test::jtx;

        {
            Env env{*this, testableAmendments() - featureVaultContinuousAccrual};
            Account const owner{"owner"};
            env.fund(XRP(10'000), owner);
            env.close();
            Vault vault{env};

            auto const refused = [&](SField const& field, std::uint32_t value) {
                auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
                tx[field] = value;
                env(tx, Ter{temDISABLED});
                env.close();
            };
            refused(sfDealingInterval, kInterval);
            refused(sfDealingWindow, kWindow);
            refused(sfDepositFee, 100);
            refused(sfRedemptionFee, 100);
            refused(sfRedemptionPeriod, 600);
            refused(sfAccountingMethod, kVaultAccountingCash);

            // Withdrawal is not window-gated without the amendment.
            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = XRP(100)}));
            env.close();
            env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = XRP(10)}));
            env.close();
        }

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        env.fund(XRP(10'000), owner);
        env.close();
        Vault const vault{env};

        // A dealing duration on its own still marks a vault that is not rolling.
        {
            auto [tx, keylet] =
                vault.create({.owner = owner, .asset = xrpIssue(), .dealingInterval = kInterval});
            env(tx, Ter{temMALFORMED});
            env.close();
        }
        {
            auto [tx, keylet] =
                vault.create({.owner = owner, .asset = xrpIssue(), .dealingWindow = kWindow});
            env(tx, Ter{temMALFORMED});
            env.close();
        }

        // Legacy is never assigned, and nothing past accrual exists.
        for (std::uint8_t const method : {kVaultAccountingLegacy, std::uint8_t{3}})
        {
            auto [tx, keylet] =
                vault.create({.owner = owner, .asset = xrpIssue(), .accountingMethod = method});
            env(tx, Ter{temMALFORMED});
            env.close();
        }

        // No vault kind past Rolling exists.
        {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue(), .vaultKind = 3});
            env(tx, Ter{temMALFORMED});
            env.close();
        }
    }

    // A vault created without the amendment carries no sfAccountingMethod, so
    // its method follows its schema version.
    void
    testAccountingMethodFromVersion()
    {
        testcase("accounting method from version");
        using namespace test::jtx;

        auto const check = [this](FeatureBitset features, std::uint8_t method) {
            Env env{*this, features};
            Account const owner{"owner"};
            env.fund(XRP(10'000), owner);
            env.close();
            BEAST_EXPECT(!isAccrualAvailable(env.current()->rules()));

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
            env(tx);
            env.close();
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault != nullptr);
            if (!sleVault)
                return;
            BEAST_EXPECT(!sleVault->isFieldPresent(sfAccountingMethod));
            BEAST_EXPECTS(
                getAccountingMethod(sleVault) == method,
                std::to_string(getAccountingMethod(sleVault)));
        };

        FeatureBitset const noAccrual{
            testableAmendments() - featureLendingProtocolV1_2 - featureVaultContinuousAccrual,
        };
        check(noAccrual, kVaultAccountingCash);
        check(noAccrual - featureLendingProtocolV1_1, kVaultAccountingLegacy);
    }

    // The redemption fee stays in the vault, applies only inside the holder's
    // redemption period when one is set, and is waived for a sole holder.
    void
    testRedemptionFee()
    {
        testcase("redemption fee");
        using namespace test::jtx;

        struct Setup
        {
            std::uint32_t redemptionFee;
            std::optional<std::uint32_t> redemptionPeriod;
            bool secondHolder;
        };

        auto const withVault = [this](FeatureBitset features, Setup const& setup, auto&& body) {
            Env env{*this, features};
            Account const owner{"owner"};
            Account const first{"first"};
            Account const second{"second"};
            env.fund(XRP(10'000), owner, first, second);
            env.close();

            Vault vault{env};
            auto [tx, keylet] = vault.create({
                .owner = owner,
                .asset = xrpIssue(),
                .redemptionFee = setup.redemptionFee,
                .redemptionPeriod = setup.redemptionPeriod,
            });
            env(tx);
            env.close();
            env(vault.deposit({.depositor = first, .id = keylet.key, .amount = XRP(1'000)}));
            if (setup.secondHolder)
                env(vault.deposit({.depositor = second, .id = keylet.key, .amount = XRP(1'000)}));
            env.close();

            auto const assetsTotal = [&] {
                auto const sleVault = env.le(keylet);
                return sleVault ? Number{sleVault->at(sfAssetsTotal)} : Number{};
            };
            body(env, vault, keylet, first, assetsTotal);
        };

        // 1% of 100 XRP stays in the vault.
        withVault(
            testableAmendments(),
            {.redemptionFee = 1'000, .redemptionPeriod = std::nullopt, .secondHolder = true},
            [&](Env& env, Vault& vault, Keylet const& keylet, Account const& first, auto&& total) {
                // A fee that would take the whole payout is not charged.
                env(vault.withdraw(
                    {.depositor = first, .id = keylet.key, .amount = STAmount{XRPAmount{1}}}));
                env.close();
                BEAST_EXPECT(total() == Number{1'999'999'999});

                env(vault.withdraw({.depositor = first, .id = keylet.key, .amount = XRP(100)}));
                env.close();
                BEAST_EXPECT(total() == Number{1'900'999'999});
            });

        // Without fixCleanup3_4_0 the reduced payout is not clamped again.
        withVault(
            testableAmendments() - featureLendingProtocolV1_2 - fixCleanup3_4_0,
            {.redemptionFee = 1'000, .redemptionPeriod = std::nullopt, .secondHolder = true},
            [&](Env& env, Vault& vault, Keylet const& keylet, Account const& first, auto&& total) {
                env(vault.withdraw({.depositor = first, .id = keylet.key, .amount = XRP(100)}));
                env.close();
                BEAST_EXPECT(total() == Number{1'901'000'000});
            });

        // With a period, the fee applies until the holder's clock runs out.
        withVault(
            testableAmendments(),
            {.redemptionFee = 1'000, .redemptionPeriod = 600, .secondHolder = true},
            [&](Env& env, Vault& vault, Keylet const& keylet, Account const& first, auto&& total) {
                env(vault.withdraw({.depositor = first, .id = keylet.key, .amount = XRP(100)}));
                env.close();
                BEAST_EXPECT(total() == Number{1'901'000'000});

                // Past the period the payout carries no 1 XRP fee, only the
                // conversion's rounding.
                env.close(env.now() + std::chrono::seconds{700});
                Number const before = total();
                env(vault.withdraw({.depositor = first, .id = keylet.key, .amount = XRP(100)}));
                env.close();
                BEAST_EXPECT(before - total() > Number{99'999'000});
            });

        // A sole holder leaves no one to lift, so pays nothing.
        withVault(
            testableAmendments(),
            {.redemptionFee = 1'000, .redemptionPeriod = std::nullopt, .secondHolder = false},
            [&](Env& env, Vault& vault, Keylet const& keylet, Account const& first, auto&& total) {
                env(vault.withdraw({.depositor = first, .id = keylet.key, .amount = XRP(100)}));
                env.close();
                BEAST_EXPECT(total() == Number{900'000'000});
            });
    }

    // A fee-reduced payout below one unit of the vault's balance grid pays out
    // nothing, so the withdrawal is refused rather than burning shares for it.
    void
    testRedemptionFeeBelowGrid()
    {
        testcase("redemption fee below the balance grid");
        using namespace test::jtx;

        // A CashBasis vault, whose balance grid follows AssetsTotal.
        Env env{*this, testableAmendments() - featureLendingProtocolV1_2};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const first{"first"};
        Account const second{"second"};
        env.fund(XRP(10'000), issuer, owner, first, second);
        env.close();

        PrettyAsset const usd = issuer["USD"];
        for (auto const& holder : {first, second})
        {
            env(trust(holder, usd(100'000'000'000'000'000)));
            env.close();
            env(pay(issuer, holder, usd(5'000'000'000'000'000)));
            env.close();
        }

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = usd, .redemptionFee = 50'000});
        tx[sfScale] = 0;
        env(tx);
        env.close();
        for (auto const& holder : {first, second})
        {
            env(vault.deposit(
                {.depositor = holder, .id = keylet.key, .amount = usd(5'000'000'000'000'000)}));
            env.close();
        }

        // Half of one USD rounds away on a vault whose balance moves in whole USD.
        env(vault.withdraw({.depositor = first, .id = keylet.key, .amount = usd(1)}),
            Ter{tecPRECISION_LOSS});
        env.close();
    }

    // A deposit whose fee rounds up to the whole amount mints nothing.
    void
    testDepositFeeTakesAll()
    {
        testcase("deposit fee takes the whole deposit");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const first{"first"};
        Account const second{"second"};
        env.fund(XRP(10'000), owner, first, second);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] =
            vault.create({.owner = owner, .asset = xrpIssue(), .depositFee = 1'000});
        env(tx);
        env.close();
        env(vault.deposit({.depositor = first, .id = keylet.key, .amount = XRP(1'000)}));
        env.close();
        env(vault.deposit(
                {.depositor = second, .id = keylet.key, .amount = STAmount{XRPAmount{1}}}),
            Ter{tecPRECISION_LOSS});
        env.close();
    }

    // Inside a struck window a withdrawal by assets converts at the struck price,
    // truncated or not.
    void
    testStruckWithdrawWithoutTruncation()
    {
        testcase("struck withdrawal without fixCleanup3_4_0");
        using namespace test::jtx;

        Env env{*this, testableAmendments() - featureLendingProtocolV1_2 - fixCleanup3_4_0};
        Account const owner{"owner"};
        Account const first{"first"};
        Account const second{"second"};
        env.fund(XRP(10'000), owner, first, second);
        env.close();

        Vault const vault{env};
        auto const start = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;
        auto [tx, keylet] = vault.create({
            .owner = owner,
            .asset = xrpIssue(),
            .vaultKind = std::to_underlying(VaultKind::Rolling),
            .subscriptionDate = start,
            .dealingInterval = kInterval,
            .dealingWindow = kWindow,
        });
        env(tx);
        env.close();

        env.close(NetClock::time_point{NetClock::duration{start + 10}});
        env(vault.deposit({.depositor = first, .id = keylet.key, .amount = XRP(1'000)}));
        env.close(NetClock::time_point{NetClock::duration{start + kInterval + 10}});
        env(vault.deposit({.depositor = second, .id = keylet.key, .amount = XRP(500)}));
        env.close();
        {
            auto const sleVault = env.le(keylet);
            BEAST_EXPECT(sleVault && sleVault->at(sfStruckUntil) == start + kInterval + kWindow);
        }

        env(vault.withdraw({.depositor = first, .id = keylet.key, .amount = XRP(100)}));
        env.close();
        auto const sleVault = env.le(keylet);
        BEAST_EXPECT(sleVault && sleVault->at(sfAssetsTotal) == Number{1'400'000'000});
    }

    // Clawback is not window-gated, so outside a window it prices at the
    // vault's carrying value rather than a struck price.
    void
    testClawbackOutsideWindow()
    {
        testcase("rolling clawback outside a window");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const holder{"holder"};
        env.fund(XRP(10'000), issuer, owner, holder);
        env.close();
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const usd = issuer["USD"];
        env(trust(holder, usd(10'000)));
        env.close();
        env(pay(issuer, holder, usd(1'000)));
        env.close();

        Vault const vault{env};
        auto const start = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;
        auto [tx, keylet] = vault.create({
            .owner = owner,
            .asset = usd,
            .vaultKind = std::to_underlying(VaultKind::Rolling),
            .subscriptionDate = start,
            .dealingInterval = kInterval,
            .dealingWindow = kWindow,
        });
        env(tx);
        env.close();

        env.close(NetClock::time_point{NetClock::duration{start + 10}});
        env(vault.deposit({.depositor = holder, .id = keylet.key, .amount = usd(100)}));
        env.close(NetClock::time_point{NetClock::duration{start + kWindow + 10}});

        env(vault.clawback(
            {.issuer = issuer, .id = keylet.key, .holder = holder, .amount = usd(40)}));
        env.close();
        auto const sleVault = env.le(keylet);
        BEAST_EXPECT(sleVault && sleVault->at(sfAssetsTotal) == Number{60});
    }

public:
    void
    run() override
    {
        testAccountingMethodAvailability();
        testVaultCreateRolling();
        testDealingWindow();
        testDepositFee();
        testStruckPrice();
        testVaultSetFees();
        testVaultCreateFieldChecks();
        testAccountingMethodFromVersion();
        testRedemptionFee();
        testRedemptionFeeBelowGrid();
        testDepositFeeTakesAll();
        testStruckWithdrawWithoutTruncation();
        testClawbackOutsideWindow();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(VaultRolling, app, xrpl, 1);

}  // namespace xrpl

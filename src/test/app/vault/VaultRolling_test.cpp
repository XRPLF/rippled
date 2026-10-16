#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
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
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .subscriptionDate = sub,
                     .dealingInterval = kInterval,
                     .dealingWindow = kWindow});
                env(tx, Ter{temDISABLED});
                env.close();
            });

        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;

            // A rolling vault needs a first window and both durations.
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .subscriptionDate = sub,
                     .dealingInterval = kInterval});
                env(tx, Ter{temMALFORMED});
                env.close();
            }
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .subscriptionDate = sub,
                     .dealingWindow = kWindow});
                env(tx, Ter{temMALFORMED});
                env.close();
            }
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .dealingInterval = kInterval,
                     .dealingWindow = kWindow});
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // 0 < DealingWindow < DealingInterval.
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .subscriptionDate = sub,
                     .dealingInterval = kInterval,
                     .dealingWindow = 0});
                env(tx, Ter{temMALFORMED});
                env.close();
            }
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .subscriptionDate = sub,
                     .dealingInterval = kInterval,
                     .dealingWindow = kInterval});
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // RedemptionDate belongs to the closed-ended structure.
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .subscriptionDate = sub,
                     .redemptionDate = sub + kInterval,
                     .dealingInterval = kInterval,
                     .dealingWindow = kWindow});
                env(tx, Ter{temMALFORMED});
                env.close();
            }

            // The dealing fields mean nothing on a vault that is not rolling.
            {
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = openEnded,
                     .dealingInterval = kInterval,
                     .dealingWindow = kWindow});
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
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = rolling,
                     .subscriptionDate = subNow,
                     .dealingInterval = kInterval,
                     .dealingWindow = kWindow,
                     .depositFee = 100,
                     .redemptionFee = 250,
                     .redemptionPeriod = kInterval});
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

        Vault vault{env};
        Asset const asset = xrpIssue();
        auto const start = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;

        auto [tx, keylet] = vault.create(
            {.owner = owner,
             .asset = asset,
             .vaultKind = std::to_underlying(VaultKind::Rolling),
             .subscriptionDate = start,
             .dealingInterval = kInterval,
             .dealingWindow = kWindow});
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

        atTime(start + 2 * kInterval + 10);
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

        Vault vault{env};
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

        Vault vault{env};
        auto const start = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;
        auto [tx, keylet] = vault.create(
            {.owner = owner,
             .asset = xrpIssue(),
             .vaultKind = std::to_underlying(VaultKind::Rolling),
             .subscriptionDate = start,
             .dealingInterval = kInterval,
             .dealingWindow = kWindow});
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
        auto const thirdWindow = start + 2 * kInterval;
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

public:
    void
    run() override
    {
        testVaultCreateRolling();
        testDealingWindow();
        testDepositFee();
        testStruckPrice();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(VaultRolling, app, xrpl, 1);

}  // namespace xrpl

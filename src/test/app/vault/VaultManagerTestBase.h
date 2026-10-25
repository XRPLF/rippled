#pragma once

#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl {

/**
 * Shared base for the VaultManager*, VaultTrade, VaultMark and
 * VaultManagerClaim suites: a vault in USD, an equity token EQ on the
 * book, and a manager entry whose roles are three distinct accounts.
 */
class VaultManagerTestBase : public VaultTestBase
{
protected:
    static constexpr std::uint32_t kSubOffset = 10 * 86'400;
    static constexpr std::uint32_t kRedemptionGap = 100 * 86'400;
    static constexpr std::uint32_t kInterval = 86'400;
    static constexpr std::uint32_t kWindow = 3'600;
    static constexpr std::uint32_t kMoveLimit = 10'000;
    static constexpr std::uint32_t kHaltPeriod = 3'600;
    static constexpr std::uint32_t kStaleAfter = 86'400;
    static constexpr std::uint32_t kTimelockSeconds = 86'400;

    struct FundArgs
    {
        VaultKind kind = VaultKind::ClosedEnded;
        bool createManager = true;
        bool deposit = true;
        bool offers = true;
    };

    // Accounts are aggregate members so a test reads fund.manager, fund.gw.
    struct Fund
    {
        test::jtx::Account gw{"gw"};
        test::jtx::Account owner{"owner"};
        test::jtx::Account manager{"manager"};
        test::jtx::Account valuer{"valuer"};
        test::jtx::Account guardian{"guardian"};
        test::jtx::Account alice{"alice"};
        test::jtx::Account bob{"bob"};
        test::jtx::Account mm{"mm"};
        test::jtx::PrettyAsset usd{gw["USD"]};
        test::jtx::PrettyAsset eq{gw["EQT"]};
        Keylet vault{ltVAULT, uint256{}};
        std::uint32_t sub = 0;
        std::uint32_t red = 0;
        std::uint32_t start = 0;

        [[nodiscard]] uint256 const&
        vaultID() const
        {
            return vault.key;
        }
    };

    static test::jtx::JTx
    defaultManager(test::jtx::Env& env, Fund const& f)
    {
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;
        return env.jt(
            set(f.owner, f.vaultID()),
            kManager(f.manager.id()),
            kValuer(f.valuer.id()),
            kGuardian(f.guardian.id()),
            mandate({mandateEntry(f.eq)}),
            kDeployedMaximum(Number{100'000}),
            kMarkMoveLimit(kMoveLimit),
            kMarkHaltPeriod(kHaltPeriod),
            kMarkStaleAfter(kStaleAfter),
            kTimelock(kTimelockSeconds));
    }

    Fund
    makeFund(test::jtx::Env& env)
    {
        return makeFund(env, FundArgs{});
    }

    Fund
    makeFund(test::jtx::Env& env, FundArgs const& args)
    {
        using namespace test::jtx;
        Fund f;
        env.fund(
            XRP(100'000), f.gw, f.owner, f.manager, f.valuer, f.guardian, f.alice, f.bob, f.mm);
        env.close();
        env(fset(f.gw, asfAllowTrustLineClawback));
        env.close();
        for (auto const& a : {f.owner, f.manager, f.valuer, f.guardian, f.alice, f.bob, f.mm})
        {
            env.trust(f.usd(1'000'000), a);
            env.trust(f.eq(1'000'000), a);
        }
        env.close();
        for (auto const& a : {f.owner, f.manager, f.valuer, f.guardian, f.alice, f.bob})
            env(pay(f.gw, a, f.usd(100'000)));
        env(pay(f.gw, f.mm, f.usd(500'000)));
        env(pay(f.gw, f.mm, f.eq(100'000)));
        env.close();

        Vault const vault{env};
        auto const now = env.now().time_since_epoch().count();
        if (args.kind == VaultKind::Rolling)
        {
            f.start = now + 60;
            auto [tx, keylet] = vault.create(
                {.owner = f.owner,
                 .asset = f.usd,
                 .vaultKind = std::to_underlying(VaultKind::Rolling),
                 .subscriptionDate = f.start,
                 .dealingInterval = kInterval,
                 .dealingWindow = kWindow});
            env(tx);
            f.vault = keylet;
            env.close(tp{d{f.start}});
        }
        else
        {
            f.sub = now + kSubOffset;
            f.red = f.sub + kRedemptionGap;
            auto [tx, keylet] = vault.create(
                {.owner = f.owner,
                 .asset = f.usd,
                 .vaultKind = std::to_underlying(args.kind),
                 .subscriptionDate = args.kind == VaultKind::ClosedEnded
                     ? std::optional<std::uint32_t>{f.sub}
                     : std::nullopt,
                 .redemptionDate = args.kind == VaultKind::ClosedEnded
                     ? std::optional<std::uint32_t>{f.red}
                     : std::nullopt});
            env(tx);
            f.vault = keylet;
        }
        env.close();

        if (args.deposit)
        {
            env(vault.deposit({.depositor = f.alice, .id = f.vaultID(), .amount = f.usd(10'000)}));
            env.close();
        }
        if (args.offers)
        {
            // mm sells EQ at 10 USD.
            env(offer(f.mm, f.usd(100'000), f.eq(10'000)));
            env.close();
        }
        if (args.createManager)
        {
            env(defaultManager(env, f));
            env.close();
        }
        return f;
    }

    static Keylet
    managerKeylet(Fund const& f)
    {
        return keylet::vaultManager(f.vaultID());
    }

    static Keylet
    positionKeylet(Fund const& f, Asset const& asset)
    {
        return keylet::vaultPosition(f.vaultID(), asset);
    }
};

}  // namespace xrpl

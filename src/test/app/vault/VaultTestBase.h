#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <utility>

namespace xrpl {

/**
 * Shared base for the Vault*_test family under src/test/app/vault/.
 *
 * Owns the class-level helpers (type aliases, closed-ended vault
 * scaffolding, standard feature bitset, IOU currency string) that every
 * topical Vault*_test suite depends on. Mirrors
 * src/test/app/lending/LoanTestBase.h.
 *
 * Run all suites in this family with `xrpld -u Vault` (the "Vault" prefix
 * is matched against every suite name via
 * beast::unit_test::Selector::ModeT::Automatch).
 */
class VaultTestBase : public beast::unit_test::Suite
{
protected:
    using PrettyAsset = test::jtx::PrettyAsset;
    using PrettyAmount = test::jtx::PrettyAmount;

    static constexpr auto kNegativeAmount = [](PrettyAsset const& asset) -> PrettyAmount {
        return {STAmount{asset.raw(), 1ul, 0, true, STAmount::Unchecked{}}, ""};
    };

    /**
     * Get the current ledger's close time resolution.
     * @param env The test environment.
     */
    static NetClock::duration
    getLedgerTimeResolution(test::jtx::Env& env)
    {
        return env.current()->header().closeTimeResolution;
    }

    void
    closeToTime(
        test::jtx::Env& env,
        NetClock::time_point time,
        std::source_location const& loc = std::source_location::current())
    {
        using namespace std::chrono_literals;
        env.close(time - env.closed()->header().closeTimeResolution + 1s);
        expect(
            env.closed()->header().closeTime == time,
            std::format(
                "current ledger time {} is not equal to the target ledger time {}",
                env.closed()->header().closeTime.time_since_epoch(),
                time.time_since_epoch()),
            loc.file_name(),
            loc.line());
    }

    using D = NetClock::duration;
    using Tp = NetClock::time_point;

    // Vault holds an Env& so no default initializer is possible; the
    // struct is always aggregate-initialized by makeClosedEndedVault.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    struct ClosedEndedSetup
    {
        test::jtx::Vault vault;
        Keylet keylet;
        std::uint32_t sub = 0;
        std::uint32_t red = 0;
    };
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)

    // Submit a VaultCreate for a closed-ended vault with SubscriptionDate at
    // env.now() + subOffset and RedemptionDate at SubscriptionDate + gap, then
    // close the ledger. Returns the Vault helper, the vault's keylet and the
    // resolved sub/red timestamps.
    static ClosedEndedSetup
    makeClosedEndedVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        Asset const& asset,
        std::uint32_t subOffset,
        std::uint32_t gap)
    {
        auto const sub = env.now().time_since_epoch().count() + subOffset;
        auto const red = sub + gap;
        test::jtx::Vault const vault{env};
        auto [tx, keylet] = vault.create(
            {.owner = owner,
             .asset = asset,
             .vaultKind = std::to_underlying(VaultKind::ClosedEnded),
             .subscriptionDate = sub,
             .redemptionDate = red});
        env(tx);
        env.close();
        return {.vault = vault, .keylet = keylet, .sub = sub, .red = red};
    }

    FeatureBitset const all_{test::jtx::testableAmendments()};
    std::string const iouCurrency_{"IOU"};
};

}  // namespace xrpl

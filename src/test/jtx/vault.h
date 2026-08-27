#pragma once

#include <test/jtx/Account.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <tuple>

namespace xrpl::test::jtx {

class Env;

struct Vault
{
    Env& env;

    struct CreateArgs
    {
        Account owner;
        Asset asset;
        std::optional<std::uint32_t> flags =
            std::nullopt;  // NOLINT(readability-redundant-member-init)
        std::optional<std::uint8_t> vaultKind =
            std::nullopt;  // NOLINT(readability-redundant-member-init)
        std::optional<std::uint32_t> subscriptionDate =
            std::nullopt;  // NOLINT(readability-redundant-member-init)
        std::optional<std::uint32_t> redemptionDate =
            std::nullopt;  // NOLINT(readability-redundant-member-init)
        std::optional<VaultVersion> leVersion =
            std::nullopt;  // NOLINT(readability-redundant-member-init)
    };

    /**
     * Return a VaultCreate transaction and the Vault's expected keylet.
     */
    [[nodiscard]] std::tuple<json::Value, Keylet>
    create(CreateArgs const& args) const;

    struct CreateClosedEndedArgs
    {
        Account owner;
        Asset asset;
        std::optional<std::uint32_t> flags =
            std::nullopt;  // NOLINT(readability-redundant-member-init)
        NetClock::duration subscriptionOffset = std::chrono::seconds{10};
        NetClock::duration investmentWindow = std::chrono::seconds{1'000'000};
    };

    /**
     * Return a VaultCreate transaction for a closed-ended vault, its
     * expected keylet, and the vault's SubscriptionDate.
     *
     * Under featureLendingProtocolV1_1, LoanBrokerSet::preclaim only
     * accepts closed-ended vaults, so tests that attach a loan broker
     * need one. SubscriptionDate is set to now() + subscriptionOffset,
     * giving callers a window to deposit while still in the Subscription
     * phase; pass the returned date to closePastSubscription() afterwards
     * to advance into the Investment phase.
     */
    [[nodiscard]] std::tuple<json::Value, Keylet, NetClock::time_point>
    createClosedEnded(CreateClosedEndedArgs const& args) const;

    /**
     * Advance env's clock to just past subscriptionDate, moving a
     * closed-ended vault from the Subscription phase into the Investment
     * phase.
     */
    void
    closePastSubscription(NetClock::time_point subscriptionDate) const;

    struct SetArgs
    {
        Account owner;
        uint256 id;
    };

    static json::Value
    set(SetArgs const& args);

    struct DeleteArgs
    {
        Account owner;
        uint256 id;
    };

    static json::Value
    del(DeleteArgs const& args);

    struct DepositArgs
    {
        Account depositor;
        uint256 id;
        STAmount amount;
    };

    static json::Value
    deposit(DepositArgs const& args);

    struct WithdrawArgs
    {
        Account depositor;
        uint256 id;
        STAmount amount;
    };

    static json::Value
    withdraw(WithdrawArgs const& args);

    struct ClawbackArgs
    {
        Account issuer;
        uint256 id;
        Account holder;
        std::optional<STAmount> amount = std::nullopt;  // NOLINT(readability-redundant-member-init)
    };

    static json::Value
    clawback(ClawbackArgs const& args);
};

}  // namespace xrpl::test::jtx

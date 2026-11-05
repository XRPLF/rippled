#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

/**
 * The shortest period a FirewallLimit may have, in seconds.
 */
inline constexpr std::uint32_t kFirewallLimitMinPeriod = 60;

/**
 * The longest period a FirewallLimit may have, in seconds: 365 days.
 */
inline constexpr std::uint32_t kFirewallLimitMaxPeriod = 31'536'000;

/**
 * A FirewallLimit's Spent and StartDate brought to a given time.
 */
struct FirewallLimitPeriod
{
    STAmount spent;
    std::uint32_t startDate = 0;
};

/**
 * The effective Spent and StartDate of a FirewallLimit at `now`.
 *
 * When the stored period has ended, Spent is zero and StartDate advances by
 * whole periods; otherwise both are the stored values.
 */
[[nodiscard]] FirewallLimitPeriod
firewallLimitPeriod(SLE const& limit, NetClock::time_point now);

/**
 * The (before, after) entry pairs of a transaction that the firewall limits
 * read. `before` is unseated for a created entry and `after` for a deleted one.
 */
using FirewallEntries = std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>>;

/**
 * Whether an entry of this type is kept in FirewallEntries.
 */
[[nodiscard]] bool
isFirewallEntry(LedgerEntryType type) noexcept;

/**
 * The accounts a transaction is matched to for limits with a LimitSigner: its
 * sfDelegate, otherwise the accounts of its signatures. A Batch inner
 * transaction carries no signature and matches none.
 */
[[nodiscard]] std::vector<AccountID>
firewallLimitSigners(STTx const& tx);

/**
 * The keylets of every FirewallLimit of `owner` on `asset` that can bind the
 * transaction: the account-wide one and one per matched signer.
 */
[[nodiscard]] std::vector<Keylet>
firewallMatchingLimits(STTx const& tx, AccountID const& owner, Asset const& asset);

/**
 * The most the transaction's own fields let leave the owner before it is
 * applied, or unseated when its bound is zero.
 */
[[nodiscard]] std::optional<STAmount>
firewallFieldBound(STTx const& tx, AccountID const& backup);

/**
 * The value a CheckCreate or a buy NFTokenCreateOffer commits for a third
 * party to take later, or unseated for every other transaction.
 */
[[nodiscard]] std::optional<STAmount>
firewallCommitment(STTx const& tx);

/**
 * Every asset the owner's balances changed in, plus the asset of the
 * transaction's commitment.
 */
[[nodiscard]] std::vector<Asset>
firewallChargedAssets(FirewallEntries const& entries, STTx const& tx, AccountID const& owner);

/**
 * The owner's net decrease of `asset` over the entry pairs, excluding the fee
 * when the owner paid it.
 */
[[nodiscard]] Number
firewallOutflow(
    FirewallEntries const& entries,
    STTx const& tx,
    AccountID const& owner,
    Asset const& asset,
    XRPAmount fee);

/**
 * What the transaction is charged against the owner's limits on `asset`: the
 * outflow plus the commitment, less what the backup received, and never below
 * zero.
 */
[[nodiscard]] Number
firewallCharge(
    FirewallEntries const& entries,
    STTx const& tx,
    AccountID const& owner,
    AccountID const& backup,
    Asset const& asset,
    XRPAmount fee);

}  // namespace xrpl

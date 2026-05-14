#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <optional>

namespace xrpl {

/** Classifies an account's role in a payment path based on its rippling flags.
 *
 *  An `Outgoing` account has rippling enabled on the inbound trust line used
 *  to reach it and can therefore relay a payment to further hops. An `Incoming`
 *  account has rippling disabled on that line and is a dead end: its own
 *  NoRipple-flagged trust lines cannot carry the payment further. The path
 *  finder uses this distinction to prune illegal rippling paths early.
 */
enum class LineDirection : bool {
    Incoming = false, /**< Reached via a NoRipple-enabled (rippling-disabled)
                           trust line; no further rippling is permitted. */
    Outgoing = true   /**< Source account, or reached via a trust line with
                           rippling enabled; path traversal may continue. */
};

/** Perspective-normalised view of an `ltRIPPLE_STATE` trust line SLE.
 *
 *  The XRPL ledger stores each trust line from a canonical low/high viewpoint
 *  determined by lexicographic `AccountID` ordering, with the balance expressed
 *  from the low account's perspective. This class resolves that asymmetry at
 *  construction time: `viewLowest_` records whether the caller is the low
 *  party, and `balance_` is negated when the caller is the high party. All
 *  accessors then select the correct flag bits and limit fields transparently.
 *
 *  @note The path finder can create tens of millions of instances. Derived
 *      classes are split into `PathFindTrustLine` (no quality rates) and
 *      `RPCTrustLine` (with quality rates) to minimise per-instance footprint.
 *      This base class must not be instantiated directly.
 */
class TrustLineBase
{
public:
    TrustLineBase&
    operator=(TrustLineBase const&) = delete;

protected:
    /** Constructs a perspective-normalised view of a trust line SLE.
     *
     *  Determines which side of the SLE `viewAccount` occupies, sets
     *  `viewLowest_` accordingly, and negates `balance_` when `viewAccount`
     *  is the high party so that `getBalance()` is always self-relative.
     *
     *  @param sle         An `ltRIPPLE_STATE` ledger entry; must be non-null
     *      and of the correct type. Pre-filtering is the caller's responsibility
     *      — see `makeItem` in the derived classes.
     *  @param viewAccount The account from whose perspective balances and flags
     *      are expressed.
     */
    TrustLineBase(std::shared_ptr<SLE const> const& sle, AccountID const& viewAccount);

    ~TrustLineBase() = default;
    TrustLineBase(TrustLineBase const&) = default;
    TrustLineBase(TrustLineBase&&) = default;

public:
    /** Returns the state map key for the ledger entry. */
    [[nodiscard]] uint256 const&
    key() const
    {
        return key_;
    }

    // VFALCO Take off the "get" from each function name

    /** Returns the `AccountID` of the view account (self side). */
    [[nodiscard]] AccountID const&
    getAccountID() const
    {
        return viewLowest_ ? lowLimit_.getIssuer() : highLimit_.getIssuer();
    }

    /** Returns the `AccountID` of the counterparty (peer side). */
    [[nodiscard]] AccountID const&
    getAccountIDPeer() const
    {
        return !viewLowest_ ? lowLimit_.getIssuer() : highLimit_.getIssuer();
    }

    /** Returns true if the view account has granted authorization to the peer.
     *
     *  Authorization is required on the issuer's side before a trustee can
     *  hold a balance when `lsfRequireAuth` is set on the issuing account.
     */
    [[nodiscard]] bool
    getAuth() const
    {
        return (flags_ & (viewLowest_ ? lsfLowAuth : lsfHighAuth)) != 0u;
    }

    /** Returns true if the peer has granted authorization to the view account. */
    [[nodiscard]] bool
    getAuthPeer() const
    {
        return (flags_ & (!viewLowest_ ? lsfLowAuth : lsfHighAuth)) != 0u;
    }

    /** Returns true if the NoRipple flag is set on the view account's side.
     *
     *  When true, payments cannot ripple through this trust line via the view
     *  account, and the line is classified as `LineDirection::Incoming`.
     */
    [[nodiscard]] bool
    getNoRipple() const
    {
        return (flags_ & (viewLowest_ ? lsfLowNoRipple : lsfHighNoRipple)) != 0u;
    }

    /** Returns true if the NoRipple flag is set on the peer's side of the line. */
    [[nodiscard]] bool
    getNoRipplePeer() const
    {
        return (flags_ & (!viewLowest_ ? lsfLowNoRipple : lsfHighNoRipple)) != 0u;
    }

    /** Returns the rippling direction for the view account's side.
     *
     *  `Outgoing` when rippling is enabled (NoRipple off); `Incoming` when
     *  rippling is disabled (NoRipple on). Used by the path finder to determine
     *  whether this account can relay a payment further along a path.
     */
    [[nodiscard]] LineDirection
    getDirection() const
    {
        return getNoRipple() ? LineDirection::Incoming : LineDirection::Outgoing;
    }

    /** Returns the rippling direction for the peer's side of the line. */
    [[nodiscard]] LineDirection
    getDirectionPeer() const
    {
        return getNoRipplePeer() ? LineDirection::Incoming : LineDirection::Outgoing;
    }

    /** Have we set the freeze flag on our peer */
    [[nodiscard]] bool
    getFreeze() const
    {
        return (flags_ & (viewLowest_ ? lsfLowFreeze : lsfHighFreeze)) != 0u;
    }

    /** Have we set the deep freeze flag on our peer */
    [[nodiscard]] bool
    getDeepFreeze() const
    {
        return (flags_ & (viewLowest_ ? lsfLowDeepFreeze : lsfHighDeepFreeze)) != 0u;
    }

    /** Has the peer set the freeze flag on us */
    [[nodiscard]] bool
    getFreezePeer() const
    {
        return (flags_ & (!viewLowest_ ? lsfLowFreeze : lsfHighFreeze)) != 0u;
    }

    /** Has the peer set the deep freeze flag on us */
    [[nodiscard]] bool
    getDeepFreezePeer() const
    {
        return (flags_ & (!viewLowest_ ? lsfLowDeepFreeze : lsfHighDeepFreeze)) != 0u;
    }

    /** Returns the trust line balance from the view account's perspective.
     *
     *  A positive value means the peer owes the view account; negative means
     *  the view account owes the peer. The sign is normalised at construction
     *  time by negating the raw SLE balance when `viewAccount` is the high party.
     */
    [[nodiscard]] STAmount const&
    getBalance() const
    {
        return balance_;
    }

    /** Returns the credit limit the view account has extended to the peer. */
    [[nodiscard]] STAmount const&
    getLimit() const
    {
        return viewLowest_ ? lowLimit_ : highLimit_;
    }

    /** Returns the credit limit the peer has extended to the view account. */
    [[nodiscard]] STAmount const&
    getLimitPeer() const
    {
        return !viewLowest_ ? lowLimit_ : highLimit_;
    }

    /** Returns a minimal diagnostic JSON object with the low and high account IDs.
     *
     *  This is a debug helper; the full `account_lines` JSON is built by
     *  `addLine()` in `AccountLines.cpp` using the individual accessors.
     *
     *  @return A JSON object with string fields `"low_id"` and `"high_id"`.
     */
    json::Value
    getJson(int);

protected:
    uint256 key_;             /**< Ledger state-map key of the `ltRIPPLE_STATE` SLE. */

    STAmount const lowLimit_; /**< Credit limit set by the lexicographically-lower account. */
    STAmount const highLimit_;/**< Credit limit set by the lexicographically-higher account. */

    /** Balance from the view account's perspective; negated at construction
     *  when the view account is the high party. */
    STAmount balance_;

    std::uint32_t flags_;  /**< Raw SLE flags word; queried via `lsfLow*`/`lsfHigh*` masks. */

    /** True when the view account is the low (lexicographically-lesser) party;
     *  drives all perspective-switching in the accessor methods. */
    bool viewLowest_;
};

/** Memory-minimal trust line wrapper used exclusively by the path finder.
 *
 *  Inherits all accessors from `TrustLineBase` but carries no additional data
 *  members, deliberately omitting the quality-in/quality-out transfer rate
 *  fields that `RPCTrustLine` stores. The path finder creates tens of millions
 *  of instances per pathfinding run, so even a few bytes per instance matters.
 *
 *  Live instance counts are tracked via `CountedObject<PathFindTrustLine>` and
 *  can be queried at runtime to diagnose memory pressure from pathfinding.
 */
class PathFindTrustLine final : public TrustLineBase, public CountedObject<PathFindTrustLine>
{
    using TrustLineBase::TrustLineBase;

public:
    PathFindTrustLine() = delete;

    /** Attempts to construct a `PathFindTrustLine` from a candidate SLE.
     *
     *  Returns `std::nullopt` silently when `sle` is null or not of type
     *  `ltRIPPLE_STATE`. The owner directory contains heterogeneous SLE types
     *  (offers, escrows, NFT pages, etc.), so this guard must never throw.
     *
     *  @param accountID The account from whose perspective the line is expressed.
     *  @param sle       The candidate ledger entry from the owner directory.
     *  @return A `PathFindTrustLine` in `std::optional`, or `std::nullopt` if
     *      `sle` is null or not a trust line.
     */
    static std::optional<PathFindTrustLine>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    /** Returns the trust lines for `accountID`, optionally filtered by rippling direction.
     *
     *  Iterates `accountID`'s owner directory and collects all `ltRIPPLE_STATE`
     *  entries. When `direction` is `Incoming`, lines where the NoRipple flag is
     *  set on `accountID`'s side are excluded — those lines cannot carry a
     *  payment further along the path. `Outgoing` (the default) returns all lines.
     *
     *  @param accountID The account whose trust lines are returned.
     *  @param view      The ledger view to read from.
     *  @param direction `Outgoing` includes all trust lines; `Incoming` excludes
     *      lines with NoRipple set on `accountID`'s side.
     *  @return A compacted vector of `PathFindTrustLine` instances.
     */
    static std::vector<PathFindTrustLine>
    getItems(AccountID const& accountID, ReadView const& view, LineDirection direction);
};

/** Trust line wrapper for the `account_lines` RPC handler.
 *
 *  Extends `TrustLineBase` with the four quality-rate fields
 *  (`sfLowQualityIn`, `sfLowQualityOut`, `sfHighQualityIn`, `sfHighQualityOut`)
 *  that the `account_lines` JSON response must include. The correct
 *  perspective-aware rates are selected by `getQualityIn()`/`getQualityOut()`
 *  using the inherited `viewLowest_` flag.
 *
 *  Unlike `PathFindTrustLine`, the `getItems()` overload here has no direction
 *  filter: `account_lines` always returns all trust lines for an account.
 */
class RPCTrustLine final : public TrustLineBase, public CountedObject<RPCTrustLine>
{
    using TrustLineBase::TrustLineBase;

public:
    RPCTrustLine() = delete;

    /** Constructs an `RPCTrustLine` from a trust line SLE.
     *
     *  Extends `TrustLineBase` construction by additionally reading all four
     *  quality rate fields from the SLE. The caller (via `makeItem`) must ensure
     *  `sle` is non-null and of type `ltRIPPLE_STATE`.
     *
     *  @param sle         The `ltRIPPLE_STATE` ledger entry to wrap.
     *  @param viewAccount The account from whose perspective the line is expressed.
     */
    RPCTrustLine(std::shared_ptr<SLE const> const& sle, AccountID const& viewAccount);

    /** Returns the inbound transfer quality rate for the view account's side.
     *
     *  A rate of 1.0 (the default, represented as 0 in the SLE) means no fee
     *  is charged on incoming transfers. Values above 1.0 indicate a surcharge.
     */
    [[nodiscard]] Rate const&
    getQualityIn() const
    {
        return viewLowest_ ? lowQualityIn_ : highQualityIn_;
    }

    /** Returns the outbound transfer quality rate for the view account's side. */
    [[nodiscard]] Rate const&
    getQualityOut() const
    {
        return viewLowest_ ? lowQualityOut_ : highQualityOut_;
    }

    /** Attempts to construct an `RPCTrustLine` from a candidate SLE.
     *
     *  Returns `std::nullopt` silently when `sle` is null or not of type
     *  `ltRIPPLE_STATE`. The owner directory contains heterogeneous SLE types,
     *  so this guard must never throw.
     *
     *  @param accountID The account from whose perspective the line is expressed.
     *  @param sle       The candidate ledger entry from the owner directory.
     *  @return An `RPCTrustLine` in `std::optional`, or `std::nullopt` if
     *      `sle` is null or not a trust line.
     */
    static std::optional<RPCTrustLine>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    /** Returns all trust lines for `accountID` with no direction filter.
     *
     *  Every `ltRIPPLE_STATE` entry in the owner directory is included
     *  regardless of NoRipple configuration, matching `account_lines` semantics.
     *
     *  @note `AccountLines.cpp` uses `forEachItemAfter` and calls `makeItem`
     *      directly for its paginated path; this function is used by bulk,
     *      non-paginated callers such as `GatewayBalances` and
     *      `AccountCurrencies`.
     *
     *  @param accountID The account whose trust lines are returned.
     *  @param view      The ledger view to read from.
     *  @return A compacted vector of `RPCTrustLine` instances.
     */
    static std::vector<RPCTrustLine>
    getItems(AccountID const& accountID, ReadView const& view);

private:
    Rate lowQualityIn_;   /**< Inbound transfer rate for the low party. */
    Rate lowQualityOut_;  /**< Outbound transfer rate for the low party. */
    Rate highQualityIn_;  /**< Inbound transfer rate for the high party. */
    Rate highQualityOut_; /**< Outbound transfer rate for the high party. */
};

}  // namespace xrpl

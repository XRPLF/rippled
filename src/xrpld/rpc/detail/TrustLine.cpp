/** @file
 *  Implements the trust line wrapper hierarchy used by the payment pathfinder
 *  and the `account_lines` RPC command.
 *
 *  Every `ltRIPPLE_STATE` SLE is stored from a ledger-centric perspective with
 *  a "low" and "high" side determined by lexicographic `AccountID` ordering.
 *  `TrustLineBase` resolves this at construction time so all callers see
 *  amounts and flags from a single account's point of view.
 */

#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

/** Constructs a perspective-normalised view of a trust line SLE.
 *
 *  The `ltRIPPLE_STATE` SLE stores balances and limits from the ledger's own
 *  low/high ordering, which depends on the lexicographic ordering of the two
 *  `AccountID` values rather than on who is "self" vs "peer". This constructor
 *  determines which side `viewAccount` occupies (`viewLowest_` = true when
 *  `viewAccount` is the low party) and immediately negates `balance_` when
 *  `viewAccount` is the high party. All accessors then use `viewLowest_`
 *  to select the correct flag bits and limit fields without any further
 *  branching.
 *
 *  @param sle         The `ltRIPPLE_STATE` ledger entry to wrap; must not be
 *      null and must be of the correct type — callers are responsible for
 *      pre-filtering (see `makeItem`).
 *  @param viewAccount The account from whose perspective balances and flags
 *      are expressed.
 */
TrustLineBase::TrustLineBase(std::shared_ptr<SLE const> const& sle, AccountID const& viewAccount)
    : key_(sle->key())
    , lowLimit_(sle->getFieldAmount(sfLowLimit))
    , highLimit_(sle->getFieldAmount(sfHighLimit))
    , balance_(sle->getFieldAmount(sfBalance))
    , flags_(sle->getFieldU32(sfFlags))
    , viewLowest_(lowLimit_.getIssuer() == viewAccount)
{
    if (!viewLowest_)
        balance_.negate();
}

/** Returns a minimal diagnostic JSON object containing only the low and high
 *  account IDs of the trust line.
 *
 *  This is a debug/diagnostic helper rather than the production serialiser;
 *  the full `account_lines` JSON representation is built by `addLine()` in
 *  `AccountLines.cpp` using the individual accessors on `RPCTrustLine`.
 *
 *  @return A JSON object with string fields `"low_id"` and `"high_id"`.
 */
json::Value
TrustLineBase::getJson(int)
{
    json::Value ret(json::ValueType::Object);
    ret["low_id"] = to_string(lowLimit_.getIssuer());
    ret["high_id"] = to_string(highLimit_.getIssuer());
    return ret;
}

/** Attempts to construct a `PathFindTrustLine` from an owner-directory SLE.
 *
 *  Returns `std::nullopt` when `sle` is null or is not an `ltRIPPLE_STATE`
 *  entry. The owner directory contains heterogeneous object types (offers,
 *  escrows, NFT pages, etc.), so this guard must filter aggressively without
 *  throwing — the error is silently contained here and the item is skipped.
 *
 *  @param accountID The account from whose perspective the trust line is
 *      expressed; passed directly to the `TrustLineBase` constructor for
 *      low/high normalisation.
 *  @param sle       The candidate ledger entry from the owner directory.
 *  @return A `PathFindTrustLine` wrapped in `std::optional`, or `std::nullopt`
 *      if `sle` is null or not a trust line.
 */
std::optional<PathFindTrustLine>
PathFindTrustLine::makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle)
{
    if (!sle || sle->getType() != ltRIPPLE_STATE)
        return {};
    return std::optional{PathFindTrustLine{sle, accountID}};
}

namespace detail {

/** Builds a typed collection of trust lines for `accountID` by iterating its
 *  owner directory.
 *
 *  Calls `T::makeItem` on every SLE yielded by `forEachItem`. Non-trust-line
 *  entries (offers, escrows, NFT pages, etc.) are silently skipped by the
 *  `makeItem` null/type guard in each derived class.
 *
 *  For `LineDirection::Incoming` an additional filter is applied: only trust
 *  lines where the NoRipple flag is **not** set on `accountID`'s side are
 *  included. This implements the XRPL pathfinding rule that an account reached
 *  via a NoRipple-disabled line cannot propagate payments through its own
 *  NoRipple-flagged lines. `LineDirection::Outgoing` bypasses this filter and
 *  includes all trust lines.
 *
 *  `shrink_to_fit()` is called before returning because the returned vector
 *  may be cached for an extended period (especially in the pathfinder), and
 *  releasing unused capacity from the initial `push_back` over-allocation is
 *  worth the potential reallocation cost.
 *
 *  @tparam T        `PathFindTrustLine` or `RPCTrustLine`; must provide a
 *      static `makeItem(AccountID const&, shared_ptr<SLE const> const&)`
 *      factory and a `getNoRipple()` accessor.
 *  @param accountID The account whose owner directory is traversed.
 *  @param view      The ledger view to read from.
 *  @param direction Filter applied to the collected lines; defaults to
 *      `Outgoing` (all lines included). Pass `Incoming` to exclude lines
 *      where NoRipple is enabled on `accountID`'s side.
 *  @return A compacted vector of `T` instances representing the matching trust
 *      lines, expressed from `accountID`'s perspective.
 */
template <class T>
std::vector<T>
getTrustLineItems(
    AccountID const& accountID,
    ReadView const& view,
    LineDirection direction = LineDirection::Outgoing)
{
    std::vector<T> items;
    forEachItem(
        view,
        accountID,
        [&items, &accountID, &direction](std::shared_ptr<SLE const> const& sleCur) {
            auto ret = T::makeItem(accountID, sleCur);
            if (ret && (direction == LineDirection::Outgoing || !ret->getNoRipple()))
                items.push_back(std::move(*ret));
        });
    items.shrink_to_fit();

    return items;
}
}  // namespace detail

/** Returns all trust lines for `accountID` filtered by rippling direction.
 *
 *  Delegates to `detail::getTrustLineItems<PathFindTrustLine>`. The
 *  `direction` parameter allows the pathfinder to suppress NoRipple-flagged
 *  outbound lines when it has entered an account via an incoming (NoRipple-
 *  disabled) path segment, preventing illegal payment propagation.
 *
 *  @param accountID The account whose trust lines are returned.
 *  @param view      The ledger view to read from.
 *  @param direction `Outgoing` returns all trust lines; `Incoming` excludes
 *      lines where the NoRipple flag is set on `accountID`'s side.
 *  @return A compacted vector of `PathFindTrustLine` instances.
 */
std::vector<PathFindTrustLine>
PathFindTrustLine::getItems(
    AccountID const& accountID,
    ReadView const& view,
    LineDirection direction)
{
    return detail::getTrustLineItems<PathFindTrustLine>(accountID, view, direction);
}

/** Constructs an `RPCTrustLine` from a trust line SLE.
 *
 *  Extends `TrustLineBase` construction by additionally capturing all four
 *  quality rate fields (`sfLowQualityIn`, `sfLowQualityOut`, `sfHighQualityIn`,
 *  `sfHighQualityOut`) required by the `account_lines` JSON response. The
 *  correct perspective-aware rates are selected at access time via
 *  `getQualityIn()` / `getQualityOut()` using `viewLowest_`.
 *
 *  @param sle         The `ltRIPPLE_STATE` ledger entry; must be non-null and
 *      of the correct type (enforced by `makeItem` before calling here).
 *  @param viewAccount The account from whose perspective the trust line is
 *      expressed.
 */
RPCTrustLine::RPCTrustLine(std::shared_ptr<SLE const> const& sle, AccountID const& viewAccount)
    : TrustLineBase(sle, viewAccount)
    , lowQualityIn_(sle->getFieldU32(sfLowQualityIn))
    , lowQualityOut_(sle->getFieldU32(sfLowQualityOut))
    , highQualityIn_(sle->getFieldU32(sfHighQualityIn))
    , highQualityOut_(sle->getFieldU32(sfHighQualityOut))
{
}

/** Attempts to construct an `RPCTrustLine` from an owner-directory SLE.
 *
 *  Returns `std::nullopt` when `sle` is null or is not an `ltRIPPLE_STATE`
 *  entry. The owner directory contains heterogeneous object types, so this
 *  guard filters silently without throwing — callers need no error handling.
 *
 *  @param accountID The account from whose perspective the trust line is
 *      expressed.
 *  @param sle       The candidate ledger entry from the owner directory.
 *  @return An `RPCTrustLine` wrapped in `std::optional`, or `std::nullopt`
 *      if `sle` is null or not a trust line.
 */
std::optional<RPCTrustLine>
RPCTrustLine::makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle)
{
    if (!sle || sle->getType() != ltRIPPLE_STATE)
        return {};
    return std::optional{RPCTrustLine{sle, accountID}};
}

/** Returns all trust lines for `accountID` with no direction filter.
 *
 *  Delegates to `detail::getTrustLineItems<RPCTrustLine>` with the default
 *  `Outgoing` direction, so every `ltRIPPLE_STATE` entry in the account's
 *  owner directory is included regardless of NoRipple configuration. This
 *  matches `account_lines` semantics, which always returns the complete set.
 *
 *  @note `AccountLines.cpp` does **not** call this function directly for its
 *      paginated path; it uses `forEachItemAfter` and calls `makeItem` per SLE
 *      instead. This function is used by non-paginated bulk callers such as
 *      `GatewayBalances` and `AccountCurrencies`.
 *
 *  @param accountID The account whose trust lines are returned.
 *  @param view      The ledger view to read from.
 *  @return A compacted vector of `RPCTrustLine` instances covering all of
 *      `accountID`'s trust lines.
 */
std::vector<RPCTrustLine>
RPCTrustLine::getItems(AccountID const& accountID, ReadView const& view)
{
    return detail::getTrustLineItems<RPCTrustLine>(accountID, view);
}

}  // namespace xrpl

/** @file
 *  Out-of-line implementations for the Asset unified asset abstraction.
 *
 *  The header contains everything that can be inlined or templated; this
 *  translation unit provides the handful of methods whose bodies are
 *  non-trivial enough to keep out of the header, together with the two
 *  free-function JSON helpers (@ref validJSONAsset and @ref assetFromJson)
 *  that gate JSON ingestion for all asset kinds.
 */

#include <xrpl/protocol/Asset.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <ostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace xrpl {

/** Return the issuing account for the active asset alternative.
 *
 *  Dispatches via `std::visit` to the same-named method on whichever of
 *  `Issue` or `MPTIssue` is currently held.  For XRP (`Issue` with zero
 *  currency), this returns `xrpAccount()`; for MPT it returns the issuing
 *  account embedded in the `MPTID`.
 *
 *  @return A reference into the active alternative's issuer field.
 *      The reference is stable for the lifetime of the `Asset`.
 */
AccountID const&
Asset::getIssuer() const
{
    return std::visit([&](auto&& issue) -> AccountID const& { return issue.getIssuer(); }, issue_);
}

/** Return a human-readable representation of the asset.
 *
 *  Delegates to the `getText()` method on the active alternative.  For
 *  `Issue` this is the currency code (e.g. `"XRP"` or a 3-letter ISO code);
 *  for `MPTIssue` it is the hex-encoded `MPTID`.
 *
 *  @return A string describing the asset, suitable for logging and display.
 *      Not guaranteed to round-trip through a parser.
 */
std::string
Asset::getText() const
{
    return std::visit([&](auto&& issue) { return issue.getText(); }, issue_);
}

/** Populate a JSON object with the fields that identify this asset.
 *
 *  Delegates to the `setJson()` method on the active alternative:
 *  - `Issue` writes `currency` (and `issuer` for non-XRP).
 *  - `MPTIssue` writes `mpt_issuance_id`.
 *
 *  These field sets are mutually exclusive, which is the invariant that
 *  @ref validJSONAsset checks on input.
 *
 *  @param jv  JSON object to populate; existing fields are preserved.
 */
void
Asset::setJson(json::Value& jv) const
{
    std::visit([&](auto&& issue) { issue.setJson(jv); }, issue_);
}

/** Factory that constructs an `STAmount` for this asset from a `Number`.
 *
 *  Provides ergonomic syntax: `myAsset(quantity)` instead of
 *  `STAmount{myAsset, quantity}`.  No arithmetic is performed; the call
 *  purely forwards into the appropriate `STAmount` constructor.
 *
 *  @param number  The magnitude to associate with this asset.
 *  @return An `STAmount` carrying this asset and the given value.
 */
STAmount
Asset::operator()(Number const& number) const
{
    return STAmount{*this, number};
}

/** Return a string representation of an asset for logging and display.
 *
 *  Delegates to the `to_string()` overload for the active alternative
 *  (`Issue` or `MPTIssue`).
 *
 *  @param asset  The asset to convert.
 *  @return A human-readable string identifying the asset.
 */
std::string
to_string(Asset const& asset)
{
    return std::visit([&](auto const& issue) { return to_string(issue); }, asset.value());
}

/** Validate that a JSON object contains a well-formed asset description.
 *
 *  Enforces mutual exclusion between the two asset representations:
 *  - If `mpt_issuance_id` is present, neither `currency` nor `issuer` may
 *    appear — those fields belong exclusively to the `Issue` representation.
 *  - If `mpt_issuance_id` is absent, `currency` must be present.
 *
 *  This is a pure predicate: it returns `bool` and never throws.  It is
 *  intended to be called before @ref assetFromJson so that callers can
 *  produce a clean error response instead of catching a constructor exception.
 *
 *  @param jv  JSON object to inspect.
 *  @return `true` if the object contains a structurally valid asset
 *      description; `false` otherwise.
 */
bool
validJSONAsset(json::Value const& jv)
{
    if (jv.isMember(jss::mpt_issuance_id))
        return !(jv.isMember(jss::currency) || jv.isMember(jss::issuer));
    return jv.isMember(jss::currency);
}

/** Construct an `Asset` from a JSON object.
 *
 *  Exactly one of `currency` or `mpt_issuance_id` must be present.  When
 *  `currency` is present the object is parsed as an `Issue` via
 *  `issueFromJson`; otherwise it is parsed as an `MPTIssue` via
 *  `mptIssueFromJson`.  Deeper field validation (format, consistency) is
 *  delegated to those two sub-parsers.
 *
 *  @note Callers should call @ref validJSONAsset first if they want to
 *      distinguish structural errors from a clean parse failure.
 *
 *  @param v  JSON object containing the asset description.
 *  @return The parsed `Asset`.
 *  @throws std::runtime_error if neither `currency` nor `mpt_issuance_id`
 *      is present in `v`.
 */
Asset
assetFromJson(json::Value const& v)
{
    if (!v.isMember(jss::currency) && !v.isMember(jss::mpt_issuance_id))
        Throw<std::runtime_error>("assetFromJson must contain currency or mpt_issuance_id");

    if (v.isMember(jss::currency))
        return issueFromJson(v);
    return mptIssueFromJson(v);
}

/** Write an asset to an output stream in its human-readable form.
 *
 *  Delegates to the stream-insertion operator of the active alternative
 *  (`Issue` or `MPTIssue`).
 *
 *  @param os  Destination stream.
 *  @param x   Asset to serialize.
 *  @return `os`, to allow chained insertions.
 */
std::ostream&
operator<<(std::ostream& os, Asset const& x)
{
    std::visit([&]<ValidIssueType TIss>(TIss const& issue) { os << issue; }, x.value());
    return os;
}

}  // namespace xrpl

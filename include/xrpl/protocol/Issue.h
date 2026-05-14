#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** Identifies a specific currency as issued by a specific account.
 *
 *  `Issue` is the minimal token identity tuple in the XRPL type system: a
 *  160-bit `Currency` paired with a 160-bit `AccountID`. It is the building
 *  block for trust lines, order books, offer matching, and AMM pools.
 *
 *  XRP is represented as a special-case `Issue` whose `currency` and
 *  `account` fields both carry their respective zero/sentinel values
 *  (`xrpCurrency()` and `xrpAccount()`).  The equality and ordering
 *  operators ignore `account` when `currency` is XRP, so all XRP issues
 *  form a single equivalence class even if `account` carries a stale value.
 *
 *  @note `Issue` is a peer to `MPTIssue`; both satisfy the `IssueType`
 *      concept in `Concepts.h` and can be held inside an `Asset` variant.
 *  @see MPTIssue, Asset, Book
 */
class Issue
{
public:
    /** The 160-bit currency code. `xrpCurrency()` (all-zero) denotes XRP. */
    Currency currency;

    /** The account that issued this currency.
     *
     *  Meaningful only for non-XRP issues.  For XRP issues this field
     *  should carry `xrpAccount()` (all-zero); `isConsistent()` enforces
     *  that invariant, but the comparison operators are lenient and ignore
     *  this field when `currency` is XRP.
     */
    AccountID account;

    Issue() = default;

    /** Constructs an issue from an explicit currency and issuer account.
     *
     *  @param c The currency code.
     *  @param a The issuing account.  Pass `xrpAccount()` when `c` is
     *      `xrpCurrency()`; use `isConsistent()` to verify the pair.
     */
    Issue(Currency const& c, AccountID const& a) : currency(c), account(a)
    {
    }

    /** Returns the issuing account.
     *
     *  Provides a uniform accessor shared with `MPTIssue`, enabling generic
     *  algorithms to retrieve the issuer without a type dispatch.  For XRP
     *  issues the returned value is `xrpAccount()` (all-zero).
     *
     *  @return A reference to `account`.
     */
    [[nodiscard]] AccountID const&
    getIssuer() const
    {
        return account;
    }

    /** Returns a human-readable diagnostic string of the form
     *  `currency[/account]`.
     *
     *  For XRP, only the currency string is returned.  For IOU issues the
     *  account is appended after a slash, substituting `"0"` for
     *  `xrpAccount()` and `"1"` for `noAccount()` so structurally
     *  inconsistent issues are detectable in logs.
     *
     *  @note Field order is `currency/account`, which is the reverse of
     *      `to_string(Issue)`.  Use this for logging; use `setJson()` for
     *      canonical wire output.
     *  @return Diagnostic string; never empty.
     */
    [[nodiscard]] std::string
    getText() const;

    /** Writes the canonical JSON representation into an existing object.
     *
     *  Always sets `jv["currency"]`.  Sets `jv["issuer"]` as a Base58Check
     *  account string only for non-XRP issues; XRP omits `"issuer"`
     *  entirely, which is the authoritative form expected by transaction
     *  JSON, RPC responses, and the binary codec.
     *
     *  @param jv Output JSON object; existing keys are not cleared.
     */
    void
    setJson(json::Value& jv) const;

    /** Returns `true` if this issue represents XRP, the native asset.
     *
     *  Implemented as a full equality comparison against `xrpIssue()`.
     *  The underlying `operator==` short-circuits on `currency` alone for
     *  XRP, so `account` is not consulted.
     *
     *  @return `true` iff `*this == xrpIssue()`.
     */
    [[nodiscard]] bool
    native() const;

    /** Returns `true` if amounts of this issue are stored as integers.
     *
     *  For `Issue`, only XRP uses integer (drop) representation; all IOU
     *  currencies use mantissa/exponent floating-point.  Delegates entirely
     *  to `native()`.
     *
     *  @note `MPTIssue::integral()` always returns `true`.  The shared
     *      method name allows generic code to query integer-vs-float
     *      semantics without a type dispatch.
     *  @return `true` iff `native()`.
     */
    [[nodiscard]] bool
    integral() const;

    friend constexpr std::weak_ordering
    operator<=>(Issue const& lhs, Issue const& rhs);
};

/** Returns `true` if `ac.currency` and `ac.account` agree on XRP-ness.
 *
 *  A well-formed XRP issue must carry `xrpCurrency()` and `xrpAccount()`
 *  (both all-zero).  A well-formed IOU issue must carry a non-zero currency
 *  and a non-zero account.  Cross-contamination — XRP currency with a real
 *  account, or a real currency with the XRP account sentinel — silently
 *  corrupts amount comparisons and offer-book matching.
 *
 *  @note The equality and ordering operators are intentionally more lenient
 *      than this check; they ignore `account` whenever `currency` is XRP.
 *      Call `isConsistent()` on any `Issue` sourced from external input.
 *  @param ac The issue to validate.
 *  @return `true` iff `isXRP(ac.currency) == isXRP(ac.account)`.
 */
bool
isConsistent(Issue const& ac);

/** Returns a string of the form `account/currency`, or just the currency
 *  for XRP issues.
 *
 *  @note Field order is `account/currency`, which is the reverse of
 *      `Issue::getText()` (`currency/account`).  Both formats are in active
 *      use in different parts of the codebase; this one matches
 *      offer-book log lines and stream output.
 *  @param ac The issue to render.
 *  @return A non-empty string identifying the issue.
 */
std::string
to_string(Issue const& ac);

/** Returns the canonical wire-format JSON representation of an issue.
 *
 *  Convenience wrapper around `Issue::setJson()`.  The returned object
 *  contains a `"currency"` field and, for non-XRP issues, an `"issuer"`
 *  field holding the Base58Check-encoded account.
 *
 *  @param is The issue to serialise.
 *  @return A new JSON object representing the issue.
 */
json::Value
toJson(Issue const& is);

/** Parses and validates an `Issue` from a JSON object.
 *
 *  Performs layered validation in strict order:
 *  1. `v` must be a JSON object.
 *  2. `mpt_issuance_id` must be absent — its presence means the caller
 *     has accidentally routed MPT data into the wrong parser.
 *  3. `"currency"` must be a string that parses to neither `badCurrency()`
 *     nor `noCurrency()`.
 *  4. For XRP currency, `"issuer"` must be absent.
 *  5. For non-XRP currencies, `"issuer"` must be a valid Base58Check
 *     account string.
 *
 *  @param v The JSON value to parse; must be a JSON object.
 *  @return The parsed `Issue`.
 *  @throws std::runtime_error if `v` is not an object, or if
 *      `mpt_issuance_id` is present.
 *  @throws Json::error if any field is missing, the wrong type, or carries
 *      an invalid currency code or account string.
 *  @see toJson for the inverse operation.
 */
Issue
issueFromJson(json::Value const& v);

/** Writes the issue to a stream using the `to_string(Issue)` format.
 *
 *  @param os The output stream.
 *  @param x  The issue to write.
 *  @return `os`.
 */
std::ostream&
operator<<(std::ostream& os, Issue const& x);

/** Appends both `currency` and `account` to the hasher unconditionally.
 *
 *  @note The XRP special case from `operator==` (ignoring `account` when
 *      `currency` is XRP) is deliberately not applied here.  Consistent
 *      data — ensured by `isConsistent()` at ingestion — guarantees that
 *      XRP issues always carry `xrpAccount()`, so hashes are stable.
 *      Hashing an inconsistent XRP issue could produce a hash that matches
 *      equality but diverges from a canonical XRP issue's hash.
 *  @tparam Hasher A type satisfying the `beast::hash_append` concept.
 *  @param h The hasher to append to.
 *  @param r The issue whose fields are appended.
 */
template <class Hasher>
void
hash_append(Hasher& h, Issue const& r)
{
    using beast::hash_append;
    hash_append(h, r.currency, r.account);
}

/** Returns `true` if two issues represent the same asset.
 *
 *  Currencies are compared first.  When both currencies are XRP (all-zero),
 *  the `account` field is ignored — all XRP issues are equal regardless of
 *  any stale or partially-constructed account value.  For IOU issues both
 *  fields must match exactly.
 *
 *  @param lhs Left-hand issue.
 *  @param rhs Right-hand issue.
 *  @return `true` iff the two issues identify the same asset.
 */
[[nodiscard]] constexpr bool
operator==(Issue const& lhs, Issue const& rhs)
{
    return (lhs.currency == rhs.currency) && (isXRP(lhs.currency) || lhs.account == rhs.account);
}

/** Provides a strict weak ordering over `Issue` values.
 *
 *  Sorts by `currency` first.  When currencies are equal and the currency is
 *  XRP, `std::weak_ordering::equivalent` is returned immediately so that all
 *  XRP issues form a single equivalence class regardless of the `account`
 *  field.  For IOU issues with equal currencies, `account` is the
 *  tiebreaker.
 *
 *  @param lhs Left-hand issue.
 *  @param rhs Right-hand issue.
 *  @return A `std::weak_ordering` value consistent with `operator==`.
 */
[[nodiscard]] constexpr std::weak_ordering
operator<=>(Issue const& lhs, Issue const& rhs)
{
    if (auto const c{lhs.currency <=> rhs.currency}; c != 0)
        return c;

    if (isXRP(lhs.currency))
        return std::weak_ordering::equivalent;

    return (lhs.account <=> rhs.account);
}

//------------------------------------------------------------------------------

/** Returns the canonical `Issue` sentinel that represents XRP.
 *
 *  The returned instance holds `xrpCurrency()` and `xrpAccount()` (both
 *  all-zero 160-bit values).  The singleton is initialised once and returned
 *  by `const&`, which is thread-safe under C++11 guaranteed-initialisation
 *  semantics.
 *
 *  @return A reference to the process-lifetime XRP issue singleton.
 */
inline Issue const&
xrpIssue()
{
    static Issue const kISSUE{xrpCurrency(), xrpAccount()};
    return kISSUE;
}

/** Returns an `Issue` sentinel that represents the absence of an issue.
 *
 *  Holds `noCurrency()` and `noAccount()`.  Used in contexts where a
 *  missing or invalid issue must be represented without `std::optional`.
 *
 *  @return A reference to the process-lifetime "no issue" singleton.
 */
inline Issue const&
noIssue()
{
    static Issue const kISSUE{noCurrency(), noAccount()};
    return kISSUE;
}

/** Returns `true` if `issue` represents XRP, the native asset.
 *
 *  Thin wrapper over `issue.native()`, providing the naming convention
 *  used throughout the codebase for XRP detection at all abstraction levels.
 *
 *  @param issue The issue to test.
 *  @return `true` iff `issue.native()`.
 */
inline bool
isXRP(Issue const& issue)
{
    return issue.native();
}

}  // namespace xrpl

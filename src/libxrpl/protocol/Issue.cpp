/** @file
 *  Implements Issue: the (Currency, AccountID) pair that identifies every
 *  IOU and XRP asset on the XRP Ledger.
 *
 *  XRP is represented as a special-case Issue whose currency and account
 *  fields are both the zero/XRP sentinel values. All serialisation, string
 *  rendering, and JSON parsing for non-MPT assets flows through this file.
 */

#include <xrpl/protocol/Issue.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <ostream>
#include <stdexcept>
#include <string>

namespace xrpl {

/** Returns a human-readable diagnostic string of the form
 *  `currency[/account]`.
 *
 *  For XRP (zero currency), only the currency string is returned with no
 *  slash suffix.  For IOU issues the account is rendered after a slash,
 *  substituting `"0"` for the XRP sentinel account and `"1"` for
 *  `noAccount()`.
 *
 *  @note This format differs from `to_string(Issue)`, which reverses the
 *      field order to `account/currency`.  Use `getText()` for logging and
 *      diagnostics; use `setJson()` for canonical wire output.
 *  @return Diagnostic string; never empty.
 */
std::string
Issue::getText() const
{
    std::string ret;

    ret.reserve(64);
    ret = to_string(currency);

    if (!isXRP(currency))
    {
        ret += "/";

        if (isXRP(account))
        {
            ret += "0";
        }
        else if (account == noAccount())
        {
            ret += "1";
        }
        else
        {
            ret += to_string(account);
        }
    }

    return ret;
}

/** Writes the canonical wire-format JSON representation into @p jv.
 *
 *  Always sets `jv["currency"]`.  Sets `jv["issuer"]` as a Base58Check
 *  account string only for non-XRP issues; XRP omits `"issuer"` entirely,
 *  which is the authoritative form expected by transaction JSON, RPC
 *  responses, and the binary codec.
 *
 *  @param jv Output JSON object; existing keys are not cleared.
 */
void
Issue::setJson(json::Value& jv) const
{
    jv[jss::currency] = to_string(currency);
    if (!isXRP(currency))
        jv[jss::issuer] = toBase58(account);
}

/** Returns `true` if this issue represents XRP (the native asset).
 *
 *  Implemented as a full equality comparison against `xrpIssue()`.
 *  The underlying `operator==` short-circuits on the currency field alone
 *  when `isXRP(currency)` is true, so the account field is not consulted
 *  for XRP values.
 *
 *  @return `true` iff `*this == xrpIssue()`.
 */
bool
Issue::native() const
{
    return *this == xrpIssue();
}

/** Returns `true` if amounts of this issue are stored as integers.
 *
 *  For `Issue`, only XRP is integral (amounts are denominated in
 *  indivisible drops).  All IOU currencies use floating-point mantissa
 *  and exponent representation.  Delegates entirely to `native()`.
 *
 *  @note `MPTIssue::integral()` always returns `true` because MPT amounts
 *      are also integers.  The shared method name enables generic code
 *      operating on either asset kind to query precision behaviour without
 *      a type-dispatch.
 *  @return `true` iff `native()`.
 */
bool
Issue::integral() const
{
    return native();
}

/** Returns `true` if the currency and account fields are mutually consistent.
 *
 *  The invariant is that both fields must agree on XRP-ness: either both are
 *  the XRP/zero sentinel or neither is.  A cross-contaminated issue (XRP
 *  currency with a real account, or a real currency with the XRP account
 *  sentinel) would silently corrupt amount comparisons and offer-book
 *  matching.  Call this on any newly constructed `Issue` that originates
 *  from external input.
 *
 *  @param ac The issue to validate.
 *  @return `true` iff `isXRP(ac.currency) == isXRP(ac.account)`.
 */
bool
isConsistent(Issue const& ac)
{
    return isXRP(ac.currency) == isXRP(ac.account);
}

/** Returns a string representation of the form `account/currency`, or just
 *  the currency string when the account is the XRP sentinel.
 *
 *  @note The field order (`account/currency`) is the reverse of
 *      `Issue::getText()` (`currency/account`).  This asymmetry is
 *      historical: offer-book keys and engine log lines have always used
 *      this order.  Both formats are in active use in different parts of
 *      the codebase.
 *  @param ac The issue to render.
 *  @return A non-empty string identifying the issue.
 */
std::string
to_string(Issue const& ac)
{
    if (isXRP(ac.account))
        return to_string(ac.currency);

    return to_string(ac.account) + "/" + to_string(ac.currency);
}

/** Returns the canonical wire-format JSON representation of an issue.
 *
 *  Convenience wrapper around `Issue::setJson()`.
 *
 *  @param is The issue to serialise.
 *  @return A JSON object with a `"currency"` field and, for non-XRP issues,
 *      an `"issuer"` field.
 */
json::Value
toJson(Issue const& is)
{
    json::Value jv;
    is.setJson(jv);
    return jv;
}

/** Parses and validates an `Issue` from a JSON object.
 *
 *  Performs layered validation in strict order:
 *  1. @p v must be a JSON object (not a string or array).
 *  2. The `mpt_issuance_id` field must be absent — its presence signals
 *     that the caller has routed MPT data into the wrong parser.
 *  3. `"currency"` must be a JSON string that parses to something other
 *     than `badCurrency()` (the literal three-letter code `"XRP"`, which
 *     is reserved) or `noCurrency()` (parse failure sentinel).
 *  4. For XRP currency, `"issuer"` must be absent.
 *  5. For non-XRP currencies, `"issuer"` must be a string that decodes as
 *     a valid Base58Check account ID.
 *
 *  Two exception types are used intentionally: `Json::error` for malformed
 *  JSON values (wrong type, missing or extra fields), and
 *  `std::runtime_error` for structural misuse (non-object input or
 *  MPT-typed JSON routed here).  Callers that only care about format errors
 *  may catch the narrower type.
 *
 *  @param v The JSON value to parse; must be an object.
 *  @return The parsed `Issue`.
 *  @throws std::runtime_error if @p v is not an object, or if
 *      `mpt_issuance_id` is present.
 *  @throws Json::error if any field is missing, wrong type, or carries an
 *      invalid currency code or account string.
 *  @see mptIssueFromJson for the parallel MPT parser.
 */
Issue
issueFromJson(json::Value const& v)
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "issueFromJson can only be specified with an 'object' Json value");
    }

    if (v.isMember(jss::mpt_issuance_id))
    {
        Throw<std::runtime_error>("issueFromJson, Issue should not have mpt_issuance_id");
    }

    json::Value const curStr = v[jss::currency];
    json::Value const issStr = v[jss::issuer];

    if (!curStr.isString())
    {
        Throw<json::Error>("issueFromJson currency must be a string Json value");
    }

    auto const currency = toCurrency(curStr.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<json::Error>("issueFromJson currency must be a valid currency");
    }

    if (isXRP(currency))
    {
        if (!issStr.isNull())
        {
            Throw<json::Error>("Issue, XRP should not have issuer");
        }
        return xrpIssue();
    }

    if (!issStr.isString())
    {
        Throw<json::Error>("issueFromJson issuer must be a string Json value");
    }
    auto const issuer = parseBase58<AccountID>(issStr.asString());

    if (!issuer || *issuer == noAccount() || *issuer == xrpAccount())
    {
        Throw<json::Error>("issueFromJson issuer must be a valid account");
    }

    return Issue{currency, *issuer};
}

/** Writes the issue to a stream using the `to_string(Issue)` format.
 *
 *  @param os The output stream.
 *  @param x  The issue to write.
 *  @return @p os.
 */
std::ostream&
operator<<(std::ostream& os, Issue const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace xrpl

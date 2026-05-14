/** @file
 *  Method implementations for `RPC::Status`, the unified error-result type
 *  for the XRPL RPC layer.
 *
 *  Converts internal status state — spanning three error code spaces (`TER`,
 *  `error_code_i`, and raw integers) — into human-readable strings and
 *  structured JSON output. All output methods short-circuit to empty/no-op
 *  when `code_ == kOK`, consistent with the `operator bool()` invariant
 *  defined in the header.
 */

#include <xrpld/rpc/Status.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/jss.h>

#include <sstream>
#include <string>

namespace xrpl::RPC {

/** Returns a human-readable string describing the error code.
 *
 *  Dispatches on `type_` to the appropriate lookup function for each code
 *  space:
 *  - `Type::None`: decimal representation of `code_` via `std::to_string`.
 *  - `Type::TER`: TER token and description from `transResultInfo()`, e.g.
 *    `"temBAD_AMOUNT: Malformed: Bad amount."`. The `[[maybe_unused]]` on the
 *    return value acknowledges that in release builds the `XRPL_ASSERT` is
 *    elided, but the call is still required for its output arguments.
 *  - `Type::ErrorCodeI`: token and message from `getErrorInfo()`, formatted
 *    as `"token: message"` via `ostringstream`.
 *
 *  The final `UNREACHABLE` / `LCOV_EXCL_*` block documents that no valid
 *  `type_` value reaches that path; coverage tooling is instructed to ignore
 *  the dead branch.
 *
 *  @return Formatted code string, or empty string if `code_ == kOK`.
 */
std::string
Status::codeString() const
{
    if (!*this)
        return "";

    if (type_ == Type::None)
        return std::to_string(code_);

    if (type_ == Status::Type::TER)
    {
        std::string s1, s2;

        [[maybe_unused]] auto const success = transResultInfo(toTER(), s1, s2);
        XRPL_ASSERT(success, "xrpl::RPC::codeString : valid TER result");

        return s1 + ": " + s2;
    }

    if (type_ == Status::Type::ErrorCodeI)
    {
        auto info = getErrorInfo(toErrorCode());
        std::ostringstream sStr;
        sStr << info.token.cStr() << ": " << info.message.cStr();
        return sStr.str();
    }

    // LCOV_EXCL_START
    UNREACHABLE("xrpl::RPC::codeString : invalid type");
    return "";
    // LCOV_EXCL_STOP
}

/** Populates a JSON-RPC 2.0 error object into `value`.
 *
 *  Writes `value["error"]["code"]` (raw integer), `value["error"]["message"]`
 *  (`codeString()` result), and — when `messages_` is non-empty —
 *  `value["error"]["data"]` (array of supplemental diagnostic strings).
 *  Has no effect when `code_ == kOK`, so callers may invoke this
 *  unconditionally.
 *
 *  @note This method is not used by the active RPC dispatch pipeline; `inject()`
 *      is the production path. `fillJson()` is preserved as a future migration
 *      target toward full JSON-RPC 2.0 conformance.
 *  @param value The JSON object to populate with the error sub-object.
 */
void
Status::fillJson(json::Value& value)
{
    if (!*this)
        return;

    auto& error = value[jss::error];
    error[jss::code] = code_;
    error[jss::message] = codeString();

    if (!messages_.empty())
    {
        auto& messages = error[jss::data];
        for (auto& i : messages_)
            messages.append(i);
    }
}

/** Returns all attached diagnostic messages joined by `'/'` separators.
 *
 *  Iterates `messages_` and concatenates each entry with a `'/'` delimiter
 *  between entries. Returns an empty string when no messages are attached.
 *
 *  @return Concatenated message string, or empty string if `messages_` is empty.
 */
std::string
Status::message() const
{
    std::string result;
    for (auto& m : messages_)
    {
        if (!result.empty())
            result += '/';
        result += m;
    }

    return result;
}

/** Returns a composite diagnostic string combining the code and messages.
 *
 *  Produces `codeString() + ":" + message()` when the status represents an
 *  error, or an empty string when OK. The `':'` separator is always present
 *  even when `message()` returns empty, so callers that only need the code
 *  portion should call `codeString()` directly.
 *
 *  @return Combined diagnostic string, or empty string if `code_ == kOK`.
 */
std::string
Status::toString() const
{
    if (*this)
        return codeString() + ":" + message();
    return "";
}

}  // namespace xrpl::RPC

#pragma once

/**
 * Helpers that set path-finding span attributes from RPC request fields.
 *
 * The path_find and ripple_path_find handlers both record the request's
 * source and destination accounts on the pathfind.request span. The value
 * comes from client input, so it is emitted only when it parses as an
 * r-address; a malformed or mistaken value never reaches the span. The
 * address itself is a public ledger identifier and is emitted raw (see the
 * attribute docs in PathFindSpanNames.h).
 *
 *   doPathFind() / doRipplePathFind()
 *       │  params[jss::source_account], read through a const json::Value
 *       ▼
 *   setAccountAttribute(span, key, field)        (this header)
 *       │  parseBase58<AccountID>: nullopt -> nothing emitted
 *       ▼
 *   span.setAttribute(key, toBase58(account))
 *
 * @code
 *     // Primary use, inside the span-live guard of a handler:
 *     auto const& params = std::as_const(context.params);
 *     pathfind_span::setAccountAttribute(
 *         span, pathfind_span::attr::sourceAccount, params[jss::source_account]);
 * @endcode
 *
 * @code
 *     // Edge cases: a missing field (null), a non-string, or a string that is
 *     // not an r-address all leave the span untouched.
 *     pathfind_span::setAccountAttribute(span, key, json::Value{});
 *     pathfind_span::setAccountAttribute(span, key, json::Value{"not an address"});
 * @endcode
 *
 * @note Not a hot path: one Base58 decode per account per RPC call, and only
 * while the span is live. Thread-safe; it holds no state.
 */

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <string_view>

namespace xrpl::telemetry::pathfind_span {

/**
 * Set an account attribute on a path-finding span when the request field
 * holds an r-address.
 *
 * @param span  The live pathfind.request span.
 * @param key   The attribute key, pathfind_source_account or
 *              pathfind_dest_account.
 * @param field The request parameter. Read it through a const json::Value so
 *              a missing key is not inserted into the request.
 */
inline void
setAccountAttribute(ScopedSpanGuard& span, std::string_view key, json::Value const& field)
{
    if (!field.isString())
        return;
    if (auto const account = parseBase58<AccountID>(field.asString()))
        span.setAttribute(key, toBase58(*account));
}

}  // namespace xrpl::telemetry::pathfind_span

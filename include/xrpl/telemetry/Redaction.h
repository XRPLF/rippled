#pragma once

/**
 * Account-address redaction helper for telemetry span attributes.
 *
 * A single pure helper that turns a string into a short, stable,
 * obfuscated token, for a span attribute whose value should not be stored
 * in the clear and is hard to guess.
 *
 * Not applied to any span today. Account addresses are public ledger
 * identifiers, so the path-finding spans emit them raw (see
 * PathFindSpanNames.h) and no collector processor hashes them. Use this
 * helper only for a value that is genuinely private, and document the
 * reason at the attribute constant.
 *
 * The returned token is the first 16 hex characters (lowercase) of the
 * SHA-512Half digest of the input. It is deterministic (same input
 * always maps to the same token) so spans for one value still correlate
 * across nodes and restarts.
 *
 * The hash is unsalted, so it is obfuscation, not a secrecy guarantee.
 * It hides a value only when that value is hard to guess: for an input
 * drawn from a small or enumerable set, such as an account address, an
 * observer can rebuild the value->token mapping by lookup, which is why
 * account addresses are emitted raw instead. A salt is intentionally
 * omitted because it would break cross-node/restart correlation, which is
 * the reason for hashing rather than dropping.
 *
 * @note This function is pure and reentrant: it holds no global state,
 * performs no I/O, and is safe to call concurrently from any thread.
 *
 * Usage example:
 * @code
 * #include <xrpl/telemetry/Redaction.h>
 * using namespace xrpl::telemetry;
 *
 * auto const token = redactAccount(value);  // 16 lowercase hex chars
 * @endcode
 *
 * Edge case (empty input yields empty output):
 * @code
 * assert(redactAccount("") == "");
 * @endcode
 */

#include <string>
#include <string_view>

namespace xrpl::telemetry {

/**
 * Hash a value into a short, stable, obfuscated token.
 *
 * @param addr  The value to redact. Named for its original use on account
 * addresses; any string can be passed.
 * @return The first 16 lowercase hex characters of sha512Half(addr),
 * or an empty string when @p addr is empty.
 */
[[nodiscard]] std::string
redactAccount(std::string_view addr);

}  // namespace xrpl::telemetry

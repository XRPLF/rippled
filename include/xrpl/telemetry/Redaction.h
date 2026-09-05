#pragma once

/**
 * Account-address redaction for telemetry span attributes.
 *
 * Path-finding RPC handlers would otherwise emit the caller's raw
 * account addresses as span attributes. To keep plaintext addresses out
 * of the telemetry backend, they are hashed at the point of emission.
 * This header exposes a single pure helper that turns an address into a
 * short, stable, obfuscated token.
 *
 * Data flow:
 *
 * handler -> redactAccount(addr) -> span attribute -> OTLP export
 *
 * The returned token is the first 16 hex characters (lowercase) of the
 * SHA-512Half digest of the address. It is deterministic (same address
 * always maps to the same token) so operators can still correlate spans
 * for a given account across nodes and restarts.
 *
 * The hash is unsalted, so it is obfuscation, not a secrecy guarantee:
 * XRP account addresses are a public, enumerable set, so a determined
 * observer with the telemetry stream could rebuild the address->token
 * mapping. The goal here is to keep plaintext addresses out of traces
 * and dashboards, not to defend against a precomputation attack. A salt
 * is intentionally omitted because it would break cross-node/restart
 * correlation, which is the reason for hashing rather than dropping.
 *
 * A second, independent hashing layer runs in the OpenTelemetry
 * Collector (an `attributes/hash` processor) as defense-in-depth for
 * any node that emits a raw value.
 *
 * @note This function is pure and reentrant: it holds no global state,
 * performs no I/O, and is safe to call concurrently from any thread.
 *
 * Usage example:
 * @code
 * #include <xrpl/telemetry/Redaction.h>
 * using namespace xrpl::telemetry;
 *
 * span.setAttribute(
 * pathfind_span::attr::sourceAccount, redactAccount(src.asString()));
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
 * Hash an account address into a short, stable, obfuscated token.
 *
 * @param addr  The account address to redact (e.g. an r-address).
 * @return The first 16 lowercase hex characters of sha512Half(addr),
 * or an empty string when @p addr is empty.
 */
[[nodiscard]] std::string
redactAccount(std::string_view addr);

}  // namespace xrpl::telemetry

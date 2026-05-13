#pragma once

/** Compile-time string concatenation utility and shared telemetry constants.
 *
 *  Provides StaticStr<N> — a compile-time string buffer that implicitly
 *  converts to std::string_view — and join() for dot-separated concatenation.
 *  Module-specific span names (e.g. RPC, consensus) live in their respective
 *  modules and build upon these shared primitives.
 *
 *  @note These constants are NOT guarded by XRPL_ENABLE_TELEMETRY because
 *  call sites reference them even when SpanGuard methods are no-ops
 *  (the no-op stubs still accept string_view parameters). The compiler
 *  elides all inline constexpr values whose only uses are in dead code.
 *
 *  @note Json::StaticString (jss.h) is a pointer wrapper without
 *  concatenation support. boost::static_string is not constexpr.
 *  StaticStr<N> exists specifically for compile-time dot-join composition.
 *
 *  Naming conventions (see spec 2026-05-13-span-attr-naming-design):
 *  - Per-span attribute keys: bare field name (span name carries the domain).
 *  - Collision qualifier: <domain>_<field> when bare name collides across
 *    domains or with OTel reserved `status` (e.g. rpc_status, grpc_status).
 *  - Resource attribute keys: xrpl.<subsystem>.<field> (process-identity).
 *  - Span prefixes: <subsystem>[.<component>].
 */

#include <cstddef>
#include <string_view>

namespace xrpl::telemetry {

// ===== Compile-time string utility =========================================

/// Fixed-size character buffer for compile-time string operations.
/// Implicitly converts to std::string_view at zero cost.
template <std::size_t N>
struct StaticStr
{
    char data[N + 1]{};
    static constexpr std::size_t size = N;

    constexpr StaticStr() = default;

    constexpr explicit StaticStr(char const (&str)[N + 1])
    {
        for (std::size_t i = 0; i <= N; ++i)
            data[i] = str[i];
    }

    constexpr
    operator std::string_view() const noexcept
    {
        return {data, N};
    }
};

/// Deduction guide: StaticStr from string literal.
template <std::size_t N>
StaticStr(char const (&)[N]) -> StaticStr<N - 1>;

/// Create a StaticStr from a string literal.
template <std::size_t N>
constexpr auto
makeStr(char const (&str)[N])
{
    return StaticStr<N - 1>(str);
}

/// Concatenate two StaticStr values with a dot separator.
template <std::size_t A, std::size_t B>
constexpr auto
join(StaticStr<A> const& lhs, StaticStr<B> const& rhs)
{
    constexpr std::size_t len = A + 1 + B;  // lhs + '.' + rhs
    StaticStr<len> result;
    std::size_t pos = 0;
    for (std::size_t i = 0; i < A; ++i)
        result.data[pos++] = lhs.data[i];
    result.data[pos++] = '.';
    for (std::size_t i = 0; i < B; ++i)
        result.data[pos++] = rhs.data[i];
    result.data[pos] = '\0';
    return result;
}

// ===== Shared root segments ================================================

namespace seg {
inline constexpr auto xrpl = makeStr("xrpl");
inline constexpr auto rpc = makeStr("rpc");
inline constexpr auto tx = makeStr("tx");
inline constexpr auto consensus = makeStr("consensus");
inline constexpr auto peer = makeStr("peer");
inline constexpr auto ledger = makeStr("ledger");
inline constexpr auto network = makeStr("network");
inline constexpr auto link = makeStr("link");
}  // namespace seg

// ===== Shared attribute keys (used across modules) =========================

namespace attr {
inline constexpr auto networkId = join(join(seg::xrpl, seg::network), makeStr("id"));
inline constexpr auto networkType = join(join(seg::xrpl, seg::network), makeStr("type"));
inline constexpr auto linkType = makeStr("link_type");

/// Node health attributes — RESOURCE-ONLY (process identity, not per-span).
/// Set at Tracer init via resource::Resource::Create and refreshed on state
/// transitions. Do NOT use with span.setAttribute().
inline constexpr auto xrplNode = join(seg::xrpl, makeStr("node"));
/// "xrpl.node.amendment_blocked" — resource attribute key.
inline constexpr auto nodeAmendmentBlocked = join(xrplNode, makeStr("amendment_blocked"));
/// "xrpl.node.server_state" — resource attribute key.
inline constexpr auto nodeServerState = join(xrplNode, makeStr("server_state"));

/// Canonical shared attrs (rule 5 — kept xrpl.<domain>.* form).
/// Defined once here, aliased by domain-specific headers.
inline constexpr auto txHash = join(join(seg::xrpl, seg::tx), makeStr("hash"));
inline constexpr auto peerId = join(join(seg::xrpl, seg::peer), makeStr("id"));
inline constexpr auto ledgerSeq = join(join(seg::xrpl, seg::ledger), makeStr("seq"));

/// Shared close-time attrs — bare names, reused by consensus and ledger.
inline constexpr auto closeTime = makeStr("close_time");
inline constexpr auto closeTimeCorrect = makeStr("close_time_correct");
inline constexpr auto closeResolutionMs = makeStr("close_resolution_ms");
}  // namespace attr

// ===== Shared attribute values =============================================

namespace attr_val {
inline constexpr auto success = makeStr("success");
inline constexpr auto error = makeStr("error");
inline constexpr auto followsFrom = makeStr("follows_from");
}  // namespace attr_val

}  // namespace xrpl::telemetry

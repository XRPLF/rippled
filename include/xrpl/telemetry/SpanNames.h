#pragma once

/**
 * Compile-time string concatenation utility and shared telemetry constants.
 *
 *  Provides StaticStr<N> — a compile-time string buffer that implicitly
 *  converts to std::string_view — and join() for dot-separated concatenation.
 *  Module-specific span names (e.g. RPC, consensus) live in their respective
 *  modules and build upon these shared primitives.
 *
 * @note These constants are NOT guarded by XRPL_ENABLE_TELEMETRY because
 *  call sites reference them even when SpanGuard methods are no-ops
 *  (the no-op stubs still accept string_view parameters). The compiler
 *  elides all inline constexpr values whose only uses are in dead code.
 *
 * @note Json::StaticString (jss.h) is a pointer wrapper without
 *  concatenation support. boost::static_string is not constexpr.
 *  StaticStr<N> exists specifically for compile-time dot-join composition.
 *
 *  Naming conventions (see spec 2026-05-13-span-attr-naming-design):
 *  - Per-span attribute keys: bare field name (span name carries the domain).
 *  - Collision qualifier: <domain>_<field> when bare name collides across
 *    domains or with OTel reserved `status` (e.g. rpc_status, grpc_status).
 *  - Shared cross-span attributes: <domain>_<field> (underscore) form
 *    (e.g. tx_hash, peer_id, ledger_seq, consensus_round).
 *  - Resource attribute keys: xrpl.<subsystem>.<field> (dotted) form is
 *    RESERVED for process-identity attributes set once at startup on the
 *    OTel resource (e.g. xrpl.network.id, xrpl.network.type). Do not use
 *    this form for span attributes — it parses awkwardly in TraceQL and
 *    blurs the resource/span scope distinction.
 *  - Span prefixes: <subsystem>[.<component>].
 */

#include <cstddef>
#include <string_view>

namespace xrpl::telemetry {

// ===== Compile-time string utility =========================================

/**
 * Fixed-size character buffer for compile-time string operations.
 * Implicitly converts to std::string_view at zero cost.
 */
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

/**
 * Deduction guide: StaticStr from string literal.
 */
template <std::size_t N>
StaticStr(char const (&)[N]) -> StaticStr<N - 1>;

/**
 * Create a StaticStr from a string literal.
 */
template <std::size_t N>
constexpr auto
makeStr(char const (&str)[N])
{
    return StaticStr<N - 1>(str);
}

/**
 * Concatenate two StaticStr values with a dot separator.
 */
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

/**
 * Canonical shared attrs (rule 5 — <domain>_<field> underscore form).
 *
 * Per the naming convention header note: shared cross-span attribute
 * keys use the underscore form, reserving the dotted xrpl.<domain>.<field>
 * form for resource attributes set on the OTel resource at startup.
 * Defined once here, aliased by domain-specific headers. These are
 * literal underscore-joined names, not dot-joined via `join()`, since
 * `join()` always inserts `.` between its arguments.
 */
inline constexpr auto txHash = makeStr("tx_hash");
inline constexpr auto peerId = makeStr("peer_id");
inline constexpr auto ledgerSeq = makeStr("ledger_seq");

/**
 * Shared "ledger being worked on" attrs — the open/tentative or in-flight
 * consensus-build ledger a transaction is applied into, NOT an established or
 * validated ledger (that is `ledgerSeq`, set on ledger.build / consensus.round).
 * Named after the RPC field `ledger_current_index` and the `currentLedgerSeq`
 * log usage. Reused by the tx lifecycle, apply-pipeline, and TxQ spans so a
 * transaction's work can be correlated to the ledger it targeted.
 * `currentLedgerHash` is the current view's parent-ledger hash, which equals the
 * consensus.round deterministic trace-id seed on the consensus-build path.
 */
inline constexpr auto currentLedgerSeq = makeStr("current_ledger_seq");
inline constexpr auto currentLedgerHash = makeStr("current_ledger_hash");
}  // namespace attr

// ===== Shared attribute values =============================================

namespace attr_val {
inline constexpr auto success = makeStr("success");
inline constexpr auto error = makeStr("error");
}  // namespace attr_val

}  // namespace xrpl::telemetry

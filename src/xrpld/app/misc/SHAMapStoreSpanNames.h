// cspell:ignore Wreturn
#pragma once

/**
 * Span, attribute and outcome constants for the online-delete rotation
 * (SHAMapStoreImp::run). One root span per rotation, one child per phase.
 * Built on StaticStr/join() from SpanNames.h, same shape as LedgerSpanNames.h.
 */

#include <xrpl/telemetry/SpanNames.h>

#include <string_view>

namespace xrpl::telemetry::nodestore_span {

namespace op {
inline constexpr auto rotate = makeStr("rotate");
}  // namespace op

/**
 * "nodestore.rotate" — the root; also the prefix every phase joins onto.
 */
inline constexpr auto rotateFull = join(seg::nodestore, op::rotate);

// ===== Phase suffixes (child span names are rotateFull + "." + phase) ======
namespace phase {
inline constexpr auto clearPrior = makeStr("clear_prior");
inline constexpr auto copy = makeStr("copy");
inline constexpr auto freshenKeys = join(makeStr("freshen"), makeStr("keys"));
inline constexpr auto freshenFetch = join(makeStr("freshen"), makeStr("fetch"));
inline constexpr auto newBackend = makeStr("new_backend");
inline constexpr auto clearCaches = makeStr("clear_caches");
inline constexpr auto swap = makeStr("swap");
inline constexpr auto healthWait = makeStr("health_wait");
}  // namespace phase

// ===== Attribute keys ========================================================
namespace attr {
using telemetry::attr::ledgerSeq;
inline constexpr auto lastRotated = makeStr("last_rotated");
inline constexpr auto nodeCount = makeStr("node_count");
inline constexpr auto keyCount = makeStr("key_count");
inline constexpr auto copyForwards = makeStr("copy_forwards");
inline constexpr auto serverMode = makeStr("server_mode");
inline constexpr auto missingLedgers = makeStr("missing_ledgers");
inline constexpr auto outcome = makeStr("outcome");
}  // namespace attr

// ===== Outcome values ========================================================
namespace val {
inline constexpr auto complete = makeStr("complete");
inline constexpr auto expired = makeStr("expired");
inline constexpr auto stopping = makeStr("stopping");
inline constexpr auto missingNode = makeStr("missing_node");
}  // namespace val

/**
 * How a rotation left run()'s rotation block. One value per exit path:
 * Complete after "finished rotation", Expired/Stopping from any healthWait(),
 * MissingNode from the SHAMapMissingNode catch around the copy walk.
 */
enum class RotationExit { Complete, Expired, Stopping, MissingNode };

/**
 * The outcome attribute for an exit. Total over the enum.
 */
[[nodiscard]] constexpr std::string_view
rotationOutcome(RotationExit exit) noexcept
{
    switch (exit)
    {
        case RotationExit::Complete:
            return val::complete;
        case RotationExit::Expired:
            return val::expired;
        case RotationExit::Stopping:
            return val::stopping;
        case RotationExit::MissingNode:
            return val::missingNode;
    }
    // Unreachable: the switch is exhaustive over RotationExit.
    // The return silences -Wreturn-type on GCC.
    return val::missingNode;
}

}  // namespace xrpl::telemetry::nodestore_span

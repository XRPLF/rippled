#pragma once

// cspell:ignore ISTOGRAM
// The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD trips cspell's
// compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here.

/**
 * Metric names and descriptions for the RPC request-count histograms.
 *
 * These two instruments measure how much work one request asks for. A span
 * attribute cannot answer that in aggregate: it is readable only on a trace
 * that was sampled, so it describes one call and never the distribution over
 * all of them. The span attributes stay in place for single-request
 * debugging; these histograms carry the shape.
 *
 * Each name is used at two sites, which is why they are shared constants
 * rather than literals:
 *
 *   RpcMetricNames.h
 *          |
 *          +--> ServerHandler.cpp    (records kRpcBatchSize)
 *          |
 *          +--> PathRequest.cpp      (records kPathfindDiscoveredPaths)
 *          |
 *          +--> MetricsRegistry.cpp  (addHistogramView: registers the
 *                                     explicit bucket edges for both)
 *
 * A drifted name silently drops the bucket override. The SDK default edges
 * start at 0, 5, 10, 25, so every batch of one to five sub-requests would
 * then land in one bucket and every quantile would be an interpolation
 * inside it.
 *
 * Placed under `include/xrpl/telemetry/` for the same reason
 * GetObjectMetricNames.h is: the record sites are `xrpld.rpc` and the view
 * registration is `xrpld.telemetry`, and `include/xrpl/` is the one level
 * both modules are allowed to reach.
 *
 * Example usage -- recording the batch size:
 * @code
 * if (batch)
 * {
 *     span.setAttribute(rpc_span::attr::batchSize, static_cast<int64_t>(size));
 *     XRPL_METRIC_HISTOGRAM_RECORD(app_, kRpcBatchSize, kRpcBatchSizeDesc, size);
 * }
 * @endcode
 *
 * Example usage -- edge case: a pathfinding pass that produced no paths still
 * records, because "found nothing" is the observation that matters most on
 * this instrument:
 * @code
 * std::int64_t totalPaths = 0;   // no source asset yielded a path
 * XRPL_METRIC_HISTOGRAM_RECORD(
 *     app_, kPathfindDiscoveredPaths, kPathfindDiscoveredPathsDesc, totalPaths);
 * @endcode
 *
 * @note These are `constexpr char[]`, not `std::string_view`. The OTel C++
 * API takes `nostd::string_view`, which has no converting constructor from
 * `std::string_view` on this build, so a `string_view` constant would not
 * compile at the call sites. Same convention as GetObjectMetricNames.h.
 *
 * @note Both share `buckets::kObjectCountBuckets`, whose top edge is 12288.
 * A pathfinding pass cannot reach it. A batch can: nothing caps the
 * sub-request count except the one-megabyte request-size limit. An
 * over-ceiling batch lands in `+Inf` and stays countable as
 * `rpc_batch_size_count - rpc_batch_size_bucket{le="12288"}`, so the overflow
 * is visible rather than lost. No measured workload occupies that range, so
 * the ladder is not extended into it.
 *
 * @note Header-only constants with no runtime state, so there is nothing to
 * synchronize -- safe to include from any thread context.
 */

namespace xrpl::telemetry {

// ===== Metric names ==========================================================

/**
 * Distribution of the sub-request count in a batch JSON-RPC call.
 *
 * Recorded only for `method == "batch"`. A plain single request would
 * otherwise flood the histogram with the value 1 and bury the batch
 * distribution this instrument exists to show.
 */
inline constexpr char kRpcBatchSize[] = "rpc_batch_size";

/**
 * Distribution of the payment paths one pathfinding pass produced, summed
 * across every candidate source asset.
 */
inline constexpr char kPathfindDiscoveredPaths[] = "pathfind_discovered_paths";

// ===== Instrument descriptions ===============================================

/** @{ */
inline constexpr char kRpcBatchSizeDesc[] = "Sub-requests per batch JSON-RPC call";

inline constexpr char kPathfindDiscoveredPathsDesc[] =
    "Payment paths produced per pathfinding pass, across all source assets";
/** @} */

}  // namespace xrpl::telemetry

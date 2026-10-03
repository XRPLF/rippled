#pragma once

#ifdef XRPL_ENABLE_TELEMETRY

#include <opentelemetry/sdk/trace/sampler.h>

#include <memory>

namespace xrpl::telemetry {

/**
 * Build the head sampler that TelemetryImpl installs on its TracerProvider.
 *
 * The result is a ParentBased sampler. A span with no parent or a remote
 * parent is decided by one TraceIdRatioBasedSampler, so a peer's sampled flag
 * cannot turn our spans off or on. A span with a local parent follows it.
 *
 *   parent of the new span      delegate
 *   ----------------------      ------------------------
 *   none (root span)       -->  TraceIdRatioBased(ratio)
 *   remote, sampled        -->  TraceIdRatioBased(ratio)
 *   remote, not sampled    -->  TraceIdRatioBased(ratio)
 *   local, sampled         -->  AlwaysOn
 *   local, not sampled     -->  AlwaysOff
 *
 * The ratio sampler decides from the trace id alone, so nodes that use the
 * same ratio make the same choice for every trace.
 *
 * @note The sampler has no mutable state, so the SDK may call ShouldSample()
 * on many threads at once.
 *
 * @param ratio Fraction of trace ids to sample, from 0.0 (none) to 1.0 (all).
 * @return A new sampler that owns its delegates.
 */
[[nodiscard]] std::unique_ptr<opentelemetry::sdk::trace::Sampler>
makeHeadSampler(double ratio);

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

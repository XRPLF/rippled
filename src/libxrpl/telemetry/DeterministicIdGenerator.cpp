/**
 * Implementation of DeterministicIdGenerator and PendingTraceId.
 *
 * The pending trace_id lives in file-local thread_locals shared between the
 * generator and the RAII guard. GenerateTraceId() consumes the pending id on
 * the SDK's no-parent branch; PendingTraceId sets it and asserts consumption.
 * All OpenTelemetry SDK types stay confined to telemetry translation units.
 *
 * @see DeterministicIdGenerator, PendingTraceId (DeterministicIdGenerator.h)
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/DeterministicIdGenerator.h>

#include <xrpl/beast/utility/instrumentation.h>

#include <opentelemetry/nostd/span.h>
#include <opentelemetry/sdk/trace/id_generator.h>
#include <opentelemetry/sdk/trace/random_id_generator_factory.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/trace_id.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace xrpl::telemetry {

namespace {

/**
 * Trace_id pinned by PendingTraceId, consumed by GenerateTraceId(). File-local
 * and thread-local, so it is set and read on the same thread with no locking.
 */
thread_local std::optional<std::array<std::uint8_t, 16>> gTlsPendingTraceId;

/**
 * True once GenerateTraceId() has consumed the pending id. ~PendingTraceId
 * asserts on it to catch a forced-root span that never reached the SDK root
 * branch.
 */
thread_local bool gTlsPendingConsumed = false;

/**
 * Process-wide count of deterministic trace_ids dropped without being consumed.
 * Bumped in ~PendingTraceId when the pending id was never taken by a root span.
 * Atomic because it is summed across all threads; relaxed ordering is enough
 * for a diagnostic tally. Exposed via unconsumedDeterministicIdDrops().
 */
std::atomic<std::uint64_t> gUnconsumedDeterministicIdDrops{0};

}  // namespace

DeterministicIdGenerator::DeterministicIdGenerator()
    : opentelemetry::sdk::trace::IdGenerator(/*is_random=*/false)
    , random_(opentelemetry::sdk::trace::RandomIdGeneratorFactory::Create())
{
}

opentelemetry::trace::TraceId
DeterministicIdGenerator::GenerateTraceId() noexcept
{
    if (gTlsPendingTraceId)
    {
        auto const id = *gTlsPendingTraceId;
        gTlsPendingTraceId.reset();
        gTlsPendingConsumed = true;
        return opentelemetry::trace::TraceId(
            opentelemetry::nostd::span<std::uint8_t const, 16>(id.data(), 16));
    }
    return random_->GenerateTraceId();
}

opentelemetry::trace::SpanId
DeterministicIdGenerator::GenerateSpanId() noexcept
{
    return random_->GenerateSpanId();  // ALWAYS random — never uses the pending id
}

PendingTraceId::PendingTraceId(std::array<std::uint8_t, 16> const& id) noexcept
{
    gTlsPendingTraceId = id;
    gTlsPendingConsumed = false;
}

PendingTraceId::~PendingTraceId() noexcept
{
    // The forced no-parent (root) span-start MUST have consumed the pending id
    // via GenerateTraceId(). If not, the SDK took a different branch than the
    // caller forced — a bug — so fail loudly in debug/test builds.
    XRPL_ASSERT(
        gTlsPendingConsumed,
        "xrpl::telemetry::PendingTraceId : deterministic trace_id was not consumed");
    if (!gTlsPendingConsumed)
    {
        // The assert above is stripped in release, so bump a counter too: a
        // process-wide tally of dropped deterministic trace roots. It is
        // exposed via unconsumedDeterministicIdDrops() as an extension point
        // for a future metric or diagnostic to read; nothing wires it to a log
        // or metric yet. Relaxed: this is a plain diagnostic tally.
        gUnconsumedDeterministicIdDrops.fetch_add(1, std::memory_order_relaxed);
    }
    gTlsPendingTraceId.reset();  // never leak, even in release
    gTlsPendingConsumed = false;
}

std::uint64_t
unconsumedDeterministicIdDrops() noexcept
{
    return gUnconsumedDeterministicIdDrops.load(std::memory_order_relaxed);
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

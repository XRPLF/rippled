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
#include <opentelemetry/sdk/trace/random_id_generator_factory.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/trace_id.h>

#include <array>
#include <cstdint>
#include <optional>

namespace xrpl::telemetry {

namespace {

/**
 * Trace_id pinned by PendingTraceId, consumed by GenerateTraceId(). File-local
 * and thread-local, so it is set and read on the same thread with no locking.
 */
thread_local std::optional<std::array<std::uint8_t, 16>> tlsPendingTraceId;

/**
 * True once GenerateTraceId() has consumed the pending id. ~PendingTraceId
 * asserts on it to catch a forced-root span that never reached the SDK root
 * branch.
 */
thread_local bool tlsPendingConsumed = false;

}  // namespace

DeterministicIdGenerator::DeterministicIdGenerator()
    : opentelemetry::sdk::trace::IdGenerator(/*is_random=*/false)
    , random_(opentelemetry::sdk::trace::RandomIdGeneratorFactory::Create())
{
}

opentelemetry::trace::TraceId
DeterministicIdGenerator::GenerateTraceId() noexcept
{
    if (tlsPendingTraceId)
    {
        auto const id = *tlsPendingTraceId;
        tlsPendingTraceId.reset();
        tlsPendingConsumed = true;
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
    tlsPendingTraceId = id;
    tlsPendingConsumed = false;
}

PendingTraceId::~PendingTraceId() noexcept
{
    // The forced no-parent (root) span-start MUST have consumed the pending id
    // via GenerateTraceId(). If not, the SDK took a different branch than the
    // caller forced — a bug — so fail loudly in debug/test builds.
    XRPL_ASSERT(
        tlsPendingConsumed,
        "xrpl::telemetry::PendingTraceId : deterministic trace_id was not consumed");
    tlsPendingTraceId.reset();  // never leak, even in release
    tlsPendingConsumed = false;
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

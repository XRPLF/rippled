#pragma once

#ifdef XRPL_ENABLE_TELEMETRY

#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/recordable.h>
#include <opentelemetry/trace/span_context.h>

#include <chrono>
#include <memory>

namespace xrpl::telemetry {

/**
 * Span processor that sits in front of the batch processor. It drops
 * discarded spans, and it makes every span export each attribute key once.
 *
 * The OTLP recordable appends every SetAttribute() call without looking up
 * the key. Without this processor a key set twice is exported twice, and a
 * reader that takes the first value misses the update. So the processor
 * wraps each recordable it hands to the SDK. The wrapper keeps the last value
 * for each key and writes each key once, in first-set order, when the span
 * ends. Every other call goes straight to the delegate's recordable.
 *
 * Dependency diagram:
 *
 *   +------------------------+  delegate_   +--------------------+
 *   | FilteringSpanProcessor |------------->| BatchSpanProcessor |
 *   +------------------------+              +--------------------+
 *        |  reads                                 |  makes
 *        v                                        v
 *   DiscardScope (DiscardFlag.h)            OtlpRecordable
 *
 * Data flow for one span:
 *
 *   MakeRecordable() --> wrapper around the delegate's recordable
 *   SetAttribute()   --> wrapper keeps an owned copy, last value wins
 *   other calls      --> forwarded to the delegate's recordable at once
 *   OnEnd()          --> discarded? drop the span
 *                    --> else write each kept key once, then pass the
 *                        delegate's recordable to the delegate
 *
 * The discard check reads a thread-local flag, not the span's attributes,
 * because the recordable type depends on the exporter and has no common
 * getter. The flag works because Span::End() calls OnEnd() on its own thread.
 *
 * @code
 * // Production: wrap the batch processor before building the provider.
 * auto processor = std::make_unique<FilteringSpanProcessor>(std::move(batchProcessor));
 *
 * // A key set twice is exported once, as "accepted".
 * auto span = processor->MakeRecordable();
 * span->SetAttribute("consensus_phase", "open");
 * span->SetAttribute("consensus_phase", "accepted");
 * processor->OnEnd(std::move(span));
 *
 * // Edge case: a span ended inside a DiscardScope never reaches the delegate.
 * {
 *     DiscardScope discardScope;
 *     processor->OnEnd(processor->MakeRecordable());
 * }
 * @endcode
 *
 * @note Thread safety: OnStart() and OnEnd() may run on many threads at once.
 * Each wrapper belongs to one span, and the SDK span serializes calls to it.
 * The discard flag is thread-local.
 * @note Limitations: every distinct key is kept until the span ends, so memory
 * grows with the number of distinct keys, and the delegate's attribute count
 * limit applies when the keys are written. Attributes inside one event or link
 * are forwarded as given. An attribute a delegate sets on the recordable in its
 * own OnStart() skips the merge.
 */
class FilteringSpanProcessor final : public opentelemetry::sdk::trace::SpanProcessor
{
    /**
     * Receives every span that is not discarded. The batch processor in
     * production.
     */
    std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor> delegate_;

public:
    /**
     * @param delegate Processor that receives every span that is not discarded.
     */
    explicit FilteringSpanProcessor(
        std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor> delegate);

    /**
     * Asks the delegate for a recordable and wraps it.
     *
     * @return the wrapper, or nullptr when the delegate made none. The SDK then
     * records nothing for the span.
     */
    std::unique_ptr<opentelemetry::sdk::trace::Recordable>
    MakeRecordable() noexcept override;

    /**
     * Tells the delegate a span started, passing the delegate's own recordable.
     *
     * @param span Recordable from MakeRecordable(). Any other recordable is
     * passed on as it is.
     * @param parentContext Context of the span's parent.
     */
    void
    OnStart(
        opentelemetry::sdk::trace::Recordable& span,
        opentelemetry::trace::SpanContext const& parentContext) noexcept override;

    /**
     * Drops the span if it was discarded. Otherwise writes each kept attribute
     * once and passes the delegate's own recordable to the delegate.
     *
     * @param span Recordable from MakeRecordable(). Any other recordable is
     * passed on as it is.
     */
    void
    OnEnd(std::unique_ptr<opentelemetry::sdk::trace::Recordable>&& span) noexcept override;

    /**
     * @param timeout Longest time to wait for the delegate.
     * @return the delegate's result.
     */
    bool
    ForceFlush(
        std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept override;

    /**
     * @param timeout Longest time to wait for the delegate.
     * @return the delegate's result.
     */
    bool
    Shutdown(
        std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept override;
};

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

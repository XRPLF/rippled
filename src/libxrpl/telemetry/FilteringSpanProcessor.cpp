/**
 * Implementation of FilteringSpanProcessor.
 *
 * The processor gives the SDK a UniqueAttributeRecordable in place of the
 * delegate's recordable. The wrapper keeps one owned value per attribute key
 * and writes the keys once, in first-set order, when the span ends.
 *
 * @see FilteringSpanProcessor (FilteringSpanProcessor.h)
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/FilteringSpanProcessor.h>

#include <xrpl/telemetry/DiscardFlag.h>

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/common/key_value_iterable.h>
#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/common/attribute_utils.h>
#include <opentelemetry/sdk/instrumentationscope/instrumentation_scope.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/recordable.h>
#include <opentelemetry/sdk/trace/span_limits.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/trace_flags.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::telemetry {

namespace {

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;

/**
 * Writes one owned attribute value to a recordable, as the view type the SDK
 * takes. The view lasts only for the call, and the recordable copies it.
 *
 * @code
 * opentelemetry::nostd::visit(ForwardOwnedValue{recordable, key}, ownedValue);
 * @endcode
 *
 * @note The key and the owned value must outlive the call.
 */
struct ForwardOwnedValue
{
    /**
     * Recordable that receives the attribute.
     */
    trace_sdk::Recordable& target;

    /**
     * Attribute key, pointing into the caller's owned copy.
     */
    opentelemetry::nostd::string_view key;

    template <typename T>
        requires std::is_arithmetic_v<T>
    void
    operator()(T value) const noexcept
    {
        target.SetAttribute(key, value);
    }

    void
    operator()(std::string const& value) const noexcept
    {
        target.SetAttribute(key, opentelemetry::nostd::string_view{value.data(), value.size()});
    }

    template <typename T>
    void
    operator()(std::vector<T> const& values) const noexcept
    {
        target.SetAttribute(key, opentelemetry::nostd::span<T const>{values.data(), values.size()});
    }

    // std::vector<bool> packs its bits and has no data(), so copy to plain bools.
    void
    operator()(std::vector<bool> const& values) const noexcept
    {
        auto const flags = std::make_unique<bool[]>(values.size());
        std::ranges::copy(values, flags.get());
        target.SetAttribute(
            key, opentelemetry::nostd::span<bool const>{flags.get(), values.size()});
    }

    // A string array is sent as views into the owned strings.
    void
    operator()(std::vector<std::string> const& values) const noexcept
    {
        std::vector<opentelemetry::nostd::string_view> views;
        views.reserve(values.size());
        for (auto const& value : values)
        {
            views.emplace_back(value.data(), value.size());
        }
        target.SetAttribute(
            key,
            opentelemetry::nostd::span<opentelemetry::nostd::string_view const>{
                views.data(), views.size()});
    }
};

/**
 * Recordable the processor gives the SDK in place of the delegate's.
 *
 * SetAttribute() keeps an owned copy of each value and replaces any earlier
 * value for the same key. The first set fixes the key's position. Every other
 * call goes to the delegate's recordable at once.
 *
 *   SDK span --SetAttribute()--> attributes_ (one entry per key)
 *            --other calls-----> inner_ (the delegate's recordable)
 *   flushAndRelease(): attributes_ --> inner_, then inner_ is handed back
 *
 * @code
 * UniqueAttributeRecordable wrapper{delegate.MakeRecordable()};
 * wrapper.SetAttribute("txq_status", "rejected");
 * wrapper.SetAttribute("txq_status", "queued");
 * delegate.OnEnd(wrapper.flushAndRelease());  // one txq_status, "queued"
 * @endcode
 *
 * @note Not thread-safe. The SDK span serializes calls to its recordable.
 * @note operator SpanData*() keeps its default of nullptr, because the
 * delegate's recordable does not hold the kept attributes until the span ends.
 */
class UniqueAttributeRecordable final : public trace_sdk::Recordable
{
    /**
     * One kept attribute: an owned key and an owned copy of its last value.
     */
    using Attribute = std::pair<std::string, opentelemetry::sdk::common::OwnedAttributeValue>;

    /**
     * The delegate's recordable. Null only after flushAndRelease().
     */
    std::unique_ptr<trace_sdk::Recordable> inner_;

    /**
     * Kept attributes, in the order each key was first set.
     */
    std::vector<Attribute> attributes_;

public:
    /**
     * @param inner The delegate's recordable. Must not be null.
     */
    explicit UniqueAttributeRecordable(std::unique_ptr<trace_sdk::Recordable> inner) noexcept
        : inner_(std::move(inner))
    {
    }

    /**
     * @return the delegate's recordable.
     */
    [[nodiscard]] trace_sdk::Recordable&
    inner() const noexcept
    {
        return *inner_;
    }

    /**
     * Writes each kept attribute to the delegate's recordable once, in
     * first-set order. The delegate's recordable applies its own limits here.
     *
     * @return the delegate's recordable. Do not use this wrapper afterwards.
     */
    [[nodiscard]] std::unique_ptr<trace_sdk::Recordable>
    flushAndRelease() noexcept
    {
        for (auto const& [key, value] : attributes_)
        {
            opentelemetry::nostd::visit(
                ForwardOwnedValue{*inner_, {key.data(), key.size()}}, value);
        }
        return std::move(inner_);
    }

    void
    SetAttribute(
        opentelemetry::nostd::string_view key,
        opentelemetry::common::AttributeValue const& value) noexcept override
    {
        auto owned = opentelemetry::sdk::common::VisitVariant(
            opentelemetry::sdk::common::AttributeConverter{}, value);
        if (!owned.second)
        {
            return;
        }
        std::string_view const name{key.data(), key.size()};
        auto const found = std::ranges::find_if(
            attributes_, [name](Attribute const& attribute) { return attribute.first == name; });
        if (found != attributes_.end())
        {
            found->second = std::move(owned.first);
        }
        else
        {
            attributes_.emplace_back(std::string{name}, std::move(owned.first));
        }
    }

    void
    SetIdentity(trace_api::SpanContext const& spanContext, trace_api::SpanId parentSpanId) noexcept
        override
    {
        inner_->SetIdentity(spanContext, parentSpanId);
    }

    void
    AddEvent(
        opentelemetry::nostd::string_view name,
        opentelemetry::common::SystemTimestamp timestamp,
        opentelemetry::common::KeyValueIterable const& attributes) noexcept override
    {
        inner_->AddEvent(name, timestamp, attributes);
    }

    void
    AddLink(
        trace_api::SpanContext const& spanContext,
        opentelemetry::common::KeyValueIterable const& attributes) noexcept override
    {
        inner_->AddLink(spanContext, attributes);
    }

    void
    SetStatus(trace_api::StatusCode code, opentelemetry::nostd::string_view description) noexcept
        override
    {
        inner_->SetStatus(code, description);
    }

    void
    SetName(opentelemetry::nostd::string_view name) noexcept override
    {
        inner_->SetName(name);
    }

    void
    SetTraceFlags(trace_api::TraceFlags flags) noexcept override
    {
        inner_->SetTraceFlags(flags);
    }

    void
    SetSpanKind(trace_api::SpanKind spanKind) noexcept override
    {
        inner_->SetSpanKind(spanKind);
    }

    void
    SetResource(opentelemetry::sdk::resource::Resource const& resource) noexcept override
    {
        inner_->SetResource(resource);
    }

    void
    SetStartTime(opentelemetry::common::SystemTimestamp startTime) noexcept override
    {
        inner_->SetStartTime(startTime);
    }

    void
    SetDuration(std::chrono::nanoseconds duration) noexcept override
    {
        inner_->SetDuration(duration);
    }

    void
    SetSpanLimits(trace_sdk::SpanLimits const& limits) noexcept override
    {
        inner_->SetSpanLimits(limits);
    }

    void
    SetInstrumentationScope(
        opentelemetry::sdk::instrumentationscope::InstrumentationScope const& scope) noexcept
        override
    {
        inner_->SetInstrumentationScope(scope);
    }
};

}  // namespace

FilteringSpanProcessor::FilteringSpanProcessor(std::unique_ptr<trace_sdk::SpanProcessor> delegate)
    : delegate_(std::move(delegate))
{
}

std::unique_ptr<trace_sdk::Recordable>
FilteringSpanProcessor::MakeRecordable() noexcept
{
    auto inner = delegate_->MakeRecordable();
    if (inner == nullptr)
    {
        return nullptr;
    }
    return std::make_unique<UniqueAttributeRecordable>(std::move(inner));
}

void
FilteringSpanProcessor::OnStart(
    trace_sdk::Recordable& span,
    trace_api::SpanContext const& parentContext) noexcept
{
    auto* const wrapper = dynamic_cast<UniqueAttributeRecordable*>(&span);
    delegate_->OnStart(wrapper != nullptr ? wrapper->inner() : span, parentContext);
}

void
FilteringSpanProcessor::OnEnd(std::unique_ptr<trace_sdk::Recordable>&& span) noexcept
{
    if (DiscardScope::isActive())
    {
        // SpanGuard::discard() is inside a DiscardScope on this thread,
        // which it entered just before calling Span::End() — and End()
        // invokes OnEnd() synchronously. Drop the span.
        return;
    }
    auto* const wrapper = dynamic_cast<UniqueAttributeRecordable*>(span.get());
    if (wrapper == nullptr)
    {
        delegate_->OnEnd(std::move(span));
        return;
    }
    delegate_->OnEnd(wrapper->flushAndRelease());
}

bool
FilteringSpanProcessor::ForceFlush(std::chrono::microseconds timeout) noexcept
{
    return delegate_->ForceFlush(timeout);
}

bool
FilteringSpanProcessor::Shutdown(std::chrono::microseconds timeout) noexcept
{
    return delegate_->Shutdown(timeout);
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

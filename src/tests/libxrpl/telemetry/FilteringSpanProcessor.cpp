// Tests for FilteringSpanProcessor.
//
// The OTLP recordable appends every SetAttribute() call. Without the
// processor a key set twice is exported twice, and a reader that takes the
// first value misses the update. These tests put the processor in front of a
// fake delegate whose recordable logs every call it receives. They check
// that each key arrives once, with its last value, in first-set order; that
// values are copied when set; that every other call is forwarded at once and
// unchanged; that the delegate gets its own recordable back; and that
// discarded spans are still dropped. One test uses the real OTLP recordable
// to show its attribute count limit still applies.
//
// The whole file is telemetry-only: without XRPL_ENABLE_TELEMETRY the SDK
// headers are unavailable, so the translation unit compiles empty.

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/FilteringSpanProcessor.h>

#include <xrpl/telemetry/DiscardFlag.h>

#include <gtest/gtest.h>
#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/common/key_value_iterable.h>
#include <opentelemetry/common/key_value_iterable_view.h>
#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/exporters/otlp/otlp_recordable.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/instrumentationscope/instrumentation_scope.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/recordable.h>
#include <opentelemetry/sdk/trace/samplers/always_on_factory.h>
#include <opentelemetry/sdk/trace/span_limits.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>
#include <opentelemetry/trace/tracer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::telemetry {
namespace {

namespace otel_common = opentelemetry::common;
namespace otel_nostd = opentelemetry::nostd;
namespace otel_trace = opentelemetry::trace;
namespace otel_sdk_trace = opentelemetry::sdk::trace;

/**
 * @return the text of an SDK string view.
 */
std::string
toString(otel_nostd::string_view text)
{
    return {text.data(), text.size()};
}

/**
 * @return a span id as 16 lowercase hex digits.
 */
std::string
toHex(otel_trace::SpanId const& id)
{
    std::array<char, 2 * otel_trace::SpanId::kSize> text{};
    id.ToLowerBase16(text);
    return {text.data(), text.size()};
}

/**
 * @return a span id holding these bytes.
 */
otel_trace::SpanId
spanId(std::array<std::uint8_t, otel_trace::SpanId::kSize> const& bytes)
{
    return otel_trace::SpanId{otel_nostd::span<std::uint8_t const, otel_trace::SpanId::kSize>{
        bytes.data(), bytes.size()}};
}

/**
 * @return a trace id holding these bytes.
 */
otel_trace::TraceId
traceId(std::array<std::uint8_t, otel_trace::TraceId::kSize> const& bytes)
{
    return otel_trace::TraceId{otel_nostd::span<std::uint8_t const, otel_trace::TraceId::kSize>{
        bytes.data(), bytes.size()}};
}

/**
 * Renders an attribute value with its type, such as `int64:7` or
 * `string:"open"`, so one string comparison checks both. Array types the
 * tests never set render as `unrendered`.
 */
struct RenderValue
{
    std::string
    operator()(bool value) const
    {
        return std::format("bool:{}", value);
    }

    std::string
    operator()(std::int32_t value) const
    {
        return std::format("int32:{}", value);
    }

    std::string
    operator()(std::uint32_t value) const
    {
        return std::format("uint32:{}", value);
    }

    std::string
    operator()(std::int64_t value) const
    {
        return std::format("int64:{}", value);
    }

    std::string
    operator()(std::uint64_t value) const
    {
        return std::format("uint64:{}", value);
    }

    std::string
    operator()(double value) const
    {
        return std::format("double:{}", value);
    }

    std::string
    operator()(char const* value) const
    {
        return std::format("string:\"{}\"", value);
    }

    std::string
    operator()(otel_nostd::string_view value) const
    {
        return std::format("string:\"{}\"", toString(value));
    }

    std::string
    operator()(otel_nostd::span<bool const> values) const
    {
        std::string text;
        for (bool const value : values)
        {
            text += std::format("{}{}", text.empty() ? "" : ",", value);
        }
        return std::format("bool[]:[{}]", text);
    }

    std::string
    operator()(otel_nostd::span<std::int64_t const> values) const
    {
        std::string text;
        for (std::int64_t const value : values)
        {
            text += std::format("{}{}", text.empty() ? "" : ",", value);
        }
        return std::format("int64[]:[{}]", text);
    }

    std::string
    operator()(otel_nostd::span<otel_nostd::string_view const> values) const
    {
        std::string text;
        for (auto const& value : values)
        {
            text += std::format("{}\"{}\"", text.empty() ? "" : ",", toString(value));
        }
        return std::format("string[]:[{}]", text);
    }

    template <typename T, std::size_t N>
    std::string
    operator()(otel_nostd::span<T, N> const&) const
    {
        return "unrendered";
    }
};

/**
 * @return the attributes of an event or link as `key=value` pairs.
 */
std::string
render(otel_common::KeyValueIterable const& attributes)
{
    std::string text;
    attributes.ForEachKeyValue(
        [&text](otel_nostd::string_view key, otel_common::AttributeValue value) noexcept {
            text += std::format(
                "{}{}={}",
                text.empty() ? "" : ",",
                toString(key),
                otel_nostd::visit(RenderValue{}, value));
            return true;
        });
    return text;
}

/**
 * Recordable that stands in for the exporter's. It logs every call it
 * receives, one line per call, so a test can compare the whole sequence.
 *
 * @note Test-only. Not thread-safe; each test drives it from one thread.
 */
class RecordingRecordable final : public otel_sdk_trace::Recordable
{
public:
    /**
     * One line per call, in the order received.
     */
    std::vector<std::string> calls;

    /**
     * The last resource passed in, to check the same object arrives.
     */
    opentelemetry::sdk::resource::Resource const* resource = nullptr;

    /**
     * The last scope passed in, to check the same object arrives.
     */
    opentelemetry::sdk::instrumentationscope::InstrumentationScope const* scope = nullptr;

    void
    SetIdentity(
        otel_trace::SpanContext const& spanContext,
        otel_trace::SpanId parentSpanId) noexcept override
    {
        calls.push_back(
            std::format(
                "SetIdentity {} parent {}", toHex(spanContext.span_id()), toHex(parentSpanId)));
    }

    void
    SetAttribute(otel_nostd::string_view key, otel_common::AttributeValue const& value) noexcept
        override
    {
        calls.push_back(
            std::format(
                "SetAttribute {}={}", toString(key), otel_nostd::visit(RenderValue{}, value)));
    }

    void
    AddEvent(
        otel_nostd::string_view name,
        otel_common::SystemTimestamp timestamp,
        otel_common::KeyValueIterable const& attributes) noexcept override
    {
        calls.push_back(
            std::format(
                "AddEvent {} at {} {{{}}}",
                toString(name),
                timestamp.time_since_epoch().count(),
                render(attributes)));
    }

    void
    AddLink(
        otel_trace::SpanContext const& spanContext,
        otel_common::KeyValueIterable const& attributes) noexcept override
    {
        calls.push_back(
            std::format("AddLink {} {{{}}}", toHex(spanContext.span_id()), render(attributes)));
    }

    void
    SetStatus(otel_trace::StatusCode code, otel_nostd::string_view description) noexcept override
    {
        calls.push_back(
            std::format("SetStatus {} {}", std::to_underlying(code), toString(description)));
    }

    void
    SetName(otel_nostd::string_view name) noexcept override
    {
        calls.push_back(std::format("SetName {}", toString(name)));
    }

    void
    SetTraceFlags(otel_trace::TraceFlags flags) noexcept override
    {
        calls.push_back(std::format("SetTraceFlags {}", static_cast<unsigned>(flags.flags())));
    }

    void
    SetSpanKind(otel_trace::SpanKind kind) noexcept override
    {
        calls.push_back(std::format("SetSpanKind {}", std::to_underlying(kind)));
    }

    void
    SetResource(opentelemetry::sdk::resource::Resource const& value) noexcept override
    {
        resource = &value;
        calls.emplace_back("SetResource");
    }

    void
    SetStartTime(otel_common::SystemTimestamp startTime) noexcept override
    {
        calls.push_back(std::format("SetStartTime {}", startTime.time_since_epoch().count()));
    }

    void
    SetDuration(std::chrono::nanoseconds duration) noexcept override
    {
        calls.push_back(std::format("SetDuration {}", duration.count()));
    }

    void
    SetSpanLimits(otel_sdk_trace::SpanLimits const& limits) noexcept override
    {
        calls.push_back(std::format("SetSpanLimits {}", limits.attribute_count_limit));
    }

    void
    SetInstrumentationScope(
        opentelemetry::sdk::instrumentationscope::InstrumentationScope const& value) noexcept
        override
    {
        scope = &value;
        calls.emplace_back("SetInstrumentationScope");
    }
};

/**
 * @return the calls a recordable received, or one line saying it is not a
 * RecordingRecordable, so a wrong type shows up in the assertion message.
 */
std::vector<std::string>
callsOf(otel_sdk_trace::Recordable const* recordable)
{
    auto const* recording = dynamic_cast<RecordingRecordable const*>(recordable);
    if (recording == nullptr)
    {
        return {"not a RecordingRecordable"};
    }
    return recording->calls;
}

/**
 * @return only the SetAttribute lines a recordable received.
 */
std::vector<std::string>
attributeCallsOf(otel_sdk_trace::Recordable const* recordable)
{
    auto calls = callsOf(recordable);
    std::erase_if(
        calls, [](std::string const& call) { return !call.starts_with("SetAttribute "); });
    return calls;
}

/**
 * What the fake delegate saw. Kept outside the delegate so a test can read it
 * after the processor that owns the delegate is gone.
 */
struct DelegateLog
{
    /**
     * Every recordable the delegate made, in order. Compared by address only.
     */
    std::vector<otel_sdk_trace::Recordable const*> made;

    /**
     * Every recordable passed to OnStart(), in order. Compared by address only.
     */
    std::vector<otel_sdk_trace::Recordable const*> started;

    /**
     * Every recordable passed to OnEnd(), kept alive for inspection.
     */
    std::vector<std::unique_ptr<otel_sdk_trace::Recordable>> ended;
};

/**
 * Span processor that stands in for the batch processor. It makes recordables
 * with a factory and keeps every span it is handed in a DelegateLog.
 *
 *   FilteringSpanProcessor
 *        |  delegate_
 *        v
 *   RecordingProcessor --make_--> RecordingRecordable (logs each call)
 *        |
 *        +--> DelegateLog: made, started, ended
 *
 * @note Test-only. The log must outlive the processor.
 */
class RecordingProcessor final : public otel_sdk_trace::SpanProcessor
{
public:
    /**
     * Makes the recordable the delegate returns from MakeRecordable().
     */
    using Factory = std::function<std::unique_ptr<otel_sdk_trace::Recordable>()>;

    /**
     * @param log Where to record what the delegate sees.
     * @param make Factory for the delegate's recordables.
     */
    RecordingProcessor(DelegateLog& log, Factory make) : log_(log), make_(std::move(make))
    {
    }

    std::unique_ptr<otel_sdk_trace::Recordable>
    MakeRecordable() noexcept override
    {
        auto recordable = make_();
        log_.made.push_back(recordable.get());
        return recordable;
    }

    void
    OnStart(otel_sdk_trace::Recordable& span, otel_trace::SpanContext const&) noexcept override
    {
        log_.started.push_back(&span);
    }

    void
    OnEnd(std::unique_ptr<otel_sdk_trace::Recordable>&& span) noexcept override
    {
        log_.ended.push_back(std::move(span));
    }

    bool
    ForceFlush(std::chrono::microseconds) noexcept override
    {
        return true;
    }

    bool
    Shutdown(std::chrono::microseconds) noexcept override
    {
        return true;
    }

private:
    DelegateLog& log_;
    Factory make_;
};

/**
 * @return a RecordingRecordable, as the delegate's recordable.
 */
std::unique_ptr<otel_sdk_trace::Recordable>
makeRecording()
{
    return std::make_unique<RecordingRecordable>();
}

/**
 * @return a recording delegate that writes to @p log.
 */
std::unique_ptr<RecordingProcessor>
recordingDelegate(DelegateLog& log, RecordingProcessor::Factory make = makeRecording)
{
    return std::make_unique<RecordingProcessor>(log, std::move(make));
}

TEST(FilteringSpanProcessor, repeated_key_is_exported_once_with_its_last_value)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetAttribute("consensus_phase", "open");
    span->SetAttribute("consensus_phase", "establish");
    span->SetAttribute("consensus_phase", "accepted");
    processor.OnEnd(std::move(span));

    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(
        callsOf(log.ended.front().get()),
        (std::vector<std::string>{R"(SetAttribute consensus_phase=string:"accepted")"}));
}

TEST(FilteringSpanProcessor, distinct_keys_are_exported_once_each_in_first_set_order)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};

    // Not alphabetical, and the first key is set again last: a sorted
    // container or a move-to-back on overwrite both give another order.
    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetAttribute("zeta", std::int64_t{1});
    span->SetAttribute("alpha", std::int64_t{2});
    span->SetAttribute("mid", std::int64_t{3});
    span->SetAttribute("zeta", std::int64_t{4});
    processor.OnEnd(std::move(span));

    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(
        callsOf(log.ended.front().get()),
        (std::vector<std::string>{
            "SetAttribute zeta=int64:4",
            "SetAttribute alpha=int64:2",
            "SetAttribute mid=int64:3"}));
}

TEST(FilteringSpanProcessor, string_value_is_copied_when_set)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};
    std::string status{"accepted"};

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetAttribute("tx_status", otel_nostd::string_view{status.data(), status.size()});
    std::ranges::fill(status, 'x');
    processor.OnEnd(std::move(span));

    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(
        callsOf(log.ended.front().get()),
        (std::vector<std::string>{R"(SetAttribute tx_status=string:"accepted")"}));
}

TEST(FilteringSpanProcessor, key_is_copied_when_set)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};
    std::string key{"tx_type"};

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetAttribute(otel_nostd::string_view{key.data(), key.size()}, true);
    std::ranges::fill(key, 'x');
    processor.OnEnd(std::move(span));

    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(
        callsOf(log.ended.front().get()),
        (std::vector<std::string>{"SetAttribute tx_type=bool:true"}));
}

TEST(FilteringSpanProcessor, scalar_values_keep_their_type)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetAttribute("flag", true);
    span->SetAttribute("count32", std::int32_t{-5});
    span->SetAttribute("ucount32", std::uint32_t{5});
    span->SetAttribute("count64", std::int64_t{-7});
    span->SetAttribute("ucount64", std::uint64_t{7});
    span->SetAttribute("ratio", 1.5);
    processor.OnEnd(std::move(span));

    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(
        callsOf(log.ended.front().get()),
        (std::vector<std::string>{
            "SetAttribute flag=bool:true",
            "SetAttribute count32=int32:-5",
            "SetAttribute ucount32=uint32:5",
            "SetAttribute count64=int64:-7",
            "SetAttribute ucount64=uint64:7",
            "SetAttribute ratio=double:1.5"}));
}

TEST(FilteringSpanProcessor, array_values_are_copied_and_exported_intact)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};
    std::array<bool, 3> flags{true, false, true};
    std::array<std::int64_t, 2> counts{3, 4};
    std::string first{"a"};
    std::string second{"b"};
    std::array<otel_nostd::string_view, 2> const names{
        otel_nostd::string_view{first}, otel_nostd::string_view{second}};

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetAttribute("flags", otel_nostd::span<bool const>{flags.data(), flags.size()});
    span->SetAttribute(
        "counts", otel_nostd::span<std::int64_t const>{counts.data(), counts.size()});
    span->SetAttribute(
        "names", otel_nostd::span<otel_nostd::string_view const>{names.data(), names.size()});
    flags.fill(false);
    counts.fill(0);
    std::ranges::fill(first, 'x');
    std::ranges::fill(second, 'y');
    processor.OnEnd(std::move(span));

    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(
        callsOf(log.ended.front().get()),
        (std::vector<std::string>{
            "SetAttribute flags=bool[]:[true,false,true]",
            "SetAttribute counts=int64[]:[3,4]",
            R"(SetAttribute names=string[]:["a","b"])"}));
}

TEST(FilteringSpanProcessor, other_calls_are_forwarded_at_once_and_unchanged)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};
    otel_trace::SpanContext const context{
        traceId({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}),
        spanId({1, 2, 3, 4, 5, 6, 7, 8}),
        otel_trace::TraceFlags{otel_trace::TraceFlags::kIsSampled},
        false};
    otel_trace::SpanContext const linked{
        traceId({16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1}),
        spanId({9, 9, 9, 9, 9, 9, 9, 9}),
        otel_trace::TraceFlags{otel_trace::TraceFlags::kIsSampled},
        true};
    otel_sdk_trace::SpanLimits limits;
    limits.attribute_count_limit = 7;
    auto const resource = opentelemetry::sdk::resource::Resource::Create({});
    auto const scope =
        opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("scope");
    std::map<std::string, std::int64_t> const eventAttributes{{"ledger_seq", 9}};

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetSpanLimits(limits);
    span->SetName("tx.process");
    span->SetInstrumentationScope(*scope);
    span->SetIdentity(context, spanId({8, 7, 6, 5, 4, 3, 2, 1}));
    span->SetTraceFlags(otel_trace::TraceFlags{otel_trace::TraceFlags::kIsSampled});
    span->SetSpanKind(otel_trace::SpanKind::kServer);
    span->SetStartTime(otel_common::SystemTimestamp{std::chrono::nanoseconds{100}});
    span->SetResource(resource);
    span->AddEvent(
        "ledger.closed",
        otel_common::SystemTimestamp{std::chrono::nanoseconds{150}},
        otel_common::KeyValueIterableView<std::map<std::string, std::int64_t>>{eventAttributes});
    span->AddLink(linked);
    span->SetStatus(otel_trace::StatusCode::kError, "boom");
    span->SetDuration(std::chrono::nanoseconds{250});

    std::vector<std::string> const expected{
        "SetSpanLimits 7",
        "SetName tx.process",
        "SetInstrumentationScope",
        "SetIdentity 0102030405060708 parent 0807060504030201",
        "SetTraceFlags 1",
        "SetSpanKind 1",
        "SetStartTime 100",
        "SetResource",
        "AddEvent ledger.closed at 150 {ledger_seq=int64:9}",
        "AddLink 0909090909090909 {}",
        "SetStatus 2 boom",
        "SetDuration 250"};

    // Every call has reached the delegate's recordable before the span ends.
    ASSERT_EQ(log.made.size(), 1U);
    EXPECT_EQ(callsOf(log.made.front()), expected);
    auto const* recording = dynamic_cast<RecordingRecordable const*>(log.made.front());
    ASSERT_NE(recording, nullptr);
    EXPECT_EQ(recording->resource, &resource);
    EXPECT_EQ(recording->scope, scope.get());

    // Ending the span adds nothing when no attribute was set.
    processor.OnEnd(std::move(span));
    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(callsOf(log.ended.front().get()), expected);
}

TEST(FilteringSpanProcessor, span_ended_inside_discard_scope_is_dropped)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};

    auto discarded = processor.MakeRecordable();
    ASSERT_NE(discarded, nullptr);
    discarded->SetAttribute("tx_status", "rejected");
    {
        DiscardScope const discardScope;
        processor.OnEnd(std::move(discarded));
    }
    EXPECT_TRUE(log.ended.empty());

    // Control: the same processor still exports a span ended outside the scope.
    auto kept = processor.MakeRecordable();
    ASSERT_NE(kept, nullptr);
    processor.OnEnd(std::move(kept));
    EXPECT_EQ(log.ended.size(), 1U);
}

TEST(FilteringSpanProcessor, delegate_gets_its_own_recordable_at_start_and_end)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    ASSERT_EQ(log.made.size(), 1U);
    otel_sdk_trace::Recordable const* const inner = log.made.front();
    EXPECT_NE(span.get(), inner);

    processor.OnStart(*span, otel_trace::SpanContext::GetInvalid());
    processor.OnEnd(std::move(span));

    EXPECT_EQ(log.started, (std::vector<otel_sdk_trace::Recordable const*>{inner}));
    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(log.ended.front().get(), inner);
}

TEST(FilteringSpanProcessor, recordable_it_did_not_make_is_passed_on_unchanged)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log)};
    auto foreign = std::make_unique<RecordingRecordable>();
    otel_sdk_trace::Recordable const* const address = foreign.get();

    processor.OnStart(*foreign, otel_trace::SpanContext::GetInvalid());
    processor.OnEnd(std::move(foreign));

    EXPECT_EQ(log.started, (std::vector<otel_sdk_trace::Recordable const*>{address}));
    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(log.ended.front().get(), address);
}

TEST(FilteringSpanProcessor, null_recordable_from_the_delegate_stays_null)
{
    DelegateLog log;
    FilteringSpanProcessor processor{
        recordingDelegate(log, [] { return std::unique_ptr<otel_sdk_trace::Recordable>{}; })};

    EXPECT_EQ(processor.MakeRecordable(), nullptr);
    EXPECT_EQ(log.made.size(), 1U);
}

TEST(FilteringSpanProcessor, otlp_recordable_gets_each_key_once_within_its_count_limit)
{
    DelegateLog log;
    FilteringSpanProcessor processor{recordingDelegate(log, [] {
        return std::unique_ptr<otel_sdk_trace::Recordable>{
            std::make_unique<opentelemetry::exporter::otlp::OtlpRecordable>()};
    })};
    otel_sdk_trace::SpanLimits limits;
    limits.attribute_count_limit = 2;

    auto span = processor.MakeRecordable();
    ASSERT_NE(span, nullptr);
    span->SetSpanLimits(limits);
    span->SetAttribute("first", std::int64_t{1});
    span->SetAttribute("second", std::int64_t{2});
    span->SetAttribute("first", std::int64_t{3});
    span->SetAttribute("third", std::int64_t{4});
    processor.OnEnd(std::move(span));

    // Two keys fit the limit. "first" keeps its place and its last value, and
    // only "third" is dropped.
    ASSERT_EQ(log.ended.size(), 1U);
    auto const* otlp =
        dynamic_cast<opentelemetry::exporter::otlp::OtlpRecordable const*>(log.ended.front().get());
    ASSERT_NE(otlp, nullptr);
    auto const& exported = otlp->span();
    ASSERT_EQ(exported.attributes_size(), 2);
    EXPECT_EQ(exported.attributes(0).key(), "first");
    EXPECT_EQ(exported.attributes(0).value().int_value(), 3);
    EXPECT_EQ(exported.attributes(1).key(), "second");
    EXPECT_EQ(exported.attributes(1).value().int_value(), 2);
    EXPECT_EQ(exported.dropped_attributes_count(), 1U);
}

TEST(FilteringSpanProcessor, sdk_span_that_sets_a_key_twice_exports_it_once)
{
    DelegateLog log;
    auto const provider = otel_sdk_trace::TracerProviderFactory::Create(
        std::make_unique<FilteringSpanProcessor>(recordingDelegate(log)),
        opentelemetry::sdk::resource::Resource::Create({}),
        otel_sdk_trace::AlwaysOnSamplerFactory::Create());
    auto const tracer = provider->GetTracer("filtering-span-processor-test");

    auto const span = tracer->StartSpan("txq.enqueue");
    span->SetAttribute("txq_status", "rejected");
    span->SetAttribute("txq_status", "queued");
    span->End();

    ASSERT_EQ(log.made.size(), 1U);
    EXPECT_EQ(log.started, log.made);
    ASSERT_EQ(log.ended.size(), 1U);
    EXPECT_EQ(log.ended.front().get(), log.made.front());
    EXPECT_EQ(
        attributeCallsOf(log.ended.front().get()),
        (std::vector<std::string>{R"(SetAttribute txq_status=string:"queued")"}));
}

}  // namespace
}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

// Tests for makeHeadSampler(), the head sampler TelemetryImpl installs on its
// TracerProvider.
//
// The rule table in HeadSampler.h has five rows, one per delegate slot. Every
// row runs at ratio 1.0 and at ratio 0.0. At those two ratios the ratio
// sampler, AlwaysOn and AlwaysOff give three different pairs of answers, so a
// wrong delegate in any slot fails at least one case.
//
// The whole file is telemetry-only: when XRPL_ENABLE_TELEMETRY is not defined
// HeadSampler.h declares nothing and the OpenTelemetry SDK headers are
// unavailable, so the translation unit compiles empty.

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/HeadSampler.h>

#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>
#include <opentelemetry/common/key_value_iterable.h>
#include <opentelemetry/sdk/trace/sampler.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_context_kv_iterable.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>

#include <array>
#include <cstdint>
#include <string>

namespace xrpl::telemetry {
namespace {

namespace otel_sdk_trace = opentelemetry::sdk::trace;
namespace otel_trace = opentelemetry::trace;

/**
 * Trace id of every parent and of every new span asked about. Non-zero, so a
 * context that carries it is valid. The elements are const so the array
 * converts to the span of const bytes that TraceId's constructor takes.
 */
constexpr std::array<std::uint8_t const, otel_trace::TraceId::kSize> kHeadSamplerTraceId{
    0x4b,
    0xf9,
    0x2f,
    0x35,
    0x77,
    0xb3,
    0x4d,
    0xa6,
    0xa3,
    0xce,
    0x92,
    0x9d,
    0x0e,
    0x0e,
    0x47,
    0x36};

/**
 * Span id of every parent. Non-zero, so a context that carries it is valid.
 */
constexpr std::array<std::uint8_t const, otel_trace::SpanId::kSize>
    kHeadSamplerParentSpanId{0x00, 0xf0, 0x67, 0xaa, 0x0b, 0xa9, 0x02, 0xb7};

/**
 * Parent of a new span: one value per row of the rule table in HeadSampler.h.
 */
enum class ParentKind {
    /**
     * No parent, so the new span is a root.
     */
    None,

    /**
     * Received from a peer, sampled flag set.
     */
    RemoteSampled,

    /**
     * Received from a peer, sampled flag clear.
     */
    RemoteNotSampled,

    /**
     * Started in this process, sampled flag set.
     */
    LocalSampled,

    /**
     * Started in this process, sampled flag clear.
     */
    LocalNotSampled,
};

/**
 * @param kind Parent kind of a case.
 * @return True when the parent came from a peer.
 */
constexpr bool
isRemoteParent(ParentKind kind)
{
    return kind == ParentKind::RemoteSampled || kind == ParentKind::RemoteNotSampled;
}

/**
 * @param kind Parent kind of a case.
 * @return True when the parent's sampled flag is set.
 */
constexpr bool
isSampledParent(ParentKind kind)
{
    return kind == ParentKind::RemoteSampled || kind == ParentKind::LocalSampled;
}

/**
 * Decision that keeps and exports the span.
 */
constexpr auto kSampleDecision = otel_sdk_trace::Decision::RECORD_AND_SAMPLE;

/**
 * Decision that drops the span.
 */
constexpr auto kDropDecision = otel_sdk_trace::Decision::DROP;

/**
 * One row of the rule table at one ratio.
 */
struct HeadSamplerCase
{
    /**
     * Parent of the new span.
     */
    ParentKind parent;

    /**
     * Ratio passed to makeHeadSampler(), from 0.0 to 1.0.
     */
    double ratio;

    /**
     * Decision the sampler must return.
     */
    otel_sdk_trace::Decision expected;
};

/**
 * Every row of the rule table in HeadSampler.h, at ratio 1.0 and at 0.0.
 */
constexpr auto kHeadSamplerCases = std::to_array<HeadSamplerCase>({
    {.parent = ParentKind::None, .ratio = 1.0, .expected = kSampleDecision},
    {.parent = ParentKind::None, .ratio = 0.0, .expected = kDropDecision},
    {.parent = ParentKind::RemoteSampled, .ratio = 1.0, .expected = kSampleDecision},
    {.parent = ParentKind::RemoteSampled, .ratio = 0.0, .expected = kDropDecision},
    {.parent = ParentKind::RemoteNotSampled, .ratio = 1.0, .expected = kSampleDecision},
    {.parent = ParentKind::RemoteNotSampled, .ratio = 0.0, .expected = kDropDecision},
    {.parent = ParentKind::LocalSampled, .ratio = 1.0, .expected = kSampleDecision},
    {.parent = ParentKind::LocalSampled, .ratio = 0.0, .expected = kSampleDecision},
    {.parent = ParentKind::LocalNotSampled, .ratio = 1.0, .expected = kDropDecision},
    {.parent = ParentKind::LocalNotSampled, .ratio = 0.0, .expected = kDropDecision},
});

/**
 * Build the parent span context for a case.
 *
 * @param kind Parent kind of the case.
 * @return An invalid context for ParentKind::None, else a valid context with
 * the shared trace id and parent span id.
 */
otel_trace::SpanContext
makeHeadSamplerParent(ParentKind kind)
{
    if (kind == ParentKind::None)
    {
        return otel_trace::SpanContext::GetInvalid();
    }
    auto const flags = isSampledParent(kind)
        ? otel_trace::TraceFlags{otel_trace::TraceFlags::kIsSampled}
        : otel_trace::TraceFlags{};
    return otel_trace::SpanContext{
        otel_trace::TraceId{kHeadSamplerTraceId},
        otel_trace::SpanId{kHeadSamplerParentSpanId},
        flags,
        isRemoteParent(kind)};
}

/**
 * Check that a parent context has the shape a case relies on.
 *
 * @param parent Context built by makeHeadSamplerParent().
 * @param kind Parent kind the case asked for.
 * @return Success, or a failure naming the first property that differs.
 */
::testing::AssertionResult
hasParentShape(otel_trace::SpanContext const& parent, ParentKind kind)
{
    if (parent.IsValid() != (kind != ParentKind::None))
    {
        return ::testing::AssertionFailure() << "parent IsValid() is " << parent.IsValid();
    }
    if (parent.IsRemote() != isRemoteParent(kind))
    {
        return ::testing::AssertionFailure() << "parent IsRemote() is " << parent.IsRemote();
    }
    if (parent.IsSampled() != isSampledParent(kind))
    {
        return ::testing::AssertionFailure() << "parent IsSampled() is " << parent.IsSampled();
    }
    return ::testing::AssertionSuccess();
}

/**
 * Name a case for failure messages.
 *
 * @param testCase The case to name.
 * @return Text such as "remote not sampled parent, ratio 0".
 */
std::string
describeHeadSamplerCase(HeadSamplerCase const& testCase)
{
    ::testing::Message message;
    if (testCase.parent == ParentKind::None)
    {
        message << "no parent";
    }
    else
    {
        message << (isRemoteParent(testCase.parent) ? "remote" : "local")
                << (isSampledParent(testCase.parent) ? " sampled" : " not sampled") << " parent";
    }
    message << ", ratio " << testCase.ratio;
    return message.GetString();
}

/**
 * Ask a new head sampler to decide on one new span.
 *
 * @param ratio Ratio passed to makeHeadSampler(), from 0.0 to 1.0.
 * @param parent Parent of the new span. An invalid context means the new
 * span is a root.
 * @return The decision the sampler returned.
 */
otel_sdk_trace::Decision
headSamplerDecision(double ratio, otel_trace::SpanContext const& parent)
{
    auto const sampler = makeHeadSampler(ratio);
    opentelemetry::common::NoopKeyValueIterable const attributes{};
    otel_trace::NullSpanContext const links{};
    return sampler
        ->ShouldSample(
            parent,
            otel_trace::TraceId{kHeadSamplerTraceId},
            "head_sampler_test",
            otel_trace::SpanKind::kInternal,
            attributes,
            links)
        .decision;
}

// Each parent kind at each ratio gets the decision its rule-table row gives.
TEST(HeadSampler, every_parent_kind_follows_rule_table)
{
    for (auto const& testCase : kHeadSamplerCases)
    {
        SCOPED_TRACE(describeHeadSamplerCase(testCase));
        auto const parent = makeHeadSamplerParent(testCase.parent);
        ASSERT_TRUE(hasParentShape(parent, testCase.parent));

        EXPECT_EQ(headSamplerDecision(testCase.ratio, parent), testCase.expected);
    }
}

// The shipped ratio keeps a span whose remote parent has its sampled flag
// clear.
TEST(HeadSampler, shipped_ratio_samples_remote_unsampled_parent)
{
    auto const parent = makeHeadSamplerParent(ParentKind::RemoteNotSampled);
    ASSERT_TRUE(hasParentShape(parent, ParentKind::RemoteNotSampled));

    EXPECT_EQ(headSamplerDecision(Telemetry::Setup::samplingRatio, parent), kSampleDecision);
}

}  // namespace
}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

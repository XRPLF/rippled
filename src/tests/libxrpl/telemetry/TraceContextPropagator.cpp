#include <gtest/gtest.h>

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/TraceContextPropagator.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/default_span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>

#include <cstring>

namespace trace = opentelemetry::trace;

TEST(TraceContextPropagator, round_trip)
{
    std::uint8_t traceIdBuf[16] = {
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0a,
        0x0b,
        0x0c,
        0x0d,
        0x0e,
        0x0f,
        0x10};
    std::uint8_t spanIdBuf[8] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22};

    trace::TraceId traceId(opentelemetry::nostd::span<uint8_t const, 16>(traceIdBuf, 16));
    trace::SpanId spanId(opentelemetry::nostd::span<uint8_t const, 8>(spanIdBuf, 8));
    trace::TraceFlags flags(trace::TraceFlags::kIsSampled);
    trace::SpanContext spanCtx(traceId, spanId, flags, true);

    auto ctx = opentelemetry::context::Context{}.SetValue(
        trace::kSpanKey,
        opentelemetry::nostd::shared_ptr<trace::Span>(new trace::DefaultSpan(spanCtx)));

    protocol::TraceContext proto;
    xrpl::telemetry::injectToProtobuf(ctx, proto);

    EXPECT_TRUE(proto.has_trace_id());
    EXPECT_EQ(proto.trace_id().size(), 16u);
    EXPECT_TRUE(proto.has_span_id());
    EXPECT_EQ(proto.span_id().size(), 8u);
    EXPECT_EQ(proto.trace_flags(), static_cast<uint32_t>(trace::TraceFlags::kIsSampled));
    EXPECT_EQ(std::memcmp(proto.trace_id().data(), traceIdBuf, 16), 0);
    EXPECT_EQ(std::memcmp(proto.span_id().data(), spanIdBuf, 8), 0);

    auto extractedCtx = xrpl::telemetry::extractFromProtobuf(proto);
    auto extractedSpan = trace::GetSpan(extractedCtx);
    ASSERT_NE(extractedSpan, nullptr);

    auto const& extracted = extractedSpan->GetContext();
    EXPECT_TRUE(extracted.IsValid());
    EXPECT_TRUE(extracted.IsRemote());
    EXPECT_EQ(extracted.trace_id(), traceId);
    EXPECT_EQ(extracted.span_id(), spanId);
    EXPECT_TRUE(extracted.trace_flags().IsSampled());
}

TEST(TraceContextPropagator, extract_empty_protobuf)
{
    protocol::TraceContext proto;
    auto ctx = xrpl::telemetry::extractFromProtobuf(proto);
    auto span = trace::GetSpan(ctx);
    if (span)
    {
        EXPECT_FALSE(span->GetContext().IsValid());
    }
}

TEST(TraceContextPropagator, extract_wrong_size_trace_id)
{
    protocol::TraceContext proto;
    proto.set_trace_id(std::string(8, '\x01'));
    proto.set_span_id(std::string(8, '\xaa'));

    auto ctx = xrpl::telemetry::extractFromProtobuf(proto);
    auto span = trace::GetSpan(ctx);
    if (span)
    {
        EXPECT_FALSE(span->GetContext().IsValid());
    }
}

TEST(TraceContextPropagator, extract_wrong_size_span_id)
{
    protocol::TraceContext proto;
    proto.set_trace_id(std::string(16, '\x01'));
    proto.set_span_id(std::string(4, '\xaa'));

    auto ctx = xrpl::telemetry::extractFromProtobuf(proto);
    auto span = trace::GetSpan(ctx);
    if (span)
    {
        EXPECT_FALSE(span->GetContext().IsValid());
    }
}

TEST(TraceContextPropagator, inject_invalid_span)
{
    auto ctx = opentelemetry::context::Context{};
    protocol::TraceContext proto;
    xrpl::telemetry::injectToProtobuf(ctx, proto);

    EXPECT_FALSE(proto.has_trace_id());
    EXPECT_FALSE(proto.has_span_id());
}

TEST(TraceContextPropagator, flags_preservation)
{
    std::uint8_t traceIdBuf[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    std::uint8_t spanIdBuf[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    // Test with flags NOT sampled (flags = 0)
    trace::TraceFlags flags(0);
    trace::SpanContext spanCtx(
        trace::TraceId(opentelemetry::nostd::span<uint8_t const, 16>(traceIdBuf, 16)),
        trace::SpanId(opentelemetry::nostd::span<uint8_t const, 8>(spanIdBuf, 8)),
        flags,
        true);

    auto ctx = opentelemetry::context::Context{}.SetValue(
        trace::kSpanKey,
        opentelemetry::nostd::shared_ptr<trace::Span>(new trace::DefaultSpan(spanCtx)));

    protocol::TraceContext proto;
    xrpl::telemetry::injectToProtobuf(ctx, proto);
    EXPECT_EQ(proto.trace_flags(), 0u);

    auto extracted = xrpl::telemetry::extractFromProtobuf(proto);
    auto span = trace::GetSpan(extracted);
    ASSERT_NE(span, nullptr);
    EXPECT_FALSE(span->GetContext().trace_flags().IsSampled());
}

#else  // XRPL_ENABLE_TELEMETRY not defined

TEST(TraceContextPropagator, compiles_without_telemetry)
{
    SUCCEED();
}

#endif  // XRPL_ENABLE_TELEMETRY

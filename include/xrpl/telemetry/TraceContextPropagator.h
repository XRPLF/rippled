#pragma once

/** Utilities for trace context propagation across nodes.

    Provides serialization/deserialization of OTel trace context to/from
    Protocol Buffer TraceContext messages (P2P cross-node propagation).

    Only compiled when XRPL_ENABLE_TELEMETRY is defined.

    TODO: These utilities are not yet wired into the P2P message flow.
    To enable cross-node distributed traces, call injectToProtobuf() in
    PeerImp when sending TMTransaction/TMProposeSet messages, and call
    extractFromProtobuf() in the corresponding message handlers to
    reconstruct the parent span context before starting a child span.
    This was deferred to validate single-node tracing performance first.
*/

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/proto/xrpl.pb.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/default_span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>

#include <cstdint>

namespace xrpl {
namespace telemetry {

/** Extract OTel context from a protobuf TraceContext message.

    @param proto  The protobuf TraceContext received from a peer.
    @return An OTel Context with the extracted parent span, or an empty
            context if the protobuf fields are missing or invalid.
*/
inline opentelemetry::context::Context
extractFromProtobuf(protocol::TraceContext const& proto)
{
    namespace trace = opentelemetry::trace;

    if (!proto.has_trace_id() || proto.trace_id().size() != 16 || !proto.has_span_id() ||
        proto.span_id().size() != 8)
    {
        return opentelemetry::context::Context{};
    }

    auto const* rawTraceId = reinterpret_cast<std::uint8_t const*>(proto.trace_id().data());
    auto const* rawSpanId = reinterpret_cast<std::uint8_t const*>(proto.span_id().data());
    trace::TraceId traceId(opentelemetry::nostd::span<std::uint8_t const, 16>(rawTraceId, 16));
    trace::SpanId spanId(opentelemetry::nostd::span<std::uint8_t const, 8>(rawSpanId, 8));
    // Default to not-sampled (0x00) per W3C Trace Context spec when
    // the trace_flags field is absent.
    trace::TraceFlags flags(
        proto.has_trace_flags() ? static_cast<std::uint8_t>(proto.trace_flags())
                                : static_cast<std::uint8_t>(0));

    trace::SpanContext spanCtx(traceId, spanId, flags, /* remote = */ true);

    return opentelemetry::context::Context{}.SetValue(
        trace::kSpanKey,
        opentelemetry::nostd::shared_ptr<trace::Span>(new trace::DefaultSpan(spanCtx)));
}

/** Inject the current span's trace context into a protobuf TraceContext.

    @param ctx    The OTel context containing the span to propagate.
    @param proto  The protobuf TraceContext to populate.
*/
inline void
injectToProtobuf(opentelemetry::context::Context const& ctx, protocol::TraceContext& proto)
{
    namespace trace = opentelemetry::trace;

    auto span = trace::GetSpan(ctx);
    if (!span)
        return;

    auto const& spanCtx = span->GetContext();
    if (!spanCtx.IsValid())
        return;

    // Serialize trace_id (16 bytes)
    auto const& traceId = spanCtx.trace_id();
    proto.set_trace_id(traceId.Id().data(), trace::TraceId::kSize);

    // Serialize span_id (8 bytes)
    auto const& spanId = spanCtx.span_id();
    proto.set_span_id(spanId.Id().data(), trace::SpanId::kSize);

    // Serialize flags
    proto.set_trace_flags(spanCtx.trace_flags().flags());
}

}  // namespace telemetry
}  // namespace xrpl

#endif  // XRPL_ENABLE_TELEMETRY

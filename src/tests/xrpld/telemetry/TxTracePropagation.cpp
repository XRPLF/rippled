// Tests that a relayed transaction's trace context crosses from the sending
// node to the receiving node the way production carries it:
//  - the sender starts a tx.process span (txProcessSpan) and writes its context
//    into the TMTransaction it relays (injectSpanContext), as NetworkOPs does;
//  - the message is serialized, parsed and its trace context cleaned
//    (sanitizeTraceContext), as the overlay does with a peer message;
//  - the receiver starts its tx.receive span from that message (txReceiveSpan),
//    as PeerImp does.
// The ended spans are read back from an in-memory exporter. One process plays
// both nodes, but the sender's guard is not scoped, so it is never the ambient
// span: the message is the only link between the two spans.
//
// The whole file is telemetry-only: when XRPL_ENABLE_TELEMETRY is not defined
// the helpers return null guards and the OpenTelemetry SDK headers are
// unavailable, so the translation unit compiles empty.

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpld/telemetry/PropagationHelpers.h>
#include <xrpld/telemetry/TxSpanNames.h>
#include <xrpld/telemetry/TxTracing.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/proto/xrpl.pb.h>
#include <xrpl/telemetry/DeterministicIdGenerator.h>
#include <xrpl/telemetry/HeadSampler.h>
#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/Telemetry.h>
#include <xrpl/telemetry/TraceContextValidation.h>

#include <gtest/gtest.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/memory/in_memory_span_data.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter_factory.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/trace_id.h>
#include <opentelemetry/trace/tracer.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::telemetry {
namespace {

using SpanData = opentelemetry::sdk::trace::SpanData;
using ExportedSpans = std::vector<std::unique_ptr<SpanData>>;

/**
 * Telemetry backend that keeps every ended span in memory.
 *
 * @code
 *   SpanGuard factories
 *         |  Telemetry::getInstance()
 *         v
 *   RecordingTelemetry --> TracerProvider
 *                            |  head sampler: makeHeadSampler()
 *                            |  ids: DeterministicIdGenerator
 *                            v
 *                          SimpleSpanProcessor --> InMemorySpanData
 * @endcode
 *
 * It uses the production head sampler and id generator, so sampling and
 * hash-derived trace ids behave as they do on a node. Every trace category
 * is on.
 *
 * @code
 *   RecordingTelemetry telemetry;
 *   Telemetry::setInstance(&telemetry);
 *   {
 *       auto const span = txProcessSpan(txId);
 *   }  // the span ends here and is exported
 *   auto const spans = telemetry.takeEndedSpans();
 *   Telemetry::setInstance(nullptr);
 * @endcode
 *
 * @note Test-only. Clear the global instance before this object is
 * destroyed. Spans may end on any thread, but takeEndedSpans() empties the
 * shared buffer, so call it only after the spans a test reads have ended.
 */
class RecordingTelemetry : public Telemetry
{
public:
    /**
     * Build the export pipeline and keep the exporter's span buffer.
     */
    RecordingTelemetry()
    {
        // The factory fills spanData_ with the exporter's buffer.
        auto exporter =
            opentelemetry::exporter::memory::InMemorySpanExporterFactory::Create(spanData_);
        provider_ = opentelemetry::sdk::trace::TracerProviderFactory::Create(
            opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter)),
            opentelemetry::sdk::resource::Resource::Create({}),
            makeHeadSampler(Setup::samplingRatio),
            std::make_unique<DeterministicIdGenerator>());
    }

    /**
     * Read and remove the spans that ended since the last call.
     * @return Those spans; the order is not part of the contract.
     */
    [[nodiscard]] ExportedSpans
    takeEndedSpans()
    {
        return spanData_->GetSpans();
    }

    void
    start() override
    {
    }
    void
    stop() override
    {
    }

    [[nodiscard]] bool
    isEnabled() const override
    {
        return true;
    }
    [[nodiscard]] bool
    shouldTraceTransactions() const override
    {
        return true;
    }
    [[nodiscard]] bool
    shouldTraceConsensus() const override
    {
        return true;
    }
    [[nodiscard]] bool
    shouldTraceRpc() const override
    {
        return true;
    }
    [[nodiscard]] bool
    shouldTracePeer() const override
    {
        return true;
    }
    [[nodiscard]] bool
    shouldTraceLedger() const override
    {
        return true;
    }

    /**
     * @return A fixed strategy. Nothing these tests call reads it.
     */
    [[nodiscard]] ConsensusTraceStrategy
    getConsensusTraceStrategy() const override
    {
        return ConsensusTraceStrategy::Deterministic;
    }

    /**
     * @return A meter from a noop provider. These tests record no metrics.
     */
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    getMeter(std::string_view name) override
    {
        return noopMeter(name);
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    getTracer(std::string_view name) override
    {
        return provider_->GetTracer(std::string(name));
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(std::string_view name, opentelemetry::trace::SpanKind kind) override
    {
        opentelemetry::trace::StartSpanOptions opts;
        opts.kind = kind;
        return getTracer(kTracerName)->StartSpan(std::string(name), opts);
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(
        std::string_view name,
        opentelemetry::context::Context const& parentContext,
        opentelemetry::trace::SpanKind kind) override
    {
        opentelemetry::trace::StartSpanOptions opts;
        opts.kind = kind;
        opts.parent = parentContext;
        return getTracer(kTracerName)->StartSpan(std::string(name), opts);
    }

private:
    /**
     * Buffer the exporter writes each ended span to.
     */
    std::shared_ptr<opentelemetry::exporter::memory::InMemorySpanData> spanData_;

    /**
     * Provider that owns the export pipeline.
     */
    std::unique_ptr<opentelemetry::sdk::trace::TracerProvider> provider_;
};

/**
 * Find an exported span by name.
 * @param spans Spans read back from the exporter.
 * @param name  Full span name, such as tx.receive.
 * @return The first span with that name, or nullptr if none ended.
 */
SpanData const*
findSpan(ExportedSpans const& spans, std::string_view name)
{
    auto const it = std::ranges::find_if(spans, [name](auto const& span) {
        auto const spanName = span->GetName();
        return std::string_view(spanName.data(), spanName.size()) == name;
    });
    return it == spans.end() ? nullptr : it->get();
}

/**
 * @return A fixed transaction id with no zero byte.
 */
uint256
fixedTxId()
{
    return uint256{"0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20"};
}

/**
 * The trace id that TxTracing.h documents for a transaction's spans.
 * @param txId Transaction id.
 * @return The first 16 bytes of txId, as a trace id.
 */
opentelemetry::trace::TraceId
traceIdOf(uint256 const& txId)
{
    return opentelemetry::trace::TraceId(
        opentelemetry::nostd::span<std::uint8_t const, opentelemetry::trace::TraceId::kSize>(
            txId.data(), opentelemetry::trace::TraceId::kSize));
}

/**
 * Build a relay message with the fields the protocol requires. The
 * transaction bytes are a placeholder: nothing in these tests decodes them.
 * @return A message with no trace context.
 */
protocol::TMTransaction
makeRelayMessage()
{
    protocol::TMTransaction msg;
    msg.set_rawtransaction("placeholder");
    msg.set_status(protocol::tsCURRENT);
    return msg;
}

/**
 * Carry a message to the receiving node: serialize it, parse the bytes into
 * a new message, then clean its trace context the way the overlay does after
 * it parses a peer message. A parse failure fails the calling test.
 * @param sent The message as the sending node relays it.
 * @return The message the receiving node's handler sees.
 */
protocol::TMTransaction
relayOverWire(protocol::TMTransaction const& sent)
{
    protocol::TMTransaction received;
    EXPECT_TRUE(received.ParseFromString(sent.SerializeAsString()));
    sanitizeTraceContext(received);
    return received;
}

/**
 * Installs a RecordingTelemetry as the global instance for each test and
 * clears it afterwards, so the global never points at a destroyed object.
 */
class TxTracePropagationTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        Telemetry::setInstance(&telemetry_);
    }

    void
    TearDown() override
    {
        Telemetry::setInstance(nullptr);
    }

    /**
     * Backend the span factories use for the duration of one test.
     */
    RecordingTelemetry telemetry_;
};

TEST_F(TxTracePropagationTest, relayed_receive_span_is_child_of_sender_process_span)
{
    auto const txId = fixedTxId();
    auto sent = makeRelayMessage();
    {
        auto const processSpan = txProcessSpan(txId);
        ASSERT_TRUE(processSpan);
        injectSpanContext(processSpan, sent);
        ASSERT_TRUE(sent.has_trace_context());

        auto const received = relayOverWire(sent);
        auto const receiveSpan = txReceiveSpan(txId, received);
        ASSERT_TRUE(receiveSpan);
    }

    auto const spans = telemetry_.takeEndedSpans();
    auto const* processData = findSpan(spans, tx_span::process);
    auto const* receiveData = findSpan(spans, tx_span::receive);
    ASSERT_NE(processData, nullptr);
    ASSERT_NE(receiveData, nullptr);
    ASSERT_EQ(spans.size(), 2u);

    // The sender's span is a true root, in the trace named by the tx id.
    EXPECT_EQ(processData->GetParentSpanId(), opentelemetry::trace::SpanId{});
    EXPECT_EQ(processData->GetTraceId(), traceIdOf(txId));

    // The receiver's span is its child, in the same trace.
    EXPECT_EQ(receiveData->GetParentSpanId(), processData->GetSpanId());
    EXPECT_EQ(receiveData->GetTraceId(), processData->GetTraceId());
}

TEST_F(TxTracePropagationTest, peer_span_in_another_trace_gives_root_receive_span)
{
    auto const txId = fixedTxId();
    // A peer whose trace id for this transaction is not the hash-derived one.
    std::string const peerTraceId(kTraceIdSize, '\x5A');
    auto sent = makeRelayMessage();
    {
        auto const processSpan = txProcessSpan(txId);
        ASSERT_TRUE(processSpan);
        injectSpanContext(processSpan, sent);
        ASSERT_TRUE(sent.has_trace_context());
        sent.mutable_trace_context()->set_trace_id(peerTraceId);

        // The parsed message still carries the peer's whole context. Only the
        // receive side decides how to use it.
        auto const received = relayOverWire(sent);
        ASSERT_EQ(received.trace_context().trace_id(), peerTraceId);
        ASSERT_TRUE(isValidSpanId(received.trace_context().span_id()));
        auto const receiveSpan = txReceiveSpan(txId, received);
        ASSERT_TRUE(receiveSpan);
    }

    auto const spans = telemetry_.takeEndedSpans();
    auto const* processData = findSpan(spans, tx_span::process);
    auto const* receiveData = findSpan(spans, tx_span::receive);
    ASSERT_NE(processData, nullptr);
    ASSERT_NE(receiveData, nullptr);
    ASSERT_EQ(spans.size(), 2u);

    // A parent and its child share one trace. The peer's span lies in another
    // trace. It cannot be the parent. The receive span is a root in the trace
    // named by the tx id.
    EXPECT_EQ(receiveData->GetParentSpanId(), opentelemetry::trace::SpanId{});
    EXPECT_EQ(receiveData->GetTraceId(), traceIdOf(txId));
}

TEST_F(TxTracePropagationTest, peer_that_did_not_sample_does_not_drop_receive_span)
{
    auto const txId = fixedTxId();
    auto sent = makeRelayMessage();
    {
        auto const processSpan = txProcessSpan(txId);
        ASSERT_TRUE(processSpan);
        injectSpanContext(processSpan, sent);
        ASSERT_TRUE(sent.has_trace_context());
        // A peer whose head sampler did not sample this trace still sends its
        // span context, with the sampled bit clear.
        sent.mutable_trace_context()->set_trace_flags(0);

        auto const received = relayOverWire(sent);
        ASSERT_EQ(received.trace_context().trace_flags(), 0u);
        auto const receiveSpan = txReceiveSpan(txId, received);
        ASSERT_TRUE(receiveSpan);
    }

    // The parent is remote, so this node's own sampler decides, and the
    // peer's flag does not drop the span.
    auto const spans = telemetry_.takeEndedSpans();
    auto const* processData = findSpan(spans, tx_span::process);
    auto const* receiveData = findSpan(spans, tx_span::receive);
    ASSERT_NE(processData, nullptr);
    ASSERT_NE(receiveData, nullptr);
    ASSERT_EQ(spans.size(), 2u);

    EXPECT_EQ(receiveData->GetParentSpanId(), processData->GetSpanId());
    EXPECT_EQ(receiveData->GetTraceId(), traceIdOf(txId));
}

TEST_F(TxTracePropagationTest, message_without_trace_context_gives_root_receive_span)
{
    auto const txId = fixedTxId();
    // A sender with nothing recorded leaves the message without trace context.
    auto sent = makeRelayMessage();
    injectSpanContext(SpanGuard{}, sent);
    ASSERT_FALSE(sent.has_trace_context());
    {
        // An unrelated span is active on the receiving thread. The receive
        // span must not take it as its parent.
        ScopedSpanGuard const ambientSpan(TraceCategory::Peer, "test", "ambient");
        ASSERT_TRUE(ambientSpan);

        auto const received = relayOverWire(sent);
        ASSERT_FALSE(received.has_trace_context());
        auto const receiveSpan = txReceiveSpan(txId, received);
        ASSERT_TRUE(receiveSpan);
    }

    auto const spans = telemetry_.takeEndedSpans();
    auto const* receiveData = findSpan(spans, tx_span::receive);
    ASSERT_NE(receiveData, nullptr);
    ASSERT_EQ(spans.size(), 2u);

    // No parent at all, and the trace named by the tx id.
    EXPECT_EQ(receiveData->GetParentSpanId(), opentelemetry::trace::SpanId{});
    EXPECT_EQ(receiveData->GetTraceId(), traceIdOf(txId));
}

}  // namespace
}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

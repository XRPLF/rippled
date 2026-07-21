// Tests for SpanGuard, ScopedSpanGuard and DeterministicIdGenerator.
//
// These verify the span-guard split and the deterministic-root fix at the
// trace level using an in-memory span exporter:
//  - SpanGuard is unscoped and thread-free: it owns only the span, never the
//    OTel thread-local context stack, so it may be moved to and ended on any
//    thread. SpanGuard::freshRoot() starts a brand-new trace root, ignoring
//    the ambient active span.
//  - ScopedSpanGuard is scoped and thread-bound: it also pushes an OTel Scope
//    so the span is the ambient parent on the constructing thread. Its
//    `operator SpanGuard() &&` pops that Scope eagerly on the origin thread and
//    yields a thread-free SpanGuard; destroying it on another thread trips an
//    owner-thread assertion.
//  - DeterministicIdGenerator (installed by the test TracerProvider) mints a
//    caller-pinned trace_id for a forced-root span. PendingTraceId pins the id
//    for one root span; an ambient child under a live parent never adopts it.
//
// The whole file is telemetry-only: when XRPL_ENABLE_TELEMETRY is not defined
// SpanGuard is a no-op stub and the OpenTelemetry SDK headers are unavailable,
// so the translation unit compiles empty.

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/DeterministicIdGenerator.h>
#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/exporters/memory/in_memory_span_data.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter_factory.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/samplers/always_on_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/trace_id.h>
#include <opentelemetry/trace/tracer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::telemetry {
namespace {

namespace otel_sdk_trace = opentelemetry::sdk::trace;
namespace otel_memory = opentelemetry::exporter::memory;

/**
 * In-memory Telemetry backing for SpanGuard scope tests.
 *
 * Reports every trace category as enabled and creates spans through an SDK
 * TracerProvider whose SimpleSpanProcessor forwards ended spans to an
 * InMemorySpanExporter, so a test can read the exact exported SpanData
 * (trace id, span id, parent id, name). The provider is built with a
 * DeterministicIdGenerator so PendingTraceId can pin the trace_id of a
 * forced-root span.
 *
 * Inheritance:
 *
 *     +-----------+
 *     | Telemetry |  (abstract interface)
 *     +-----+-----+
 *           |
 *     +-----+---------+
 *     | TestTelemetry |  in-memory exporter pipeline
 *     +---------------+
 *
 * @note Test-only. Install with Telemetry::setInstance() and clear it in
 * teardown. The exporter buffer is drained by InMemorySpanData::GetSpans(),
 * so each GetSpans() call returns only spans exported since the previous one.
 */
class TestTelemetry : public Telemetry
{
public:
    /**
     * Build the SDK export pipeline and keep the exporter's span buffer.
     */
    TestTelemetry()
    {
        // Factory populates spanData_ with the exporter's shared buffer.
        auto exporter = otel_memory::InMemorySpanExporterFactory::Create(spanData_);
        auto processor = otel_sdk_trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
        // Install the DeterministicIdGenerator (4-arg overload) so a
        // PendingTraceId can pin a forced-root span's trace_id in tests.
        provider_ = otel_sdk_trace::TracerProviderFactory::Create(
            std::move(processor),
            opentelemetry::sdk::resource::Resource::Create({}),
            otel_sdk_trace::AlwaysOnSamplerFactory::Create(),
            std::make_unique<DeterministicIdGenerator>());
    }

    /**
     * @return The exporter's span buffer (drained by GetSpans()).
     */
    [[nodiscard]] std::shared_ptr<otel_memory::InMemorySpanData>
    spanData() const
    {
        return spanData_;
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
     * @return A fixed strategy label; the scope tests do not exercise
     * deterministic trace-id correlation, so any stable value works.
     */
    [[nodiscard]] std::string const&
    getConsensusTraceStrategy() const override
    {
        static std::string const kStrategy{"none"};
        return kStrategy;
    }

    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    getTracer(std::string_view name) override
    {
        return provider_->GetTracer(std::string(name));
    }

    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(std::string_view name, opentelemetry::trace::SpanKind kind) override
    {
        opentelemetry::trace::StartSpanOptions opts;
        opts.kind = kind;
        return getTracer(kTracerName)->StartSpan(std::string(name), opts);
    }

    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
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
     * SDK provider owning the export pipeline.
     */
    std::shared_ptr<otel_sdk_trace::TracerProvider> provider_;

    /**
     * Shared buffer that receives ended spans from the exporter.
     */
    std::shared_ptr<otel_memory::InMemorySpanData> spanData_;
};

/**
 * @return The span's name as a std::string for equality checks.
 */
std::string
nameOf(otel_sdk_trace::SpanData const& span)
{
    auto view = span.GetName();
    return std::string(view.data(), view.size());
}

/**
 * @return Pointer to the first exported span with the given name, or null.
 */
otel_sdk_trace::SpanData*
findSpan(std::vector<std::unique_ptr<otel_sdk_trace::SpanData>> const& spans, std::string_view name)
{
    for (auto const& span : spans)
    {
        if (nameOf(*span) == name)
            return span.get();
    }
    return nullptr;
}

/**
 * @return Number of exported spans with the given name.
 */
std::size_t
countSpans(
    std::vector<std::unique_ptr<otel_sdk_trace::SpanData>> const& spans,
    std::string_view name)
{
    std::size_t count = 0;
    for (auto const& span : spans)
    {
        if (nameOf(*span) == name)
            ++count;
    }
    return count;
}

/**
 * Build the 16-byte deterministic trace_id used by the generator tests
 * (bytes 1..16). Kept out of line so every generator test pins the same id.
 * @return The fixed 16-byte trace_id {1, 2, ..., 16}.
 */
std::array<std::uint8_t, 16>
makeTraceIdBytes()
{
    std::array<std::uint8_t, 16> h{};
    for (int i = 0; i < 16; ++i)
        h[i] = static_cast<std::uint8_t>(i + 1);
    return h;
}

/**
 * Installs a TestTelemetry as the global instance for each test and clears it
 * afterwards so the singleton never dangles between cases.
 */
class SpanGuardScopeTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        telemetry_ = std::make_unique<TestTelemetry>();
        Telemetry::setInstance(telemetry_.get());
    }

    void
    TearDown() override
    {
        Telemetry::setInstance(nullptr);
        telemetry_.reset();
    }

    /**
     * @return The exporter's span buffer for the active TestTelemetry.
     */
    [[nodiscard]] std::shared_ptr<otel_memory::InMemorySpanData>
    spanData() const
    {
        return telemetry_->spanData();
    }

    /**
     * The in-memory Telemetry installed for the duration of a test.
     */
    std::unique_ptr<TestTelemetry> telemetry_;
};

// freshRoot() must ignore the ambient active span and start a brand-new trace.
TEST_F(SpanGuardScopeTest, spanGuard_freshRoot_is_true_root_ignoring_ambient)
{
    {
        // Ambient span becomes the active span on this thread.
        ScopedSpanGuard const ambient(TraceCategory::Rpc, "rpc", "command");
        ASSERT_TRUE(static_cast<bool>(ambient));

        // freshRoot must NOT inherit the ambient span as its parent.
        auto r = SpanGuard::freshRoot(TraceCategory::Peer, "peer", "validation.receive");
        ASSERT_TRUE(static_cast<bool>(r));
    }  // r ends first, then ambient's scope pops and ambient span ends.

    auto spans = spanData()->GetSpans();
    auto* ambient = findSpan(spans, "rpc.command");
    auto* root = findSpan(spans, "peer.validation.receive");
    ASSERT_NE(ambient, nullptr);
    ASSERT_NE(root, nullptr);

    // The ambient span is itself a root (first span, no parent).
    EXPECT_FALSE(ambient->GetParentSpanId().IsValid());
    EXPECT_TRUE(ambient->GetTraceId().IsValid());

    // The freshRoot span has NO parent and lives in a DIFFERENT trace.
    EXPECT_FALSE(root->GetParentSpanId().IsValid());
    EXPECT_TRUE(root->GetTraceId().IsValid());
    EXPECT_NE(root->GetTraceId(), ambient->GetTraceId());
}

// A ScopedSpanGuard is the ambient active span on its thread the moment it is
// constructed: a child created while it is alive parents to its span.
TEST_F(SpanGuardScopeTest, scopedGuard_is_ambient_on_construct)
{
    opentelemetry::trace::SpanId activeId;
    {
        ScopedSpanGuard const s(TraceCategory::Rpc, "rpc", "process");
        ASSERT_TRUE(static_cast<bool>(s));

        // While s is alive it is the active span on this thread's context.
        auto active =
            opentelemetry::trace::GetSpan(opentelemetry::context::RuntimeContext::GetCurrent())
                ->GetContext();
        ASSERT_TRUE(active.IsValid());
        activeId = active.span_id();

        // A child created here must parent to s's span, proving s is ambient.
        auto child = s.childSpan("rpc.dispatch");
        ASSERT_TRUE(static_cast<bool>(child));
    }  // child ends first, then s pops its scope and ends.

    auto spans = spanData()->GetSpans();
    auto* parent = findSpan(spans, "rpc.process");
    auto* child = findSpan(spans, "rpc.dispatch");
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);

    // The active span observed while s was alive WAS s's span.
    EXPECT_TRUE(activeId.IsValid());
    EXPECT_EQ(parent->GetSpanId(), activeId);

    // The child nested under s: same trace, parent = s's span.
    EXPECT_EQ(child->GetParentSpanId(), parent->GetSpanId());
    EXPECT_EQ(child->GetTraceId(), parent->GetTraceId());
}

// operator SpanGuard() && pops the Scope eagerly on the origin thread, so the
// span is no longer ambient here and the resulting thread-free guard can be
// ended on a worker thread without corrupting this thread's context stack.
TEST_F(SpanGuardScopeTest, scopedGuard_conversion_pops_scope_on_this_thread)
{
    {
        ScopedSpanGuard s(TraceCategory::Ledger, "ledger", "build");
        ASSERT_TRUE(static_cast<bool>(s));

        // Convert to a bare SpanGuard: the scope is popped here, on this thread.
        SpanGuard bare = std::move(s);
        ASSERT_TRUE(static_cast<bool>(bare));

        // The scope is gone: no span is active on this thread now.
        auto active =
            opentelemetry::trace::GetSpan(opentelemetry::context::RuntimeContext::GetCurrent())
                ->GetContext();
        EXPECT_FALSE(active.IsValid());

        // A new ambient span here is a fresh root, NOT nested under build.
        {
            ScopedSpanGuard const after(TraceCategory::Rpc, "rpc", "command");
            ASSERT_TRUE(static_cast<bool>(after));
        }

        // End the thread-free guard on a worker thread -- no crash.
        std::thread worker([g = std::move(bare)]() mutable {});
        worker.join();
    }

    auto spans = spanData()->GetSpans();
    auto* build = findSpan(spans, "ledger.build");
    auto* after = findSpan(spans, "rpc.command");
    ASSERT_NE(build, nullptr);
    ASSERT_NE(after, nullptr);

    // The span was exported exactly once, by the worker thread.
    EXPECT_EQ(countSpans(spans, "ledger.build"), 1u);

    // 'after' did NOT nest under build: fresh root, different trace.
    EXPECT_FALSE(after->GetParentSpanId().IsValid());
    EXPECT_NE(after->GetTraceId(), build->GetTraceId());
}

// The SpanGuard produced by the conversion ends the span exactly once: the
// moved-from ScopedSpanGuard must not re-end it on destruction.
TEST_F(SpanGuardScopeTest, scopedGuard_conversion_result_ends_span_once)
{
    {
        ScopedSpanGuard scoped(TraceCategory::Ledger, "ledger", "build");
        ASSERT_TRUE(static_cast<bool>(scoped));

        SpanGuard const bare = std::move(scoped);
        ASSERT_TRUE(static_cast<bool>(bare));

        // Nothing exported yet: the span is still open.
        EXPECT_EQ(countSpans(spanData()->GetSpans(), "ledger.build"), 0u);

        // 'bare' ends the span here on destruction; the moved-from 'scoped'
        // guard is destroyed too but must NOT end it a second time.
    }

    auto spans = spanData()->GetSpans();
    // Exactly one export: not zero (the guard still owns the span) and not two
    // (the moved-from scoped guard does not re-end it).
    EXPECT_EQ(countSpans(spans, "ledger.build"), 1u);
}

// A forced-root span started while a PendingTraceId is active adopts that
// pinned 16-byte trace_id and remains a true root (no parent).
TEST_F(SpanGuardScopeTest, deterministicIdGenerator_forced_root_gets_pending_trace_id)
{
    auto const h = makeTraceIdBytes();
    {
        PendingTraceId const pending{h};
        auto rootCtx = opentelemetry::context::Context{opentelemetry::trace::kIsRootSpanKey, true};
        auto span =
            telemetry_->startSpan("tx.receive", rootCtx, opentelemetry::trace::SpanKind::kInternal);
        span->End();
    }  // ~PendingTraceId asserts the id was consumed.

    auto spans = spanData()->GetSpans();
    ASSERT_EQ(spans.size(), 1u);
    // trace_id == the pinned hash.
    EXPECT_EQ(std::memcmp(spans[0]->GetTraceId().Id().data(), h.data(), 16), 0);
    // TRUE ROOT: no parent.
    EXPECT_FALSE(spans[0]->GetParentSpanId().IsValid());
}

// A forced-root span with NO PendingTraceId gets a random (non-zero) trace_id,
// never the deterministic hash -- the safety property when no id is pinned.
TEST_F(SpanGuardScopeTest, deterministicIdGenerator_no_pending_gives_random_root)
{
    auto const h = makeTraceIdBytes();
    {
        auto rootCtx = opentelemetry::context::Context{opentelemetry::trace::kIsRootSpanKey, true};
        auto span =
            telemetry_->startSpan("tx.receive", rootCtx, opentelemetry::trace::SpanKind::kInternal);
        span->End();
    }

    auto spans = spanData()->GetSpans();
    ASSERT_EQ(spans.size(), 1u);
    // Random root: valid (non-zero) trace_id...
    EXPECT_TRUE(spans[0]->GetTraceId().IsValid());
    // ...and NOT the deterministic hash (no pending id leaked in).
    EXPECT_NE(std::memcmp(spans[0]->GetTraceId().Id().data(), h.data(), 16), 0);
    EXPECT_FALSE(spans[0]->GetParentSpanId().IsValid());
}

// SAFETY: an ambient child under a live parent never adopts a pending id. The
// SDK inherits the parent's trace_id and never calls GenerateTraceId() for the
// child, so the pinned id stays available -- proven here by a trailing
// forced-root span that DOES adopt it (which also consumes the id so
// ~PendingTraceId's consumed-assert holds; see the report for this choice).
TEST_F(SpanGuardScopeTest, deterministicIdGenerator_ambient_child_ignores_pending)
{
    auto const h = makeTraceIdBytes();
    {
        // Ambient parent (a random-id root) active on this thread FIRST, before
        // any id is pinned, so the parent itself does not consume it.
        ScopedSpanGuard const parent(TraceCategory::Rpc, "rpc", "process");
        ASSERT_TRUE(static_cast<bool>(parent));

        // Pin the id, then start an ambient child under the live parent.
        PendingTraceId const pending{h};

        // The child has a valid parent, so the SDK inherits the parent's
        // trace_id and never calls GenerateTraceId(): the pending id is ignored.
        {
            auto child = parent.childSpan("rpc.dispatch");
            ASSERT_TRUE(static_cast<bool>(child));
        }

        // Consume the pinned id with a real forced-root span (its intended use),
        // so ~PendingTraceId sees the id as consumed. Its trace_id == h.
        {
            auto rootCtx =
                opentelemetry::context::Context{opentelemetry::trace::kIsRootSpanKey, true};
            auto root = telemetry_->startSpan(
                "tx.receive", rootCtx, opentelemetry::trace::SpanKind::kInternal);
            root->End();
        }
    }  // ~PendingTraceId: consumed == true, assert holds; then parent ends.

    auto spans = spanData()->GetSpans();
    auto* parent = findSpan(spans, "rpc.process");
    auto* child = findSpan(spans, "rpc.dispatch");
    auto* root = findSpan(spans, "tx.receive");
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(root, nullptr);

    // The ambient child inherited the parent's trace and did NOT adopt h.
    EXPECT_EQ(child->GetTraceId(), parent->GetTraceId());
    EXPECT_EQ(child->GetParentSpanId(), parent->GetSpanId());
    EXPECT_NE(std::memcmp(child->GetTraceId().Id().data(), h.data(), 16), 0);

    // The forced-root span DID adopt the pinned id: proof the id was available
    // the whole time -- the ambient child simply never requested it.
    EXPECT_EQ(std::memcmp(root->GetTraceId().Id().data(), h.data(), 16), 0);
    EXPECT_FALSE(root->GetParentSpanId().IsValid());
}

// Death test guarding the cross-thread scope-leak bug, now on ScopedSpanGuard.
//
// A ScopedSpanGuard's Scope is bound to the thread that constructed it, so it
// must be destroyed on that same thread. ScopedSpanGuard is non-movable, so it
// cannot be moved into a worker directly; instead we own it through a
// unique_ptr and move only that pointer to a worker thread. Destroying the
// pointer there runs ~ScopedSpanGuard on the WRONG thread, which pops the Scope
// on a foreign context stack -- ~ScopedSpanGuard's owner-thread XRPL_ASSERT
// turns that silent corruption into a loud abort().
//
// The death happens on the WORKER thread (the moved-in unique_ptr is destroyed
// when the worker lambda's captures are torn down, before worker.join()
// completes). A failed assert() calls abort(), which raises SIGABRT
// process-wide regardless of thread, so EXPECT_DEATH -- which runs the
// statement in a forked child and checks it dies -- observes the crash. The
// regex matches the assert message substring; the assert message is written as
// two adjacent string literals in SpanGuard.cpp, so the ".*" bridges the gap
// between "on" and "constructing".
//
// The test is skipped where the assertion cannot fire: under NDEBUG (Release
// builds) XRPL_ASSERT is a no-op, and under ENABLE_VOIDSTAR a failed assert
// continues instead of aborting -- in both cases the worker would not crash and
// EXPECT_DEATH would report a spurious failure.
TEST_F(SpanGuardScopeTest, scopedGuard_cross_thread_death_asserts_at_wrong_thread_destroy)
{
#ifdef NDEBUG
    GTEST_SKIP() << "XRPL_ASSERT compiles to a no-op under NDEBUG (Release builds), so the "
                    "cross-thread scope-leak assertion this test exercises does not fire.";
#elifdef ENABLE_VOIDSTAR
    GTEST_SKIP() << "ENABLE_VOIDSTAR continues past a failed XRPL_ASSERT instead of aborting, so "
                    "the cross-thread scope-leak assertion this test exercises does not crash.";
#else
    EXPECT_DEATH(
        {
            // Scoped guard constructed on THIS thread; its Scope binds here.
            auto scoped =
                std::make_unique<ScopedSpanGuard>(TraceCategory::Ledger, "ledger", "build");

            // Move only the owning pointer to a worker. ~ScopedSpanGuard runs on
            // the worker when the lambda's captures are destroyed and trips the
            // owner-thread assertion -> abort().
            std::thread worker([s = std::move(scoped)]() mutable {});
            worker.join();
        },
        // The assert message is written as two adjacent string literals in
        // SpanGuard.cpp; assert() stringifies the expression source via the
        // preprocessor '#' operator, keeping both quoted literals with the
        // "\" \"" gap between "on" and "constructing". Match across that gap.
        ".*destroyed on.*constructing thread.*");
#endif
}

}  // namespace
}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

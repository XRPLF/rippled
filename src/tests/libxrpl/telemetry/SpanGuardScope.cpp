// Tests for SpanGuard::rootSpan() and SpanGuard::detached().
//
// These verify the cross-thread scope-leak fix at the trace level using an
// in-memory span exporter: rootSpan() must start a fresh trace root (ignoring
// the ambient active span), and detached() must strip the thread-local Scope
// on the origin thread so the guard can be moved to and ended on another
// thread without corrupting the origin thread's context stack.
//
// The whole file is telemetry-only: when XRPL_ENABLE_TELEMETRY is not defined
// SpanGuard is a no-op stub and the OpenTelemetry SDK headers are unavailable,
// so the translation unit compiles empty.

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/memory/in_memory_span_data.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter_factory.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/samplers/always_on_factory.h>
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

#include <cstddef>
#include <memory>
#include <optional>
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
 * (trace id, span id, parent id, name).
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
        provider_ = otel_sdk_trace::TracerProviderFactory::Create(
            std::move(processor),
            opentelemetry::sdk::resource::Resource::Create({}),
            otel_sdk_trace::AlwaysOnSamplerFactory::Create());
    }

    /**
     * @return The exporter's span buffer (drained by GetSpans()).
     */
    std::shared_ptr<otel_memory::InMemorySpanData>
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
    std::shared_ptr<otel_memory::InMemorySpanData>
    spanData() const
    {
        return telemetry_->spanData();
    }

    /**
     * The in-memory Telemetry installed for the duration of a test.
     */
    std::unique_ptr<TestTelemetry> telemetry_;
};

// rootSpan() must ignore the ambient active span and start a brand-new trace.
TEST_F(SpanGuardScopeTest, rootSpan_ignores_ambient_parent)
{
    {
        // Ambient span becomes the active span on this thread.
        auto ambient = SpanGuard::span(TraceCategory::Rpc, "rpc", "command");
        ASSERT_TRUE(static_cast<bool>(ambient));

        // rootSpan must NOT inherit the ambient span as its parent.
        auto root = SpanGuard::rootSpan(TraceCategory::Peer, "peer", "validation.receive");
        ASSERT_TRUE(static_cast<bool>(root));
    }  // root ends first, then ambient -- both on this thread.

    auto spans = spanData()->GetSpans();
    auto* ambient = findSpan(spans, "rpc.command");
    auto* root = findSpan(spans, "peer.validation.receive");
    ASSERT_NE(ambient, nullptr);
    ASSERT_NE(root, nullptr);

    // The ambient span is itself a root (first span, no parent).
    EXPECT_FALSE(ambient->GetParentSpanId().IsValid());
    EXPECT_TRUE(ambient->GetTraceId().IsValid());

    // The rootSpan has NO parent and lives in a DIFFERENT trace than ambient.
    EXPECT_FALSE(root->GetParentSpanId().IsValid());
    EXPECT_TRUE(root->GetTraceId().IsValid());
    EXPECT_NE(root->GetTraceId(), ambient->GetTraceId());
}

// detached() pops the Scope on the origin thread, so ending the guard on
// another thread leaves the origin thread's context stack clean.
TEST_F(SpanGuardScopeTest, detached_does_not_corrupt_origin_stack)
{
    // Scoped guard is active on this (origin) thread.
    auto guard = SpanGuard::span(TraceCategory::Ledger, "ledger", "build");
    ASSERT_TRUE(static_cast<bool>(guard));

    // Strip the thread-local Scope here on the origin thread.
    auto detached = std::move(guard).detached();
    ASSERT_TRUE(static_cast<bool>(detached));

    // End the detached span on a worker thread.
    std::thread worker([d = std::move(detached)]() mutable {});
    worker.join();

    // Back on the origin thread: a new span must be a fresh root, proving the
    // origin stack top was restored by detached() (not left holding
    // ledger.build).
    {
        auto after = SpanGuard::span(TraceCategory::Rpc, "rpc", "command");
        ASSERT_TRUE(static_cast<bool>(after));
    }

    auto spans = spanData()->GetSpans();
    auto* build = findSpan(spans, "ledger.build");
    auto* after = findSpan(spans, "rpc.command");
    ASSERT_NE(build, nullptr);
    ASSERT_NE(after, nullptr);

    // 'after' is a new root: zero parent and a different trace than
    // ledger.build.
    EXPECT_FALSE(after->GetParentSpanId().IsValid());
    EXPECT_NE(after->GetTraceId(), build->GetTraceId());
}

// A detached span is ended exactly once, by the worker thread that owns it.
TEST_F(SpanGuardScopeTest, detached_span_ends_once)
{
    auto guard = SpanGuard::span(TraceCategory::Ledger, "ledger", "build");
    auto detached = std::move(guard).detached();
    ASSERT_TRUE(static_cast<bool>(detached));

    // Before the worker ends it, the span is still open: nothing exported yet.
    auto before = spanData()->GetSpans();
    EXPECT_EQ(countSpans(before, "ledger.build"), 0u);

    // Ending happens once, on the worker thread, when the guard is destroyed.
    {
        std::thread worker([d = std::move(detached)]() mutable {});
        worker.join();
    }

    auto after = spanData()->GetSpans();
    EXPECT_EQ(countSpans(after, "ledger.build"), 1u);
}

// rootSpan().detached() is the combination used at RPC coroutine entry points
// (e.g. RipplePathFind) that hold a span across a coro yield: the span must be
// a fresh root AND must not leave a Scope on the worker's context stack.
TEST_F(SpanGuardScopeTest, root_then_detached_is_root_and_leaves_stack_clean)
{
    {
        // Ambient span active on this (origin) thread.
        auto ambient = SpanGuard::span(TraceCategory::Rpc, "rpc", "command");
        ASSERT_TRUE(static_cast<bool>(ambient));

        // Fresh root, then detach the Scope on the origin thread.
        auto req = SpanGuard::rootSpan(TraceCategory::Rpc, "pathfind", "request").detached();
        ASSERT_TRUE(static_cast<bool>(req));

        // End the detached root span on a worker thread (mimics coro resume).
        std::thread worker([r = std::move(req)]() mutable {});
        worker.join();

        // Origin stack must be clean: a new span here is still parented by the
        // ambient span (proving the detached root did not linger on the stack).
        {
            auto child = SpanGuard::span(TraceCategory::Rpc, "rpc", "sub");
            ASSERT_TRUE(static_cast<bool>(child));
        }
    }

    auto spans = spanData()->GetSpans();
    auto* ambient = findSpan(spans, "rpc.command");
    auto* req = findSpan(spans, "pathfind.request");
    auto* child = findSpan(spans, "rpc.sub");
    ASSERT_NE(ambient, nullptr);
    ASSERT_NE(req, nullptr);
    ASSERT_NE(child, nullptr);

    // The rooted-then-detached span has NO parent and a DIFFERENT trace.
    EXPECT_FALSE(req->GetParentSpanId().IsValid());
    EXPECT_NE(req->GetTraceId(), ambient->GetTraceId());

    // The detached root did not corrupt the origin stack: 'child' still nests
    // under the ambient span (same trace, parent = ambient), NOT under req.
    EXPECT_EQ(child->GetParentSpanId(), ambient->GetSpanId());
    EXPECT_EQ(child->GetTraceId(), ambient->GetTraceId());
}

// Death test guarding the cross-thread scope-leak bug.
//
// This used to be a "negative control": a scoped guard was NOT detached, moved
// to and destroyed on a worker thread (popping the WRONG thread's stack), and
// the test then asserted that a later span on the origin thread had SILENTLY
// inherited the stale span -- proving the corruption happened undetected.
//
// SpanGuard::Impl::~Impl now enforces thread affinity with an XRPL_ASSERT: a
// live scope must be destroyed on the thread that created it. In debug/test
// builds (NDEBUG unset) XRPL_ASSERT expands to assert(), so the exact sequence
// above now aborts the process inside the worker thread's destructor instead of
// corrupting the origin stack. The bug therefore moved from "silently wrong" to
// "loudly crashes early" -- the intended tradeoff, since a crash is far easier
// to catch than a corrupted trace hierarchy.
//
// The death happens on the WORKER thread (the moved-in guard is destroyed when
// the worker lambda returns, before worker.join() completes). A failed assert()
// calls abort(), which raises SIGABRT process-wide regardless of which thread
// hit it, so EXPECT_DEATH -- which runs the statement in a forked child and
// checks it dies -- observes the crash. The regex matches the assert message
// substring rather than the whole line, because the file:line/function prefix
// glibc prints around it is platform and compiler dependent.
//
// The test is skipped where the assertion cannot fire: under NDEBUG (Release
// builds) XRPL_ASSERT is a no-op, and under ENABLE_VOIDSTAR a failed assert
// continues instead of aborting -- in both cases the worker would not crash and
// EXPECT_DEATH would report a spurious failure.
TEST_F(SpanGuardScopeTest, non_detached_cross_thread_death_asserts_at_wrong_thread_destroy)
{
    // XRPL_ASSERT only aborts when assertions are live. Under NDEBUG (Release
    // builds) it expands to assert(), which the C standard strips to a no-op,
    // so the worker thread destroys the guard without crashing. Under
    // ENABLE_VOIDSTAR (Antithesis fuzzing) a failed XRPL_ASSERT continues
    // execution instead of aborting. In either case the process does not die
    // and EXPECT_DEATH would fail, so skip this test there. The two macros are
    // mutually exclusive (instrumentation.h #errors on ENABLE_VOIDSTAR+NDEBUG),
    // but both break the death check, so guard against each.
#ifdef NDEBUG
    GTEST_SKIP() << "XRPL_ASSERT compiles to a no-op under NDEBUG (Release builds), so the "
                    "cross-thread scope-leak assertion this test exercises does not fire.";
#elif defined(ENABLE_VOIDSTAR)
    GTEST_SKIP() << "ENABLE_VOIDSTAR continues past a failed XRPL_ASSERT instead of aborting, so "
                    "the cross-thread scope-leak assertion this test exercises does not crash.";
#else
    EXPECT_DEATH(
        {
            // Scoped guard is active on this (origin) thread; its Scope is
            // bound to this thread.
            auto guard = SpanGuard::span(TraceCategory::Ledger, "ledger", "build");

            // Move the still-scoped guard to a worker thread WITHOUT detaching.
            // ~SpanGuard::Impl runs on the worker when the lambda returns and
            // trips the thread-affinity assertion -> abort().
            std::thread worker([d = std::move(guard)]() mutable {});
            worker.join();
        },
        // The assert message is written as two adjacent string literals in
        // SpanGuard.cpp; glibc's assert() stringifies the expression source via
        // the preprocessor '#' operator, keeping both quoted literals with the
        // "\" \"" gap between "on" and "constructing". Match across that gap.
        ".*scoped guard destroyed on.*constructing thread.*");
#endif
}

// detachInPlace(optional&) must detach the live guard the same way the
// hand-rolled emplace(std::move(*opt).detached()) idiom does: after the call
// the guard can be moved to and ended on a worker thread without leaving the
// origin thread's context stack holding it.
TEST_F(SpanGuardScopeTest, detach_in_place_optional_detaches_active_guard)
{
    // Scoped optional guard is active on this (origin) thread.
    std::optional<SpanGuard> opt = SpanGuard::span(TraceCategory::Ledger, "ledger", "build");
    ASSERT_TRUE(opt.has_value());
    ASSERT_TRUE(static_cast<bool>(*opt));

    // Strip the thread-local Scope here on the origin thread via the helper.
    detachInPlace(opt);
    ASSERT_TRUE(opt.has_value());
    ASSERT_TRUE(static_cast<bool>(*opt));

    // End the detached span on a worker thread.
    std::thread worker([d = std::move(*opt)]() mutable {});
    worker.join();

    // Back on the origin thread: a new span must be a fresh root, proving the
    // origin stack top was restored by detachInPlace() (not left holding
    // ledger.build).
    {
        auto after = SpanGuard::span(TraceCategory::Rpc, "rpc", "command");
        ASSERT_TRUE(static_cast<bool>(after));
    }

    auto spans = spanData()->GetSpans();
    auto* build = findSpan(spans, "ledger.build");
    auto* after = findSpan(spans, "rpc.command");
    ASSERT_NE(build, nullptr);
    ASSERT_NE(after, nullptr);

    // 'after' is a new root: zero parent and a different trace than
    // ledger.build.
    EXPECT_FALSE(after->GetParentSpanId().IsValid());
    EXPECT_NE(after->GetTraceId(), build->GetTraceId());
}

// detachInPlace(optional&) on an empty optional is a no-op: it must not crash,
// must leave the optional empty, and must export nothing.
TEST_F(SpanGuardScopeTest, detach_in_place_optional_noop_on_empty)
{
    std::optional<SpanGuard> opt;
    ASSERT_FALSE(opt.has_value());

    detachInPlace(opt);

    // Still empty, no span was created or exported.
    EXPECT_FALSE(opt.has_value());
    EXPECT_TRUE(spanData()->GetSpans().empty());
}

// detachInPlace(shared_ptr) must detach the live guard and return it as a new
// shared_ptr, so the returned guard can be moved to and ended on a worker
// thread without corrupting the origin thread's context stack.
TEST_F(SpanGuardScopeTest, detach_in_place_shared_detaches_active_guard)
{
    // Scoped shared guard is active on this (origin) thread.
    auto span =
        std::make_shared<SpanGuard>(SpanGuard::span(TraceCategory::Ledger, "ledger", "build"));
    ASSERT_NE(span, nullptr);
    ASSERT_TRUE(static_cast<bool>(*span));

    // Strip the thread-local Scope here on the origin thread via the helper.
    span = detachInPlace(std::move(span));
    ASSERT_NE(span, nullptr);
    ASSERT_TRUE(static_cast<bool>(*span));

    // End the detached span on a worker thread.
    std::thread worker([d = std::move(span)]() mutable {});
    worker.join();

    // Back on the origin thread: a new span must be a fresh root, proving the
    // origin stack top was restored by detachInPlace() (not left holding
    // ledger.build).
    {
        auto after = SpanGuard::span(TraceCategory::Rpc, "rpc", "command");
        ASSERT_TRUE(static_cast<bool>(after));
    }

    auto spans = spanData()->GetSpans();
    auto* build = findSpan(spans, "ledger.build");
    auto* after = findSpan(spans, "rpc.command");
    ASSERT_NE(build, nullptr);
    ASSERT_NE(after, nullptr);

    // 'after' is a new root: zero parent and a different trace than
    // ledger.build.
    EXPECT_FALSE(after->GetParentSpanId().IsValid());
    EXPECT_NE(after->GetTraceId(), build->GetTraceId());
}

// detachInPlace(shared_ptr) on a null pointer is a no-op: it must not crash and
// must return null unchanged.
TEST_F(SpanGuardScopeTest, detach_in_place_shared_noop_on_null)
{
    auto result = detachInPlace(std::shared_ptr<SpanGuard>{});
    EXPECT_EQ(result, nullptr);
}

}  // namespace
}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY

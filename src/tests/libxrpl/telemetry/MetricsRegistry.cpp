/**
 * GTest unit tests for MetricsRegistry.
 *
 *  Three independent groups, split by what they can link:
 *
 *  1. sanitiseHandler() — the `handler` label sanitiser. Runs in **both**
 *     builds. sanitiseHandler() is a public static constexpr defined inline
 *     in the header, so it needs no part of MetricsRegistry.cpp on the link
 *     line. These tests therefore sit outside the guard below; putting them
 *     inside it would silently compile them out of the telemetry-enabled
 *     build, which is the build that actually exports the label.
 *
 *  2. scaledMean() — the guarded-division helper behind every derived mean
 *     on the nodestore_state gauge. Also a public static constexpr inline,
 *     so it runs in both builds for the same reason.
 *
 *  3. The no-op / telemetry-disabled path — construction, start()/stop()
 *     lifecycle, and the synchronous record*() methods. Guarded, because
 *     when XRPL_ENABLE_TELEMETRY is defined MetricsRegistry.cpp is not
 *     compiled into this binary (see src/tests/libxrpl/CMakeLists.txt) and
 *     its out-of-line symbols are unresolvable here.
 */

#include <xrpld/telemetry/MetricsRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string_view>

namespace {

using xrpl::telemetry::MetricsRegistry;

/**
 * Every job name reaching the JobQueue in non-test code that
 * sanitiseHandler() must return unchanged, i.e. every one consisting solely
 * of ASCII letters.
 *
 * How to re-derive this set (it is read off the source, not off docs):
 *
 *  1. Enumerate the four surfaces that put a name into the JobQueue,
 *     excluding `src/test/` and `src/tests/`:
 *       - `JobQueue::addJob`
 *       - `JobQueue::postCoro` (its `name` becomes `Coro::name_`, which
 *         `Coro::post()` hands to `addRefCountedJob`)
 *       - `LedgerMaster::newPFWork`, a thin `addJob` wrapper
 *       - each `TimeoutCounter` subclass's `.jobName =` designated
 *         initialiser, consumed by `TimeoutCounter::queueJob()`'s `addJob`
 *     `addRefCountedJob` is private and has only those callers, so the four
 *     surfaces close the set.
 *  2. Resolve each `name` argument to the literal(s) it can hold. Most are
 *     literals written in place, but four call sites pass a variable and
 *     must be traced back:
 *       - `PeerImp.cpp` passes a local `std::string const name =
 *         isTrusted ? "ChkTrust" : "ChkUntrust";`, so one call site
 *         contributes *two* names. Neither literal appears at an `addJob`
 *         call site, which is why a grep of `addJob` alone misses them.
 *       - `LedgerMaster::newPFWork` forwards its own `char const* name`
 *         parameter; its three callers supply the three `PthFind*` literals.
 *       - `TimeoutCounter` forwards `queueJobParameter_.jobName`, set by a
 *         `.jobName =` designated initialiser in each of its five
 *         subclasses.
 *       - `Coro::post()` forwards `name_`, set from `postCoro`'s argument.
 *  3. Keep the names that satisfy sanitiseHandler()'s rule — non-empty and
 *     all ASCII letters. Everything else belongs in kFoldToOtherHandlers.
 *
 * Note what step 3 excludes. Two call sites compose the name at runtime from
 * a literal prefix and a ledger sequence: `"Pub" + std::to_string(seq)` and
 * `"OB" + std::to_string(seq % 1000000000)`. The bare prefixes `"Pub"` and
 * `"OB"` are all letters, but they are never what reaches the JobQueue — the
 * *composed* form is, and it always carries digits. So they are absent here
 * and their composed forms appear in kFoldToOtherHandlers instead.
 *
 * `JobQueue::makeLoadEvent` is deliberately out of scope: its `name` feeds
 * `LoadEvent`/`LoadMonitor`, neither of which reaches MetricsRegistry, so
 * names like `"cmd:" + method` never become a `handler` label value.
 *
 * Asserting on the real list is the point of the test: it proves the
 * cardinality bound holds for the names actually in the binary, so a job
 * added later whose name breaks the rule shows up as a failure here rather
 * than as an unexplained `other` bucket on a dashboard.
 */
constexpr std::array kPassThroughHandlers = {
    std::string_view{"AcceptLedger"},     // RCLConsensus.cpp
    std::string_view{"AcqDone"},          // InboundLedger.cpp
    std::string_view{"AdvanceLedger"},    // LedgerMaster.cpp
    std::string_view{"ChkTrust"},         // PeerImp.cpp (local, ternary)
    std::string_view{"ChkUntrust"},       // PeerImp.cpp (local, ternary)
    std::string_view{"ComplAcquire"},     // TransactionAcquire.cpp
    std::string_view{"DoTxs"},            // PeerImp.cpp
    std::string_view{"GotFetchPack"},     // LedgerMaster.cpp
    std::string_view{"GotStaleData"},     // InboundLedgers.cpp
    std::string_view{"HandleHaveTxs"},    // PeerImp.cpp
    std::string_view{"HistTxStream"},     // NetworkOPs.cpp
    std::string_view{"InboundLedger"},    // InboundLedger.cpp (.jobName)
    std::string_view{"LedReplDelta"},     // LedgerDeltaAcquire.cpp (.jobName)
    std::string_view{"LedReplTask"},      // LedgerReplayTask.cpp (.jobName)
    std::string_view{"MakeFetchPack"},    // PeerImp.cpp
    std::string_view{"NObjStore"},        // NodeStoreScheduler.cpp
    std::string_view{"NetCluster"},       // NetworkOPs.cpp
    std::string_view{"NetHeart"},         // NetworkOPs.cpp
    std::string_view{"OnLedBuilt"},       // LedgerDeltaAcquire.cpp
    std::string_view{"ProcessLData"},     // InboundLedgers.cpp
    std::string_view{"PthFindNewLed"},    // LedgerMaster.cpp (newPFWork)
    std::string_view{"PthFindNewReq"},    // LedgerMaster.cpp (newPFWork)
    std::string_view{"PthFindOBDB"},      // LedgerMaster.cpp (newPFWork)
    std::string_view{"PubCons"},          // NetworkOPs.cpp
    std::string_view{"PubFee"},           // NetworkOPs.cpp
    std::string_view{"RPCSubSendThr"},    // RPCSub.cpp
    std::string_view{"RcvCheckTx"},       // PeerImp.cpp
    std::string_view{"RcvGetLedger"},     // PeerImp.cpp
    std::string_view{"RcvGetObjByHash"},  // PeerImp.cpp
    std::string_view{"RcvManifests"},     // PeerImp.cpp
    std::string_view{"RcvPeerData"},      // PeerImp.cpp
    std::string_view{"RcvProofPReq"},     // PeerImp.cpp
    std::string_view{"RcvReplDReq"},      // PeerImp.cpp
    std::string_view{"SkipListAcq"},      // SkipListAcquire.cpp (.jobName)
    std::string_view{"SubmitTxn"},        // NetworkOPs.cpp
    std::string_view{"TryFill"},          // LedgerMaster.cpp
    std::string_view{"TxAcq"},            // TransactionAcquire.cpp (.jobName)
    std::string_view{"TxBatchAsync"},     // NetworkOPs.cpp
    std::string_view{"TxBatchSync"},      // NetworkOPs.cpp
    std::string_view{"TxsToTxn"},         // ConsensusTransSetSF.cpp
    std::string_view{"WAL"},              // SociDB.cpp
    std::string_view{"checkPropose"},     // PeerImp.cpp (lowercase start)
    std::string_view{"sweep"},            // Application.cpp (all lowercase)
};

/**
 * Names that must fold to kHandlerOther.
 *
 * The first two are the composed forms of the only two dynamically built job
 * names in the tree: `"Pub" + std::to_string(ledger->seq())`
 * (LedgerPersistence.cpp) and
 * `"OB" + std::to_string(ledger->seq() % 1000000000)` (OrderBookDBImpl.cpp).
 * They are the reason the sanitiser exists — used raw they would mint a
 * Prometheus series per ledger — so realistic sequence values are used
 * rather than short placeholders. The sequence is unbounded at the `"Pub"`
 * site and masked to nine digits at the `"OB"` site, but a digit appears
 * either way (even `seq == 0` gives `"Pub0"`), so no reachable input at
 * either site can produce an all-letter name.
 *
 * The next five are static literals that already fail the rule today, so
 * the fallback is exercised by real code and not only by synthetic input.
 *
 * The remainder are the edge cases: empty, and one entry per disallowed
 * character class (space, digit, underscore, hyphen, non-ASCII byte). The
 * non-ASCII entry is UTF-8 'e-acute'; on a signed-char platform its lead
 * byte is negative, which the explicit ASCII range check rejects where a
 * locale-sensitive std::isalpha might not.
 */
constexpr std::array kFoldToOtherHandlers = {
    std::string_view{"Pub97531234"},  // dynamic: "Pub" + ledger seq
    std::string_view{"OB123456789"},  // dynamic: "OB" + ledger seq % 1e9
    std::string_view{"GetConsL1"},    // static, digit  (RCLConsensus.cpp)
    std::string_view{"GetConsL2"},    // static, digit  (RCLValidations.cpp)
    std::string_view{"gRPC-Client"},  // static, hyphen (GRPCServer.cpp)
    std::string_view{"RPC-Client"},   // static, hyphen (ServerHandler.cpp)
    std::string_view{"WS-Client"},    // static, hyphen (ServerHandler.cpp)
    std::string_view{""},             // empty
    std::string_view{"Rcv Ledger"},   // space
    std::string_view{"Handler7"},     // digit
    std::string_view{"Rcv_Ledger"},   // underscore
    std::string_view{"Rcv-Ledger"},   // hyphen
    std::string_view{"caf\xC3\xA9"},  // non-ASCII byte (UTF-8 e-acute)
};

/**
 * True when sanitiseHandler() returns each pass-through name unchanged.
 *
 * consteval so a regression is a compile error rather than a test failure:
 * the sanitiser is constexpr precisely so this bound can be checked without
 * running anything.
 */
consteval bool
allPassThroughUnchanged()
{
    return std::ranges::all_of(kPassThroughHandlers, [](auto const name) {
        return MetricsRegistry::sanitiseHandler(name) == name;
    });
}

/**
 * True when sanitiseHandler() maps every listed name to kHandlerOther.
 */
consteval bool
allFoldToOther()
{
    return std::ranges::all_of(kFoldToOtherHandlers, [](auto const name) {
        return MetricsRegistry::sanitiseHandler(name) == MetricsRegistry::kHandlerOther;
    });
}

// Compile-time guarantees. Duplicated at runtime below so a failure names
// the offending input instead of only pointing at the assertion.
static_assert(allPassThroughUnchanged());
static_assert(allFoldToOther());

// The verified size of the pass-through set as of this branch: 43 all-letter
// job-name literals. Pinned so that adding or removing a job name without
// revisiting the label-cardinality budget fails the build here.
static_assert(kPassThroughHandlers.size() == 43);

/**
 * Total distinct `handler` label values reachable from the inputs above:
 * one per pass-through name plus the single shared kHandlerOther bucket.
 * This is the number the Prometheus cardinality budget is sized against.
 */
constexpr std::size_t kExpectedHandlerDomain = kPassThroughHandlers.size() + 1;
static_assert(kExpectedHandlerDomain == 44);

}  // namespace

TEST(MetricsRegistrySanitiseHandler, static_job_names_pass_through_unchanged)
{
    // Every all-letter job name in the tree survives sanitisation, so the
    // `handler` label keeps its attribution value for real producers.
    for (auto const name : kPassThroughHandlers)
    {
        EXPECT_EQ(MetricsRegistry::sanitiseHandler(name), name)
            << "job name should pass through unchanged: " << name;
        // Cause, not just state: it passed because it is not the fallback.
        EXPECT_NE(MetricsRegistry::sanitiseHandler(name), MetricsRegistry::kHandlerOther)
            << "job name wrongly folded to the fallback: " << name;
    }
}

TEST(MetricsRegistrySanitiseHandler, dynamic_and_non_letter_names_fold_to_other)
{
    // Negative path: everything that is not an all-letter name collapses
    // into exactly one bucket, which is what bounds the label domain.
    for (auto const name : kFoldToOtherHandlers)
    {
        EXPECT_EQ(MetricsRegistry::sanitiseHandler(name), MetricsRegistry::kHandlerOther)
            << "name should fold to the fallback: " << name;
    }
}

TEST(MetricsRegistrySanitiseHandler, empty_name_folds_to_other)
{
    // Called out separately because it is the one case the all-letter scan
    // cannot catch: std::ranges::all_of() is vacuously true on an empty
    // range, so the sanitiser needs its own emptiness check.
    EXPECT_EQ(MetricsRegistry::sanitiseHandler(std::string_view{}), MetricsRegistry::kHandlerOther);
    EXPECT_EQ(MetricsRegistry::sanitiseHandler(""), MetricsRegistry::kHandlerOther);
}

TEST(MetricsRegistrySanitiseHandler, fallback_value_is_the_shared_constant)
{
    // The fallback must be the constant the dashboards and the reference doc
    // are written against, not merely some non-empty string.
    EXPECT_EQ(MetricsRegistry::kHandlerOther, std::string_view{"other"});
    EXPECT_EQ(MetricsRegistry::kHandlerOther.size(), 5u);

    // "other" is itself all letters, so sanitising it is idempotent -- a
    // handler genuinely named "other" is indistinguishable from the bucket.
    EXPECT_EQ(
        MetricsRegistry::sanitiseHandler(MetricsRegistry::kHandlerOther),
        MetricsRegistry::kHandlerOther);
}

TEST(MetricsRegistrySanitiseHandler, output_domain_is_exactly_44_values)
{
    // The cardinality bound itself: over every input above -- 43 real job
    // names, 2 dynamic names, 5 non-conforming static names and 6 edge
    // cases -- the sanitiser can emit only 44 distinct label values (43
    // names plus the single "other" bucket).
    std::set<std::string_view> domain;
    for (auto const name : kPassThroughHandlers)
        domain.insert(MetricsRegistry::sanitiseHandler(name));
    for (auto const name : kFoldToOtherHandlers)
        domain.insert(MetricsRegistry::sanitiseHandler(name));

    EXPECT_EQ(domain.size(), kExpectedHandlerDomain);
    EXPECT_EQ(domain.size(), 44u);

    // State plus cause: the domain is the pass-through names and nothing
    // else besides the one fallback bucket.
    EXPECT_TRUE(domain.contains(MetricsRegistry::kHandlerOther));
    EXPECT_EQ(domain.size() - 1, kPassThroughHandlers.size());
    for (auto const name : kPassThroughHandlers)
        EXPECT_TRUE(domain.contains(name)) << "missing from domain: " << name;
}

// ---------------------------------------------------------------------------
// scaledMean() — the guarded division behind every derived mean published on
// the nodestore_state gauge.
//
// The property under test is not "it divides". It is that a zero denominator
// yields absence rather than a plausible zero, because a dashboard must show
// a gap instead of a number an operator would believe. Every expected value
// below is an independently computed constant, never a restatement of the
// implementation's own expression: `EXPECT_EQ(mean, total / count)` would
// pass even if both sides were wrong the same way.
// ---------------------------------------------------------------------------

namespace {

using Registry = MetricsRegistry;

// Compile-time checks first, so a regression is a build failure. Each
// expected value is written as a literal worked out by hand.
static_assert(Registry::scaledMean(500, 4) == 125);           // 500/4 exactly
static_assert(Registry::scaledMean(9, 1) == 9);               // single sample
static_assert(Registry::scaledMean(0, 4) == 0);               // real zero mean
static_assert(Registry::scaledMean(7, 2) == 3);               // 3.5 truncates
static_assert(Registry::scaledMean(7, 5, 100) == 140);        // 1.4 scaled
static_assert(Registry::scaledMean(4, 4, 100) == 100);        // depth exactly 1
static_assert(Registry::scaledMean(1, 3, 100) == 33);         // 0.333 scaled
static_assert(!Registry::scaledMean(500, 0).has_value());     // no samples
static_assert(!Registry::scaledMean(0, 0).has_value());       // idle, not zero
static_assert(!Registry::scaledMean(500, 4, 0).has_value());  // scale 0

}  // namespace

TEST(MetricsRegistryScaledMean, zero_count_reports_absence_not_zero)
{
    // The whole point of the helper. A mean over no samples is undefined, and
    // reporting it as 0 would draw a believable flat line at the bottom of a
    // latency axis. Absence is the only honest answer.
    EXPECT_FALSE(Registry::scaledMean(500, 0).has_value());
    EXPECT_FALSE(Registry::scaledMean(0, 0).has_value());
    EXPECT_FALSE(Registry::scaledMean(std::numeric_limits<std::uint64_t>::max(), 0).has_value());

    // Cause, not just state: absence is specific to a zero denominator. The
    // same numerator with one sample does produce a value, so the guard is
    // keyed on the count and is not rejecting everything.
    ASSERT_TRUE(Registry::scaledMean(500, 1).has_value());
    EXPECT_EQ(Registry::scaledMean(500, 1), std::optional<std::int64_t>{500});
}

TEST(MetricsRegistryScaledMean, zero_total_over_real_samples_is_a_genuine_zero)
{
    // The negative counterpart of the test above, and the reason absence and
    // zero must stay distinguishable: a store fast enough that every sample
    // truncated to 0 us really does have a mean of 0. That must be reported,
    // not suppressed, or a working fast path looks like a dead one.
    auto const mean = Registry::scaledMean(0, 32);
    ASSERT_TRUE(mean.has_value());
    EXPECT_EQ(mean, std::optional<std::int64_t>{0});
}

TEST(MetricsRegistryScaledMean, exact_means_are_computed_exactly)
{
    // Independently computed expectations: 4800/32 is 150 by hand.
    EXPECT_EQ(Registry::scaledMean(4800, 32), 150);
    // A mean equal to its own total when there is one sample.
    EXPECT_EQ(Registry::scaledMean(917, 1), 917);
    // Truncation toward zero is the documented behaviour: 99/10 is 9.9.
    EXPECT_EQ(Registry::scaledMean(99, 10), 9);
}

TEST(MetricsRegistryScaledMean, scale_recovers_the_fractional_digits)
{
    // Mean writer depth is the reason `scale` exists. Unscaled, a depth of
    // 1.4 truncates to 1 and is indistinguishable from an idle 1.0; the x100
    // form must keep the fraction.
    EXPECT_EQ(Registry::scaledMean(7, 5), 1);         // the lost signal
    EXPECT_EQ(Registry::scaledMean(7, 5, 100), 140);  // the kept signal

    // A pure fraction with no whole part must survive too. Without scaling
    // the remainder this would read 0 and the metric would be useless.
    EXPECT_EQ(Registry::scaledMean(1, 4, 100), 25);
    EXPECT_EQ(Registry::scaledMean(3, 8, 100), 37);  // 0.375 truncated

    // Scaling must not invent precision where the value is already whole.
    EXPECT_EQ(Registry::scaledMean(8, 4, 100), 200);
}

TEST(MetricsRegistryScaledMean, large_inputs_saturate_instead_of_wrapping)
{
    constexpr auto kU64Max = std::numeric_limits<std::uint64_t>::max();
    constexpr auto kI64Max = std::numeric_limits<std::int64_t>::max();

    // A uint64 total that does not fit the signed gauge must clamp. Wrapping
    // would surface as a sudden dip to a healthy-looking small number, which
    // is the failure mode worth preventing.
    auto const saturated = Registry::scaledMean(kU64Max, 1);
    if (!saturated.has_value())
        FAIL() << "a saturated mean must still be reported";
    EXPECT_EQ(*saturated, kI64Max);

    // Scaling must not overflow either: this quotient times 100 exceeds
    // int64 range, so it clamps rather than wraps negative.
    auto const scaled = Registry::scaledMean(kU64Max, 2, 100);
    if (!scaled.has_value())
        FAIL() << "a saturated scaled mean must still be reported";
    EXPECT_EQ(*scaled, kI64Max);

    // Every result is non-negative; a negative latency or depth is
    // meaningless and is the visible symptom of a wrap.
    EXPECT_GE(*saturated, 0);
    EXPECT_GE(*scaled, 0);

    // Just below the boundary the value is exact, not clamped, so the clamp
    // above is a real bound and not a blanket ceiling on everything.
    auto const exact = Registry::scaledMean(static_cast<std::uint64_t>(kI64Max), 1);
    EXPECT_EQ(exact, std::optional<std::int64_t>{kI64Max});
    auto const belowBoundary = Registry::scaledMean(1'000'000, 4, 100);
    EXPECT_EQ(belowBoundary, std::optional<std::int64_t>{25'000'000});
}

TEST(MetricsRegistryScaledMean, default_scale_is_one)
{
    // The two-argument form is the latency case and must not scale silently;
    // if the default were 100 every published latency would be 100x wrong.
    // 360/8 is 45 by hand -- an independent literal, not a restatement of the
    // implementation. A default of 100 would read 4500 here.
    EXPECT_EQ(Registry::scaledMean(360, 8), 45);
}

// When telemetry is globally enabled, MetricsRegistry.cpp requires xrpld
// link dependencies we cannot satisfy in a standalone GTest binary.
#ifndef XRPL_ENABLE_TELEMETRY

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>

#include <boost/asio/io_context.hpp>

#include <optional>
#include <stdexcept>
#include <string>

using namespace xrpl;

namespace {

/**
 * Minimal mock ServiceRegistry for MetricsRegistry testing.
 *
 *  Only the getMetricsRegistry() call is used in the tests; other methods
 *  are not invoked because the registry is disabled (enabled=false) so no
 *  gauge callbacks execute.
 *
 *  All pure virtual methods throw to catch accidental calls during tests.
 */
class MockServiceRegistry : public ServiceRegistry
{
    [[noreturn]] static void
    throwUnimplemented()
    {
        throw std::logic_error("MockServiceRegistry: method not implemented");
    }

public:
    // ServiceRegistry interface — stubs that should never be called.
    CollectorManager&
    getCollectorManager() override
    {
        throwUnimplemented();
    }
    Family&
    getNodeFamily() override
    {
        throwUnimplemented();
    }
    TimeKeeper&
    getTimeKeeper() override
    {
        throwUnimplemented();
    }
    JobQueue&
    getJobQueue() override
    {
        throwUnimplemented();
    }
    NodeCache&
    getTempNodeCache() override
    {
        throwUnimplemented();
    }
    CachedSLEs&
    getCachedSLEs() override
    {
        throwUnimplemented();
    }
    NetworkIDService&
    getNetworkIDService() override
    {
        throwUnimplemented();
    }
    AmendmentTable&
    getAmendmentTable() override
    {
        throwUnimplemented();
    }
    HashRouter&
    getHashRouter() override
    {
        throwUnimplemented();
    }
    LoadFeeTrack&
    getFeeTrack() override
    {
        throwUnimplemented();
    }
    LoadManager&
    getLoadManager() override
    {
        throwUnimplemented();
    }
    RCLValidations&
    getValidations() override
    {
        throwUnimplemented();
    }
    ValidatorList&
    getValidators() override
    {
        throwUnimplemented();
    }
    ValidatorSite&
    getValidatorSites() override
    {
        throwUnimplemented();
    }
    ManifestCache&
    getValidatorManifests() override
    {
        throwUnimplemented();
    }
    ManifestCache&
    getPublisherManifests() override
    {
        throwUnimplemented();
    }
    Overlay&
    getOverlay() override
    {
        throwUnimplemented();
    }
    Cluster&
    getCluster() override
    {
        throwUnimplemented();
    }
    PeerReservationTable&
    getPeerReservations() override
    {
        throwUnimplemented();
    }
    Resource::Manager&
    getResourceManager() override
    {
        throwUnimplemented();
    }
    node_store::Database&
    getNodeStore() override
    {
        throwUnimplemented();
    }
    SHAMapStore&
    getSHAMapStore() override
    {
        throwUnimplemented();
    }
    RelationalDatabase&
    getRelationalDatabase() override
    {
        throwUnimplemented();
    }
    InboundLedgers&
    getInboundLedgers() override
    {
        throwUnimplemented();
    }
    InboundTransactions&
    getInboundTransactions() override
    {
        throwUnimplemented();
    }
    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override
    {
        throwUnimplemented();
    }
    LedgerMaster&
    getLedgerMaster() override
    {
        throwUnimplemented();
    }
    LedgerCleaner&
    getLedgerCleaner() override
    {
        throwUnimplemented();
    }
    LedgerReplayer&
    getLedgerReplayer() override
    {
        throwUnimplemented();
    }
    PendingSaves&
    getPendingSaves() override
    {
        throwUnimplemented();
    }
    // AcquireStats lives in src/xrpld/ and is only forward-declared here; a
    // reference return to an incomplete type is fine because this throws.
    AcquireStats&
    getAcquireStats() override
    {
        throwUnimplemented();
    }
    [[nodiscard]] OpenLedger&
    getOpenLedger() override
    {
        throwUnimplemented();
    }
    [[nodiscard]] OpenLedger const&
    getOpenLedger() const override
    {
        throwUnimplemented();
    }
    NetworkOPs&
    getOPs() override
    {
        throwUnimplemented();
    }
    OrderBookDB&
    getOrderBookDB() override
    {
        throwUnimplemented();
    }
    TransactionMaster&
    getMasterTransaction() override
    {
        throwUnimplemented();
    }
    TxQ&
    getTxQ() override
    {
        throwUnimplemented();
    }
    PathRequestManager&
    getPathRequestManager() override
    {
        throwUnimplemented();
    }
    ServerHandler&
    getServerHandler() override
    {
        throwUnimplemented();
    }
    perf::PerfLog&
    getPerfLog() override
    {
        throwUnimplemented();
    }
    telemetry::Telemetry&
    getTelemetry() override
    {
        throwUnimplemented();
    }
    telemetry::MetricsRegistry*
    getMetricsRegistry() override
    {
        return nullptr;
    }
    [[nodiscard]] bool
    isStopping() const override
    {
        return false;
    }
    beast::Journal
    getJournal(std::string const&) override
    {
        return beast::Journal(beast::Journal::getNullSink());
    }
    boost::asio::io_context&
    getIOContext() override
    {
        throwUnimplemented();
    }
    Logs&
    getLogs() override
    {
        throwUnimplemented();
    }
    [[nodiscard]] std::optional<uint256> const&
    getTrapTxID() const override
    {
        static std::optional<uint256> const empty;
        return empty;
    }
    DatabaseCon&
    getWalletDB() override
    {
        throwUnimplemented();
    }
    Application&
    getApp() override
    {
        throwUnimplemented();
    }
};

/**
 * Test fixture that provides a MockServiceRegistry and null Journal.
 */
class MetricsRegistryTest : public ::testing::Test
{
protected:
    MockServiceRegistry mockApp_;
    beast::Journal j_{beast::Journal::getNullSink()};
};

}  // namespace

TEST_F(MetricsRegistryTest, disabled_construction)
{
    // Construct with enabled=false; should be a no-op.
    telemetry::MetricsRegistry const registry(false, mockApp_, j_);
    EXPECT_FALSE(registry.isEnabled());
}

TEST_F(MetricsRegistryTest, disabled_start_stop)
{
    telemetry::MetricsRegistry registry(false, mockApp_, j_);

    // start() and stop() should be no-ops when disabled.
    registry.start("http://localhost:4318/v1/metrics");
    registry.stop();

    // Double stop should be safe.
    registry.stop();
}

TEST_F(MetricsRegistryTest, disabled_recording_methods)
{
    telemetry::MetricsRegistry registry(false, mockApp_, j_);
    registry.start("http://localhost:4318/v1/metrics");

    // All recording methods should be no-ops (not crash).
    registry.recordRpcStarted("server_info");
    registry.recordRpcFinished("server_info", 1000);
    registry.recordRpcErrored("ledger", 500);
    registry.recordJobQueued("ledgerData", "ProcessLData");
    registry.recordJobStarted("ledgerData", "ProcessLData", 200);
    registry.recordJobFinished("ledgerData", "ProcessLData", 3000);

    registry.stop();
}

TEST_F(MetricsRegistryTest, destructor_calls_stop)
{
    {
        // Let the destructor handle cleanup.
        telemetry::MetricsRegistry registry(false, mockApp_, j_);
        registry.start("http://localhost:4318/v1/metrics");
    }
    // If we get here without crash, the destructor handled stop.
}

#endif  // !XRPL_ENABLE_TELEMETRY

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
 *
 *  Tests cover:
 *  - Construction with telemetry disabled (no-op behavior).
 *  - start()/stop() lifecycle when disabled.
 *  - Synchronous instrument recording methods do not crash when disabled.
 *  - Double stop() is safe.
 *  - Destructor handles cleanup without crash.
 *  - Compile-time-disabled proof for the sync-diagnostics gauges: the whole
 *    async-gauge registration surface is compiled away, and a full disabled
 *    lifecycle never touches any ServiceRegistry service -- including the
 *    Overlay and AmendmentTable the WP-A7 gauges would read.
 *
 *  NOTE: These tests only exercise the no-op path (telemetry disabled).
 *  When XRPL_ENABLE_TELEMETRY is defined, MetricsRegistry.cpp pulls in
 *  xrpld symbols that cannot be linked into this standalone test binary,
 *  so the tests are compiled out.
 *
 *  CONSEQUENCE for the sync-diagnostics gauges (`unl_quorum`,
 *  `clock_close_offset_seconds`, `sync_state`,
 *  `server_stall_events_total`, `sync_acquire`, `shamap_cache_hit_rate`,
 *  `jobq_saturation`, `peer_ledger_supply`,
 *  `peerfinder_slot_census`, `amendment_block`, `nodestore_state`):
 *  this file CANNOT assert an observed gauge
 *  value, because on this build the gauges do not exist -- their registration
 *  methods and the OTel instrument members are inside
 *  `#ifdef XRPL_ENABLE_TELEMETRY`, and there is no MeterProvider at all. What
 *  is provable here, and what the tests below assert, is the complementary
 *  half: that nothing is registered and no service is consulted. The exact
 *  observed values (trusted_keys=5, quorum=4, offset=-3, the sync_state /
 *  stall-episode values, the acquire-progress / cache-hit-rate values, the
 *  per-type backlog / pool-saturation values, the peer-supply /
 *  slot-census / amendment-countdown values, and the nodestore
 *  read/write mean-latency values) are
 *  asserted in MetricMacros.cpp, which is the file compiled when telemetry IS
 *  enabled.
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
#include <type_traits>
#include <utility>

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

// -----------------------------------------------------------------
// Sync-diagnostics gauges: compile-time-disabled proof.
//
// `unl_quorum` reads ValidatorList::trustedKeyCount() and quorum();
// `clock_close_offset_seconds` reads TimeKeeper::closeOffset(); `sync_state` and
// `server_stall_events_total` read NetworkOPs and LoadManager; `sync_acquire`
// reads InboundLedgers::acquireProgress() and `shamap_cache_hit_rate` reads the
// node Family's tree-node cache; `jobq_saturation` reads
// JobQueue::getWorkerSaturation(); `peer_ledger_supply` and
// `peerfinder_slot_census` read Overlay::getPeerLedgerSupply() /
// getSlotCensus() and `amendment_block` reads
// AmendmentTable::firstUnsupportedExpected(). All are
// reached through the ServiceRegistry, and MockServiceRegistry::getValidators()
// / getTimeKeeper() / getOPs() / getLoadManager() / getInboundLedgers() /
// getNodeFamily() / getJobQueue() / getOverlay() / getAmendmentTable() THROW
// std::logic_error. So "no
// gauge callback ran" is directly observable here: had registerAsyncGauges() run
// and had a callback fired, one of those accessors would have thrown.
//
// Honest scope note: these tests do NOT assert an observed gauge value. On this
// build the gauges are not compiled at all (see the file header), so there is no
// value to read -- inventing one would be fiction. The value assertions live in
// MetricMacros.cpp. What is asserted here is the other half of the contract:
// registration is absent and no service is consulted.
// -----------------------------------------------------------------

// The observable-gauge registration surface is compiled OUT when telemetry is
// disabled: `meter()` -- the only accessor the gauges and the XRPL_METRIC_*
// macros use to reach the OTel SDK -- does not exist as a member at all. This is
// a compile-time assertion, so it fails the build (not the run) if the accessor
// ever escapes its #ifdef and drags the SDK into a telemetry-off build.
TEST_F(MetricsRegistryTest, disabled_build_exposes_no_meter_accessor)
{
    // Detects `registry.meter()` being callable. Under #ifndef
    // XRPL_ENABLE_TELEMETRY it must not be, so the trait is false.
    auto hasMeter = []<typename T>(T* r) { return requires { r->meter(); }; };
    EXPECT_FALSE(hasMeter(static_cast<telemetry::MetricsRegistry*>(nullptr)));

    // The enable flag is still queryable and reports exactly false -- the class
    // is a no-op, not an absent type.
    telemetry::MetricsRegistry const registry(false, mockApp_, j_);
    EXPECT_FALSE(registry.isEnabled());
}

// A full disabled lifecycle registers no gauge and therefore consults NO
// ServiceRegistry service. Asserting the cause, not just the absence of a crash:
// every MockServiceRegistry accessor a sync-diagnostics gauge would need throws,
// so reaching the end without an exception proves no callback ran.
TEST_F(MetricsRegistryTest, disabled_lifecycle_never_consults_gauge_services)
{
    telemetry::MetricsRegistry registry(false, mockApp_, j_);

    // start() is where registerAsyncGauges() -- and with it
    // registerUnlQuorumGauge() / registerClockSkewGauge() /
    // registerSyncStateGauge() / registerStallEventsCounter() /
    // registerSyncAcquireGauge() / registerCacheHitRateDetailGauge() /
    // registerJobQueueBacklogGauge() / registerJobQueueSaturationGauge() /
    // registerPeerLedgerSupplyGauge() / registerSlotCensusGauge() /
    // registerAmendmentBlockGauge() / registerNodeStoreGauge() --
    // would run.
    EXPECT_NO_THROW(registry.start("http://localhost:4318/v1/metrics"));

    // detachCallbacks() is the shutdown hook the real gauges honour. It must be
    // safe and idempotent even though there is nothing to detach.
    EXPECT_NO_THROW(registry.detachCallbacks());
    EXPECT_NO_THROW(registry.detachCallbacks());

    // Still disabled after a full start(): start() must not flip the flag.
    EXPECT_FALSE(registry.isEnabled());

    EXPECT_NO_THROW(registry.stop());

    // Positive control: the mock DOES throw when a gauge-backing service is
    // actually requested. Without this, "nothing threw" would be vacuous -- it
    // could mean the mock is permissive rather than that no callback ran.
    EXPECT_THROW(mockApp_.getValidators(), std::logic_error);
    EXPECT_THROW(mockApp_.getTimeKeeper(), std::logic_error);
    // The two services the WP-A2 sync-state signals read. sync_state needs both
    // (NetworkOPs for the gate/duration/ledgers-behind, LoadManager for stall
    // seconds) and server_stall_events_total needs the second, so either one
    // firing would have thrown above.
    EXPECT_THROW(mockApp_.getOPs(), std::logic_error);
    EXPECT_THROW(mockApp_.getLoadManager(), std::logic_error);
    // The two services the WP-A3 acquire signals read: sync_acquire polls the
    // in-flight acquire collection, shamap_cache_hit_rate polls the node
    // Family's tree-node cache. Neither was consulted above.
    EXPECT_THROW(mockApp_.getInboundLedgers(), std::logic_error);
    EXPECT_THROW(mockApp_.getNodeFamily(), std::logic_error);
    // The service the WP-A4 job-queue gauge reads: jobq_saturation polls
    // getWorkerSaturation() on the JobQueue. Not consulted above, so the
    // gauge never took the JobQueue mutex on a telemetry-off build.
    EXPECT_THROW(mockApp_.getJobQueue(), std::logic_error);
    // The service both WP-A7 peer gauges read: peer_ledger_supply polls
    // getPeerLedgerSupply(), which walks the active-peer list, and
    // peerfinder_slot_census polls getSlotCensus(), which takes the PeerFinder
    // lock. Both go through the Overlay, so a single throw here proves neither
    // gauge walked the peer list nor took the PeerFinder lock on a
    // telemetry-off build.
    EXPECT_THROW(mockApp_.getOverlay(), std::logic_error);
    // The service the WP-A7 amendment countdown reads: amendment_block polls
    // firstUnsupportedExpected() on the AmendmentTable, which takes that
    // table's mutex. Not consulted above, so the countdown never ran. (Its
    // `warned` half reads NetworkOPs, already covered by the getOPs() check.)
    EXPECT_THROW(mockApp_.getAmendmentTable(), std::logic_error);
    // The service the nodestore gauge reads: nodestore_state polls
    // getStoreDurationUs()/getStoreCount() and
    // getFetchDurationUs()/getFetchTotalCount() on the node-store Database,
    // alongside its I/O totals and write-queue detail. Not consulted above,
    // so the gauge never read those atomics on a telemetry-off build.
    EXPECT_THROW(mockApp_.getNodeStore(), std::logic_error);
}

// Even asking for enabled=true registers no sync-diagnostics gauge on a
// telemetry-off build. isEnabled() faithfully echoes the constructor argument
// (the flag lives outside the #ifdef), so the flag alone does NOT prove the
// gauges are inert -- the mock does: a full start()/stop() cycle with
// enabled=true still consults no service, so no callback was ever registered.
// Asserts the exact flag value on BOTH construction paths.
TEST_F(MetricsRegistryTest, enabled_flag_alone_registers_no_gauges_when_compiled_out)
{
    telemetry::MetricsRegistry enabledRequest(true, mockApp_, j_);

    // The flag is echoed back exactly, true not false: it is a plain member,
    // not gated on XRPL_ENABLE_TELEMETRY.
    EXPECT_TRUE(enabledRequest.isEnabled());

    // Yet the whole lifecycle stays inert. If registerAsyncGauges() had run and
    // registered registerUnlQuorumGauge()/registerClockSkewGauge()/
    // registerSyncStateGauge()/registerStallEventsCounter()/
    // registerSyncAcquireGauge()/registerCacheHitRateDetailGauge()/
    // registerJobQueueBacklogGauge()/registerJobQueueSaturationGauge()/
    // registerPeerLedgerSupplyGauge()/registerSlotCensusGauge()/
    // registerAmendmentBlockGauge()/registerNodeStoreGauge(), a
    // callback would reach getValidators()/getTimeKeeper()/getOPs()/
    // getLoadManager()/getInboundLedgers()/getNodeFamily()/getJobQueue()/
    // getOverlay()/getAmendmentTable()/getNodeStore() and
    // throw std::logic_error.
    EXPECT_NO_THROW(enabledRequest.start("http://localhost:4318/v1/metrics"));
    EXPECT_NO_THROW(enabledRequest.detachCallbacks());
    EXPECT_NO_THROW(enabledRequest.stop());

    // Contrast: enabled=false reports exactly false.
    telemetry::MetricsRegistry const disabledRequest(false, mockApp_, j_);
    EXPECT_FALSE(disabledRequest.isEnabled());
}

// The `state_changes_total` counter no longer has a registry-owned wrapper
// method: WP-A2 moved it to a labelled call-site macro in
// NetworkOPsImp::setMode so it can carry {from,to}. This compile-time
// assertion is the regression guard -- if someone reintroduces
// incrementStateChanges(), the unlabelled instrument would coexist with the
// labelled one and Prometheus would carry two conflicting versions of the same
// metric name.
TEST_F(MetricsRegistryTest, state_changes_counter_has_no_registry_wrapper)
{
    auto hasIncrementStateChanges = []<typename T>(T* r) {
        return requires { r->incrementStateChanges(); };
    };
    EXPECT_FALSE(hasIncrementStateChanges(static_cast<telemetry::MetricsRegistry*>(nullptr)));

    // Positive control: a sibling parity counter that WAS deliberately kept as
    // a registry wrapper is still detectable, so the trait above is really
    // probing for the method and not vacuously false.
    auto hasIncrementLedgersClosed = []<typename T>(T* r) {
        return requires { r->incrementLedgersClosed(); };
    };
    EXPECT_TRUE(hasIncrementLedgersClosed(static_cast<telemetry::MetricsRegistry*>(nullptr)));
}

#endif  // !XRPL_ENABLE_TELEMETRY

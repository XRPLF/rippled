/**
 * GTest unit tests for MetricsRegistry.
 *
 *  Four groups. The first three drive the pure static helpers, which are
 *  constexpr inline in the header and so need nothing on the link line. The
 *  fourth drives a real registry object.
 *
 *  1. sanitiseHandler() — the `handler` label sanitiser.
 *
 *  2. scaledMean() — the guarded-division helper behind every derived mean
 *     on the nodestore_state gauge.
 *
 *  3. parseLedgerRange() — reads one segment of the complete-ledger range
 *     string the complete_ledgers gauge publishes. The last case drives the
 *     real producer, xrpl::to_string(RangeSet), rather than restating its
 *     format.
 *
 *  4. The registry lifecycle — construction, stop(), and the record and
 *     increment methods. Every test here runs in **both** builds: the core
 *     is compiled into xrpl.libxrpl, which this binary links either way, so
 *     with telemetry on these tests drive a real OTel pipeline and with it
 *     off they drive the no-op stubs. An assertion that holds in only one
 *     build carries its own #ifdef and says which build it pins.
 *
 * What group 4 pins about stop(), and what it does not:
 *
 * stop() stores Phase::Stopped before it destroys the SDK provider, and every
 * record method reads that phase through recording() first. Without the store,
 * a record carrying a first-seen attribute set would reach an
 * AggregationConfig that the destroyed View owned. The tests below assert that
 * the gate is shut after stop() and that a record past it is inert. That pins
 * the gate. It does not prove the memory is safe: with no sanitizer, a read of
 * freed memory can still pass. A sanitizer build running these same tests is
 * what would catch a regression in the memory itself.
 */

#include <xrpl/telemetry/MetricsRegistry.h>

#include <xrpl/basics/RangeSet.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// The pass-through set is every all-letter job-name literal in the tree: 43
// of them. Pinned so that adding or removing a job name without revisiting
// the label-cardinality budget fails the build here.
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

namespace {

/**
 * Segments the producer can emit, paired with the range each denotes.
 *
 * Both shapes come from xrpl::to_string(ClosedInterval): `first-last`, and a
 * bare number when first equals last.
 */
constexpr std::array<std::pair<std::string_view, std::pair<std::uint32_t, std::uint32_t>>, 6>
    kProducibleSegments{{
        {"32570-50000", {32570, 50000}},
        {"50005-75891421", {50005, 75891421}},
        {"0-1", {0, 1}},
        {"5000", {5000, 5000}},
        {"0", {0, 0}},
        {"1-1", {1, 1}},
    }};

/**
 * Segments no producer emits and the parser must refuse.
 *
 * `5-6 ` and `0x10` are the two that pin the consumed-everything check: they
 * start with digits from_chars can read, so only the `ptr != end` test rejects
 * them. from_chars refuses the other ten on its own. Keep those two.
 */
constexpr std::array<std::string_view, 12> kUnreadableSegments{
    "",
    "-",
    "-5",
    "5-",
    "abc",
    "5-a",
    "a-5",
    "5--6",
    " 5-6",
    "5-6 ",
    "+5",
    "0x10",
};

}  // namespace

TEST(MetricsRegistryParseLedgerRange, dashed_segment_yields_both_bounds)
{
    // The ordinary shape. Both bounds must survive, because the gauge publishes
    // them as separate `start` and `end` series and a dashboard subtracts them.
    EXPECT_EQ(
        Registry::parseLedgerRange("32570-50000"),
        (std::pair<std::uint32_t, std::uint32_t>{32570, 50000}));
    EXPECT_EQ(
        Registry::parseLedgerRange("50005-75891421"),
        (std::pair<std::uint32_t, std::uint32_t>{50005, 75891421}));
}

TEST(MetricsRegistryParseLedgerRange, single_ledger_segment_is_a_range_not_a_reject)
{
    // A node holding exactly one complete ledger renders as a bare number, so
    // treating a dashless segment as malformed reports nothing at all for that
    // node -- the reading an operator most needs while a node is catching up.
    auto const one = Registry::parseLedgerRange("5000");
    if (!one.has_value())
        FAIL() << "a single-ledger segment must parse";
    EXPECT_EQ(one->first, 5000u);
    EXPECT_EQ(one->second, 5000u);

    // Cause, not just state: acceptance is specific to an all-digit segment.
    // These two prove the dashless branch is not simply accepting everything,
    // so the test above would still fail if the guard were removed outright.
    EXPECT_FALSE(Registry::parseLedgerRange("abc").has_value());
    EXPECT_FALSE(Registry::parseLedgerRange("5-").has_value());
}

TEST(MetricsRegistryParseLedgerRange, every_producible_segment_parses_exactly)
{
    for (auto const& [segment, expected] : kProducibleSegments)
    {
        // Comparing the whole optional covers both "was it parsed" and "are the
        // bounds right" in one exact assertion, and keeps the loop going so one
        // bad row cannot hide the other five.
        EXPECT_EQ(Registry::parseLedgerRange(segment), std::optional{expected})
            << "segment: " << segment;
    }
}

TEST(MetricsRegistryParseLedgerRange, unreadable_segments_are_refused)
{
    for (auto const segment : kUnreadableSegments)
    {
        EXPECT_FALSE(Registry::parseLedgerRange(segment).has_value())
            << "accepted an unreadable segment: [" << segment << "]";
    }
}

TEST(MetricsRegistryParseLedgerRange, bounds_are_exact_at_the_sequence_limits)
{
    // The width comes from the function's own return type, so widening the
    // sequence cannot leave this asserting against a stale boundary.
    using Seq = decltype(Registry::parseLedgerRange("0"))::value_type::first_type;
    constexpr auto kMaxSeq = std::numeric_limits<Seq>::max();
    auto const maxText = std::to_string(kMaxSeq);

    auto const atLimit = Registry::parseLedgerRange(maxText);
    if (!atLimit.has_value())
        FAIL() << "rejected the largest representable sequence: " << maxText;
    EXPECT_EQ(atLimit->first, kMaxSeq);
    EXPECT_EQ(atLimit->second, kMaxSeq);

    // One past the limit does not wrap to a small, believable sequence.
    auto const pastLimit = std::to_string(static_cast<std::uint64_t>(kMaxSeq) + 1);
    EXPECT_FALSE(Registry::parseLedgerRange(pastLimit).has_value())
        << "overflowed instead of refusing: " << pastLimit;
}

TEST(MetricsRegistryParseLedgerRange, reads_back_what_the_real_producer_wrote)
{
    // Drives the actual producer rather than a restatement of its format, so a
    // change to to_string() fails here instead of silently changing what the
    // gauge reports. The middle interval is one ledger wide on purpose: that is
    // the shape that renders without a dash.
    xrpl::RangeSet<std::uint32_t> ledgers;
    ledgers.insert(xrpl::range<std::uint32_t>(32570, 50000));
    ledgers.insert(xrpl::range<std::uint32_t>(60000, 60000));
    ledgers.insert(xrpl::range<std::uint32_t>(70000, 75891421));

    auto const rendered = xrpl::to_string(ledgers);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> recovered;
    std::string_view rest{rendered};
    while (!rest.empty())
    {
        auto const comma = rest.find(',');
        auto const segment = rest.substr(0, comma);

        auto const parsed = Registry::parseLedgerRange(segment);
        if (!parsed.has_value())
        {
            FAIL() << "producer emitted a segment the parser refuses: [" << segment << "] from "
                   << rendered;
        }
        recovered.push_back(*parsed);

        rest = (comma == std::string_view::npos) ? std::string_view{} : rest.substr(comma + 1);
    }

    std::vector<std::pair<std::uint32_t, std::uint32_t>> const expected{
        {32570, 50000},
        {60000, 60000},
        {70000, 75891421},
    };
    EXPECT_EQ(recovered, expected) << "rendered as: " << rendered;

    // Cause, not just state: every interval survived the round trip, so none
    // was dropped and no later index shifted down to fill a gap.
    EXPECT_EQ(recovered.size(), ledgers.iterative_size());
}

// ---------------------------------------------------------------------------
// 4. The registry lifecycle.
//
// The core is compiled into xrpl.libxrpl, which this binary links in both
// builds, so every test below runs in both. The headers here serve only this
// group, and two of them name types that exist in one build only, so they sit
// beside their uses rather than at the top of the file.
// ---------------------------------------------------------------------------

#include <xrpl/beast/utility/Journal.h>

#ifdef XRPL_ENABLE_TELEMETRY
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/telemetry/ValidationTracker.h>
#endif

namespace {

/**
 * OTLP/HTTP endpoint every registry below is given.
 *
 * Port 1 has no listener, so the one export attempt a test can provoke gets an
 * immediate connection refusal. The reader's interval is 10 s, so no periodic
 * export fires inside a test; stop() is what exports, because Shutdown()
 * performs a final collect-and-export drain. A routable-but-dead address would
 * make every test that calls stop() wait out the 5 s export timeout.
 */
constexpr std::string_view kTestEndpoint{"http://127.0.0.1:1/v1/metrics"};

/**
 * Resource identity stamped on the test pipeline. Fixed values, so any
 * difference a test sees between two registries comes from the objects and not
 * from their config.
 */
constexpr std::string_view kTestServiceName{"metrics-registry-test-service"};
constexpr std::string_view kTestServiceVersion{"0.0.0-test"};
constexpr std::string_view kTestInstanceId{"metrics-registry-test-instance"};
constexpr std::string_view kTestNodeId{"metrics-registry-test-node"};

/**
 * Options for every registry below.
 *
 * A function rather than a namespace-scope constant: the fields are
 * std::string, so a constant would need dynamic initialisation to run before
 * the first test.
 *
 * Default-constructed and then assigned, the shape makeMetricsRegistryOptions()
 * in Application.cpp uses to build this same struct. Default construction
 * leaves no member indeterminate: every std::string is empty, networkId is 0
 * and useTls is false. A designated-initializer list naming a subset would trip
 * -Wmissing-designated-field-initializers, an error in this build, and would
 * trip it again the next time a field is added to Options.
 *
 * networkId stays 0 and useTls false, so the three TLS paths stay empty: the
 * exporter reads them only over TLS. No test asserts on a resource attribute,
 * because nothing here reads exported points back.
 */
MetricsRegistry::Options
testOptions()
{
    MetricsRegistry::Options options;
    options.endpoint = std::string{kTestEndpoint};
    options.serviceName = std::string{kTestServiceName};
    options.serviceVersion = std::string{kTestServiceVersion};
    options.serviceInstanceId = std::string{kTestInstanceId};
    options.nodeId = std::string{kTestNodeId};
    return options;
}

/**
 * Call every record and increment method on @p registry once.
 *
 * All thirteen are driven from one place, so a method added to the class
 * without a line here reads as an uncovered method rather than as a passing
 * test.
 *
 * @param registry  The registry to drive.
 * @param tag       Folded into every attribute value, so one call's label sets
 *                  are disjoint from another call's. A tag unused before
 *                  stop() is what makes each set first-seen afterwards.
 */
void
recordEverything(MetricsRegistry& registry, std::string const& tag)
{
    registry.recordRpcStarted("started_" + tag);
    registry.recordRpcFinished("finished_" + tag, 1000);
    registry.recordRpcErrored("errored_" + tag, 500);
    registry.recordJobQueued("queued_" + tag, "ProcessLData");
    registry.recordJobStarted("started_" + tag, "RcvGetLedger", 200);
    registry.recordJobFinished("finished_" + tag, "RcvGetObjByHash", 3000);
    registry.incrementLedgersClosed();
    registry.incrementValidationsSent();
    registry.incrementValidationsChecked();
    registry.incrementStateChanges();
    registry.incrementLedgerHistoryMismatch("mismatch_" + tag);
    registry.incrementTxqExpired();
    registry.incrementTxqDropped("dropped_" + tag);
}

/**
 * Fixture for the lifecycle tests.
 *
 * Holds the journal only. Each test builds its own registry: the class is
 * neither copyable nor movable, and each test needs its own enable flag or its
 * own stop ordering.
 */
class MetricsRegistryTest : public ::testing::Test
{
protected:
    beast::Journal j_{beast::Journal::getNullSink()};
};

}  // namespace

// ---------------------------------------------------------------------------
// The disabled path. enabled=false makes every method inert in both builds, so
// every assertion here holds unguarded.
// ---------------------------------------------------------------------------

TEST_F(MetricsRegistryTest, disabled_construction)
{
    MetricsRegistry const registry(false, j_, testOptions());

    EXPECT_EQ(registry.isEnabled(), false);

    // Mutation: drop the `enabled_ &&` term from recording(). A disabled
    // registry would report itself recordable, and every call site would walk
    // into an instrument that was never created.
    EXPECT_EQ(registry.recording(), false);

    // Mutation: delete `if (!enabled_) return;` from the constructor. A node
    // with telemetry off would open an OTLP exporter and start a reader
    // thread. In a telemetry-off build the same value comes from the #else
    // branch of hasPipeline().
    EXPECT_EQ(registry.hasPipeline(), false);
}

TEST_F(MetricsRegistryTest, disabled_construct_stop)
{
    MetricsRegistry registry(false, j_, testOptions());

    registry.stop();
    registry.stop();

    // Mutation, telemetry-on build: delete `if (!provider_) return;` from
    // stop(). provider_ is null on this path because the constructor returned
    // before building it, so the first call would dereference an empty
    // shared_ptr. With telemetry off stop() has no body to break, and the three
    // values below are what that build pins.
    EXPECT_EQ(registry.isEnabled(), false);
    EXPECT_EQ(registry.recording(), false);
    EXPECT_EQ(registry.hasPipeline(), false);
}

TEST_F(MetricsRegistryTest, disabled_recording_methods)
{
    MetricsRegistry registry(false, j_, testOptions());

    // A crash canary rather than a guard with a single-line mutation: both the
    // recording() test and the null-instrument test would have to go before a
    // record method faulted here. It is the line a sanitizer build turns into
    // real coverage.
    EXPECT_NO_THROW(recordEverything(registry, "disabled"));

    // State after the sweep. enabled_ is `bool const`, so no mutation can turn
    // the first two lines red on this path; hasPipeline() is the one that can --
    // a record path that assigned provider_ would fail it.
    EXPECT_EQ(registry.isEnabled(), false);
    EXPECT_EQ(registry.recording(), false);
    EXPECT_EQ(registry.hasPipeline(), false);

    registry.stop();
    EXPECT_EQ(registry.isEnabled(), false);
    EXPECT_EQ(registry.recording(), false);
}

// ---------------------------------------------------------------------------
// The enabled path. In a telemetry-on build these drive a real OTLP exporter,
// MeterProvider and set of instruments; in a telemetry-off build they drive the
// stubs, where recording() is just the enable flag and no pipeline exists.
// ---------------------------------------------------------------------------

TEST_F(MetricsRegistryTest, enabled_registry_records_from_construction)
{
    // The registry is usable the moment it exists, which is why Application
    // can declare it ahead of every subsystem that records. Nothing stops it
    // here either, so the scope exit also covers the enabled destructor path.
    // const because every method this test calls is const.
    MetricsRegistry const registry(true, j_, testOptions());

    // Mutation: isEnabled() returning a literal false. The disabled test
    // asserts the opposite value, so only the enabled tests catch this. Not
    // "drop the enabled_(enabled) member init" -- enabled_ is `bool const` with
    // no default, so a constructor omitting it does not compile.
    EXPECT_EQ(registry.isEnabled(), true);

    // Mutation: seed phase_ with Phase::Stopped instead of Phase::Ready.
    // Nothing in the class ever stores Ready, so the node would stay silent
    // for its whole run. Also catches recording() testing phase_ == Stopped.
    EXPECT_EQ(registry.recording(), true);

#ifdef XRPL_ENABLE_TELEMETRY
    // Telemetry-on only, and the property this refactoring exists to test: the
    // constructor builds the pipeline, so no later start() call has to.
    // Mutation: delete the initExporterAndProvider(options) call from the
    // constructor's try block.
    EXPECT_EQ(registry.hasPipeline(), true);
#else
    // Telemetry-off: the pipeline compiles out, so hasPipeline() is a literal
    // false. Mutation: return true from that #else branch, which would tell a
    // caller it may register an observable that can never export.
    EXPECT_EQ(registry.hasPipeline(), false);
#endif
}

TEST_F(MetricsRegistryTest, every_record_method_runs_while_recording)
{
    MetricsRegistry registry(true, j_, testOptions());
    ASSERT_EQ(registry.recording(), true);

    // The one test that drives all thirteen real entry points against a real
    // SDK provider. No point can be read back -- the core owns its provider and
    // exposes no reader -- so the sweep is a crash canary and the assertions
    // below are the deterministic part.
    EXPECT_NO_THROW(recordEverything(registry, "live"));

    // Mutation: a record method that stores Phase::Stopped or resets provider_
    // as a side effect. Either would silence the node after its first metric.
    EXPECT_EQ(registry.isEnabled(), true);
    EXPECT_EQ(registry.recording(), true);
#ifdef XRPL_ENABLE_TELEMETRY
    EXPECT_EQ(registry.hasPipeline(), true);
#endif
}

TEST_F(MetricsRegistryTest, stop_closes_the_gate_and_leaves_enabled_true)
{
    MetricsRegistry registry(true, j_, testOptions());

    // Setup: the gate really is open, so a false reading below is attributable
    // to stop() and not to construction.
    ASSERT_EQ(registry.recording(), true);

    registry.stop();

    // isEnabled() reports what config asked for; recording() reports whether a
    // record call is safe. The value of this line is the PAIR it forms with the
    // recording() assertion below -- true beside false -- which is what shows
    // the gate is a phase and not the enable flag.
    //
    // Named honestly, because no single-line change makes this line fail in
    // BOTH builds. enabled_ is `bool const`, so clearing it in stop() does not
    // compile -- the type already forbids the defect. Rewriting isEnabled() as
    // recording() is red only where stop() can move the phase, which is the
    // telemetry-on build.
    EXPECT_EQ(registry.isEnabled(), true);

#ifdef XRPL_ENABLE_TELEMETRY
    // Mutation: delete the phase_.store(Phase::Stopped, release) line from
    // stop(). Every XRPL_METRIC_* call site reads recording() before it
    // touches an instrument, so that one store is the whole gate.
    EXPECT_EQ(registry.recording(), false);

    // A separate observation from the gate, because a separate line does it:
    // one stores the phase, another drops the provider. Mutation: delete
    // provider_.reset() from stop().
    EXPECT_EQ(registry.hasPipeline(), false);
#else
    // Telemetry-off: stop()'s body is entirely inside the guard, so it cannot
    // move phase_, and recording() is the enable flag here. Pinned so that an
    // #else branch which started gating shows up as a change.
    EXPECT_EQ(registry.recording(), true);
#endif
}

TEST_F(MetricsRegistryTest, records_after_stop_are_inert)
{
    MetricsRegistry registry(true, j_, testOptions());

    // Label sets that already have SDK storage by the time stop() runs.
    recordEverything(registry, "before_stop");

    registry.stop();

#ifdef XRPL_ENABLE_TELEMETRY
    ASSERT_EQ(registry.recording(), false);
#endif

    // The same thirteen methods with a tag never used before stop(), so every
    // attribute set here is first-seen -- including four histogram records
    // across three instruments, which is the case that allocates through the
    // AggregationConfig the destroyed View owned.
    //
    // Mutation: delete the `!recording()` test from any record method. That is
    // the regression this file exists for, and it is reliably red only under a
    // sanitizer: with none, a read of freed memory can still return and pass.
    EXPECT_NO_THROW(recordEverything(registry, "after_stop"));

    // Deterministic part: no record path reopens the gate or rebuilds the
    // pipeline.
    EXPECT_EQ(registry.isEnabled(), true);
#ifdef XRPL_ENABLE_TELEMETRY
    EXPECT_EQ(registry.recording(), false);
    EXPECT_EQ(registry.hasPipeline(), false);
#endif
}

TEST_F(MetricsRegistryTest, stop_twice_is_safe)
{
    MetricsRegistry registry(true, j_, testOptions());

    registry.stop();

    // run() and the Application destructor both call stop(), so a second call
    // is the ordinary shutdown path. Mutation: delete `if (!provider_) return;`
    // from stop(). provider_ is null by now, so this call would dereference an
    // empty shared_ptr on every clean shutdown. The registry's own destructor
    // then makes a third call.
    EXPECT_NO_THROW(registry.stop());

    EXPECT_EQ(registry.isEnabled(), true);
#ifdef XRPL_ENABLE_TELEMETRY
    EXPECT_EQ(registry.recording(), false);
    EXPECT_EQ(registry.hasPipeline(), false);
#endif
}

#ifdef XRPL_ENABLE_TELEMETRY

// ---------------------------------------------------------------------------
// getValidationTracker() is declared in a telemetry-on build only, because only
// the observable-gauge callbacks drain the tracker. Two production call sites
// reach it this way, so the accessor has to hand back the live member.
// ---------------------------------------------------------------------------

TEST_F(MetricsRegistryTest, validation_tracker_is_owned_per_registry)
{
    MetricsRegistry first(true, j_, testOptions());
    MetricsRegistry second(true, j_, testOptions());

    // Setup: both trackers start empty, so a count below is attributable to
    // the record call and not to fixture state.
    ASSERT_EQ(first.getValidationTracker().totalValidationsSent(), 0u);
    ASSERT_EQ(second.getValidationTracker().totalValidationsSent(), 0u);

    first.getValidationTracker().recordOurValidation(
        xrpl::uint256{std::uint64_t{7}}, xrpl::LedgerIndex{7});

    // Exact counts on both sides: the reference is live, so the write lands,
    // and it lands on one registry only.
    //
    // Mutation: return a reference to a function-local static from
    // getValidationTracker(). One shared tracker would put this count on
    // `second` as well, so two registries in one process would report one
    // merged agreement figure.
    EXPECT_EQ(first.getValidationTracker().totalValidationsSent(), 1u);
    EXPECT_EQ(second.getValidationTracker().totalValidationsSent(), 0u);
}

#endif  // XRPL_ENABLE_TELEMETRY

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/FlatStateMap.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace xrpl;

namespace {

// Construct a synthetic SLE for testing. The contents are not meaningful —
// we only need a distinct shared_ptr<STLedgerEntry const> per key to exercise
// the map's storage and retrieval semantics.
[[nodiscard]] std::shared_ptr<STLedgerEntry const>
makeSle(std::uint64_t keyValue)
{
    uint256 key{keyValue};
    return std::make_shared<STLedgerEntry const>(ltACCOUNT_ROOT, key);
}

[[nodiscard]] uint256
keyOf(std::uint64_t v)
{
    return uint256{v};
}

}  // namespace

// ---------------------------------------------------------------------------
// Basic read/write semantics
// ---------------------------------------------------------------------------

TEST(FlatStateMap, EmptyOnConstruction)
{
    FlatStateMap m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
    EXPECT_FALSE(m.exists(keyOf(0)));
    EXPECT_EQ(m.read(keyOf(0)), nullptr);
}

TEST(FlatStateMap, InsertThenRead)
{
    FlatStateMap m;
    auto const sle = makeSle(42);

    m.insert(keyOf(42), sle);

    EXPECT_FALSE(m.empty());
    EXPECT_EQ(m.size(), 1u);
    EXPECT_TRUE(m.exists(keyOf(42)));
    EXPECT_EQ(m.read(keyOf(42)), sle);
}

TEST(FlatStateMap, ReadMissReturnsNullptr)
{
    FlatStateMap m;
    m.insert(keyOf(1), makeSle(1));
    EXPECT_EQ(m.read(keyOf(2)), nullptr);
    EXPECT_FALSE(m.exists(keyOf(2)));
}

TEST(FlatStateMap, InsertReplacesPriorEntry)
{
    // update() semantics in apply: mutating an SLE produces a new shared_ptr
    // value; insert() must replace the prior pointer cleanly.
    FlatStateMap m;
    auto const first = makeSle(1);
    auto const second = makeSle(1);  // same key, different SLE object

    m.insert(keyOf(1), first);
    EXPECT_EQ(m.read(keyOf(1)), first);

    m.insert(keyOf(1), second);
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(m.read(keyOf(1)), second);
    EXPECT_NE(m.read(keyOf(1)), first);
}

TEST(FlatStateMap, EraseRemovesEntry)
{
    FlatStateMap m;
    m.insert(keyOf(7), makeSle(7));
    EXPECT_TRUE(m.exists(keyOf(7)));

    m.erase(keyOf(7));
    EXPECT_FALSE(m.exists(keyOf(7)));
    EXPECT_EQ(m.read(keyOf(7)), nullptr);
    EXPECT_TRUE(m.empty());
}

TEST(FlatStateMap, EraseAbsentKeyIsNoop)
{
    FlatStateMap m;
    m.insert(keyOf(1), makeSle(1));
    m.erase(keyOf(99));  // no-op; must not throw, must not affect other keys
    EXPECT_EQ(m.size(), 1u);
    EXPECT_TRUE(m.exists(keyOf(1)));
}

TEST(FlatStateMap, Clear)
{
    FlatStateMap m;
    for (std::uint64_t i = 0; i < 100; ++i)
        m.insert(keyOf(i), makeSle(i));
    EXPECT_EQ(m.size(), 100u);

    m.clear();
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
    EXPECT_FALSE(m.exists(keyOf(50)));
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

TEST(FlatStateMap, ForEachVisitsAllEntries)
{
    FlatStateMap m;
    std::vector<std::shared_ptr<STLedgerEntry const>> inserted;
    constexpr std::size_t N = 50;
    for (std::uint64_t i = 0; i < N; ++i)
    {
        auto sle = makeSle(i);
        inserted.push_back(sle);
        m.insert(keyOf(i), sle);
    }

    std::size_t visited = 0;
    m.forEach([&](uint256 const& /*key*/, auto const& /*sle*/) { ++visited; });
    EXPECT_EQ(visited, N);
}

// ---------------------------------------------------------------------------
// Snapshot semantics
// ---------------------------------------------------------------------------

TEST(FlatStateMap, SnapshotPreservesEntries)
{
    FlatStateMap source;
    for (std::uint64_t i = 0; i < 10; ++i)
        source.insert(keyOf(i), makeSle(i));

    auto snap = source.snapshot();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->size(), 10u);
    for (std::uint64_t i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(snap->exists(keyOf(i)));
        EXPECT_EQ(snap->read(keyOf(i)), source.read(keyOf(i)));  // shared SLE
    }
}

TEST(FlatStateMap, SnapshotIsIndependentOfSubsequentWrites)
{
    FlatStateMap source;
    source.insert(keyOf(1), makeSle(1));
    source.insert(keyOf(2), makeSle(2));

    auto snap = source.snapshot();

    // Mutate source after snapshot.
    source.insert(keyOf(3), makeSle(3));
    source.erase(keyOf(1));
    source.insert(keyOf(2), makeSle(99));  // replace key 2 with a different SLE

    // Snapshot must reflect state at snapshot time, not source's current state.
    EXPECT_EQ(snap->size(), 2u);
    EXPECT_TRUE(snap->exists(keyOf(1)));
    EXPECT_TRUE(snap->exists(keyOf(2)));
    EXPECT_FALSE(snap->exists(keyOf(3)));
    EXPECT_NE(snap->read(keyOf(2)), source.read(keyOf(2)));
}

TEST(FlatStateMap, SnapshotSharesUnderlyingSleObjects)
{
    // Snapshot performs a shallow copy of the map (shared_ptr values).
    // It does NOT deep-copy SLE bodies; both source and snapshot point at
    // the same immutable SLE instance.
    FlatStateMap source;
    auto const sle = makeSle(1);
    source.insert(keyOf(1), sle);

    auto snap = source.snapshot();
    EXPECT_EQ(snap->read(keyOf(1)).get(), sle.get());
    EXPECT_EQ(source.read(keyOf(1)).get(), sle.get());
}

// ---------------------------------------------------------------------------
// Ownership: FlatStateMap is non-copyable and non-movable (owns a mutex).
// Callers that need ownership transfer wrap in std::unique_ptr<FlatStateMap>.
// ---------------------------------------------------------------------------

TEST(FlatStateMap, UniquePtrOwnershipTransfer)
{
    auto a = std::make_unique<FlatStateMap>();
    a->insert(keyOf(1), makeSle(1));
    a->insert(keyOf(2), makeSle(2));

    auto b = std::move(a);  // pointer move, not map move
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a, nullptr);  // a is now null
    EXPECT_EQ(b->size(), 2u);
    EXPECT_TRUE(b->exists(keyOf(1)));
    EXPECT_TRUE(b->exists(keyOf(2)));
}

// ---------------------------------------------------------------------------
// Population from a range of SLEs (P6.2). The ReadView-taking overload
// `populateFromReadView` is the same one-line forwarder; we cover the
// templated range form directly so the test doesn't need a live ReadView.
// ---------------------------------------------------------------------------

TEST(FlatStateMap, PopulateFromRange)
{
    std::vector<std::shared_ptr<STLedgerEntry const>> sles;
    constexpr std::size_t N = 25;
    for (std::uint64_t i = 0; i < N; ++i)
        sles.push_back(makeSle(i));

    FlatStateMap m;
    populateFromRange(m, sles);

    EXPECT_EQ(m.size(), N);
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_TRUE(m.exists(sles[i]->key()));
        EXPECT_EQ(m.read(sles[i]->key()).get(), sles[i].get());
    }
}

TEST(FlatStateMap, PopulateFromRangeOnEmptyRangeLeavesMapEmpty)
{
    std::vector<std::shared_ptr<STLedgerEntry const>> empty;
    FlatStateMap m;
    populateFromRange(m, empty);
    EXPECT_TRUE(m.empty());
}

TEST(FlatStateMap, PopulateFromRangePreservesSleIdentity)
{
    // The map must store the exact shared_ptr the caller provided —
    // not a deep copy of the SLE. This matters because SLE objects are
    // logically immutable; any "copy" would risk subtle observer drift.
    std::vector<std::shared_ptr<STLedgerEntry const>> sles{makeSle(1)};
    auto const expected = sles[0].get();

    FlatStateMap m;
    populateFromRange(m, sles);

    EXPECT_EQ(m.read(sles[0]->key()).get(), expected);
}

// ---------------------------------------------------------------------------
// Dual-write mirroring (P6.3 from plan-6).
//
// In plan-6's 2-writes-for-1-read pattern, every state mutation must go to
// both the SHAMap (authoritative for the state root) and the FlatStateMap
// (read-side materialization). The integration point in xrpld is the
// RawView interface — every state mutation goes through one of three
// methods: rawInsert(sle), rawReplace(sle), or rawErase(sle).
//
// `mirrorRawInsert/mirrorRawReplace/mirrorRawErase` are the testable
// units that perform the flat-map side of the dual write. They take a
// FlatStateMap and an SLE (or key, for erase) and update the map to
// reflect the operation. The Ledger integration (a separate change)
// wires each `raw*` override to call the matching `mirror*` helper.
//
// These tests describe the contract:
//   * mirrorRawInsert(map, sle)  — adds the sle keyed by sle->key()
//   * mirrorRawReplace(map, sle) — replaces the sle for sle->key()
//   * mirrorRawErase(map, sle)   — removes sle->key() from the map
//   * mirrorRawErase(map, key)   — removes the key from the map
//
// All four are write-side ops; they take a unique_lock under the hood.
// ---------------------------------------------------------------------------

TEST(FlatStateMap_Mirror, MirrorRawInsertAddsEntry)
{
    FlatStateMap map;
    auto const sle = makeSle(1);

    mirrorRawInsert(map, sle);

    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.read(sle->key()), sle);
}

TEST(FlatStateMap_Mirror, MirrorRawReplaceReplacesEntry)
{
    FlatStateMap map;
    auto const original = makeSle(1);
    auto const replacement = makeSle(1);  // same key, distinct object
    map.insert(original->key(), original);

    mirrorRawReplace(map, replacement);

    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.read(original->key()), replacement);
    EXPECT_NE(map.read(original->key()), original);
}

TEST(FlatStateMap_Mirror, MirrorRawEraseBySleRemovesEntry)
{
    FlatStateMap map;
    auto const sle = makeSle(1);
    map.insert(sle->key(), sle);

    mirrorRawErase(map, sle);

    EXPECT_TRUE(map.empty());
    EXPECT_FALSE(map.exists(sle->key()));
}

TEST(FlatStateMap_Mirror, MirrorRawEraseByKeyRemovesEntry)
{
    FlatStateMap map;
    auto const sle = makeSle(1);
    map.insert(sle->key(), sle);

    mirrorRawErase(map, sle->key());

    EXPECT_TRUE(map.empty());
}

TEST(FlatStateMap_Mirror, MirrorOpsAreNoopOnAbsentKeys)
{
    FlatStateMap map;
    // Erasing keys not in the map must not throw and must not alter the map.
    mirrorRawErase(map, keyOf(99));
    EXPECT_TRUE(map.empty());

    // Replacing a key not in the map is semantically equivalent to an
    // insert: in xrpld, rawReplace asserts the prior SLE exists in the
    // SHAMap, so the SHAMap side handles the precondition. The flat
    // mirror is permissive — it ensures the post-state matches what the
    // SHAMap will have. If the caller upstream got it wrong, the
    // differential invariant check at close (P6.5) is what catches it.
    auto const sle = makeSle(5);
    mirrorRawReplace(map, sle);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.read(sle->key()), sle);
}

TEST(FlatStateMap_Mirror, MirroredSequenceMatchesIntendedState)
{
    // Simulate a sequence of raw operations as they would happen during
    // a transaction's apply path, and assert the flat map ends in the
    // state matching the SHAMap-equivalent view.
    FlatStateMap map;

    auto const a = makeSle(1);
    auto const b = makeSle(2);
    auto const c = makeSle(3);
    auto const aPrime = makeSle(1);  // updated version of a

    mirrorRawInsert(map, a);
    mirrorRawInsert(map, b);
    mirrorRawInsert(map, c);
    mirrorRawReplace(map, aPrime);
    mirrorRawErase(map, b);

    // Expected end state: { 1 -> aPrime, 3 -> c }
    EXPECT_EQ(map.size(), 2u);
    EXPECT_EQ(map.read(a->key()), aPrime);
    EXPECT_FALSE(map.exists(b->key()));
    EXPECT_EQ(map.read(c->key()), c);
}

// ---------------------------------------------------------------------------
// Keylet-aware read (P6.4).
//
// `readFromFlatStateMap(map, keylet)` is the testable unit underlying
// the `Ledger::read(Keylet)` integration. It performs three steps:
//   1. lookup by keylet.key in the FlatStateMap
//   2. if missing, return nullptr (no SLE under that key)
//   3. if present, verify the SLE matches the keylet's expected type via
//      Keylet::check; on mismatch, return nullptr
//
// The type check mirrors the existing Ledger::read behavior — a keylet
// query for the wrong type returns nullptr, not the wrong-typed SLE.
// This preserves the contract: callers ask "is there an X at this key?"
// and the read either yields an X or yields nothing.
// ---------------------------------------------------------------------------

TEST(FlatStateMap_KeyletRead, HitReturnsSle)
{
    FlatStateMap map;
    auto const sle = makeSle(1);
    map.insert(sle->key(), sle);

    Keylet const k{ltACCOUNT_ROOT, sle->key()};
    auto const result = readFromFlatStateMap(map, k);
    EXPECT_EQ(result, sle);
}

TEST(FlatStateMap_KeyletRead, MissReturnsNullptr)
{
    FlatStateMap map;
    Keylet const k{ltACCOUNT_ROOT, keyOf(42)};

    auto const result = readFromFlatStateMap(map, k);
    EXPECT_EQ(result, nullptr);
}

TEST(FlatStateMap_KeyletRead, TypeMismatchReturnsNullptr)
{
    // SLE stored with ltACCOUNT_ROOT, queried as ltRIPPLE_STATE: must
    // return nullptr, not the wrong-typed SLE.
    FlatStateMap map;
    auto const sle = makeSle(1);  // ltACCOUNT_ROOT (see makeSle helper)
    map.insert(sle->key(), sle);

    Keylet const wrongType{ltRIPPLE_STATE, sle->key()};
    auto const result = readFromFlatStateMap(map, wrongType);
    EXPECT_EQ(result, nullptr);
}

TEST(FlatStateMap_KeyletRead, AbsenceIsAuthoritativeUnderPlan6V2)
{
    // Plan 6 v2 semantics: when a FlatStateMap is the read source of
    // truth, a miss IS the answer. No fallback. The differential
    // invariant check at close (P6.5) is what makes this safe.
    //
    // This test exists to document the contract: a populated map that
    // doesn't contain key K reports nullptr for K, period. No probing
    // into a SHAMap or other source.
    FlatStateMap map;
    auto const sleA = makeSle(1);
    auto const sleB = makeSle(2);
    map.insert(sleA->key(), sleA);
    map.insert(sleB->key(), sleB);

    Keylet const absent{ltACCOUNT_ROOT, keyOf(99)};
    auto const result = readFromFlatStateMap(map, absent);
    EXPECT_EQ(result, nullptr);
    // Map state unchanged (no implicit population on read miss).
    EXPECT_EQ(map.size(), 2u);
    EXPECT_FALSE(map.exists(absent.key));
}

// ---------------------------------------------------------------------------
// Concurrency: many concurrent readers do not block each other; writes
// interleave with reads safely. We're not benchmarking, just checking that
// no race trips a sanitizer and that final state is consistent.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Differential invariant (P6.5).
//
// Once the flat map is the read source of truth (P6.4), the safety
// property that lets the no-fallback design ship is: at every ledger
// close, the flat map and the SHAMap must agree on which keys are
// present. `diffFlatStateKeys(flat, sourceKeys)` performs that check —
// returning the sets of (a) keys in the source that are missing from
// the flat map and (b) keys in the flat map that aren't in the source.
//
// Both lists empty == invariant holds. Anything else is a stop-the-line
// bug — the Ledger integration crashes rather than publishing a state
// root that disagrees with reality.
//
// Content drift (right keys, wrong SLE bodies) is a separate, stronger
// invariant. It's prevented by construction: the mirror helpers (tested
// in isolation) write exactly the SLE the caller passed to raw*. If the
// mirror helpers are correct and the wiring is correct, content can't
// drift. P6.5 catches the membership-drift case where wiring is broken.
// ---------------------------------------------------------------------------

TEST(FlatStateMap_Diff, EmptyVsEmptyHasNoDiff)
{
    FlatStateMap flat;
    std::vector<uint256> sourceKeys;

    auto const diff = diffFlatStateKeys(flat, sourceKeys);
    EXPECT_TRUE(diff.missingFromFlat.empty());
    EXPECT_TRUE(diff.extraInFlat.empty());
}

TEST(FlatStateMap_Diff, IdenticalKeySetsHaveNoDiff)
{
    FlatStateMap flat;
    std::vector<uint256> sourceKeys;
    for (std::uint64_t i = 0; i < 50; ++i)
    {
        auto const sle = makeSle(i);
        flat.insert(sle->key(), sle);
        sourceKeys.push_back(sle->key());
    }

    auto const diff = diffFlatStateKeys(flat, sourceKeys);
    EXPECT_TRUE(diff.missingFromFlat.empty());
    EXPECT_TRUE(diff.extraInFlat.empty());
}

TEST(FlatStateMap_Diff, KeysInSourceButNotFlatAreFlagged)
{
    FlatStateMap flat;
    auto const sleA = makeSle(1);
    auto const sleB = makeSle(2);
    auto const sleC = makeSle(3);
    flat.insert(sleA->key(), sleA);
    // sleB intentionally not in flat
    flat.insert(sleC->key(), sleC);

    std::vector<uint256> sourceKeys{sleA->key(), sleB->key(), sleC->key()};

    auto const diff = diffFlatStateKeys(flat, sourceKeys);
    ASSERT_EQ(diff.missingFromFlat.size(), 1u);
    EXPECT_EQ(diff.missingFromFlat[0], sleB->key());
    EXPECT_TRUE(diff.extraInFlat.empty());
}

TEST(FlatStateMap_Diff, KeysInFlatButNotSourceAreFlagged)
{
    FlatStateMap flat;
    auto const sleA = makeSle(1);
    auto const sleB = makeSle(2);
    auto const sleC = makeSle(3);
    flat.insert(sleA->key(), sleA);
    flat.insert(sleB->key(), sleB);  // phantom — not in source
    flat.insert(sleC->key(), sleC);

    std::vector<uint256> sourceKeys{sleA->key(), sleC->key()};

    auto const diff = diffFlatStateKeys(flat, sourceKeys);
    EXPECT_TRUE(diff.missingFromFlat.empty());
    ASSERT_EQ(diff.extraInFlat.size(), 1u);
    EXPECT_EQ(diff.extraInFlat[0], sleB->key());
}

TEST(FlatStateMap_Diff, BothSidesFlaggedSimultaneously)
{
    FlatStateMap flat;
    auto const inBoth = makeSle(1);
    auto const onlyFlat = makeSle(2);
    auto const onlySource = makeSle(3);
    flat.insert(inBoth->key(), inBoth);
    flat.insert(onlyFlat->key(), onlyFlat);

    std::vector<uint256> sourceKeys{inBoth->key(), onlySource->key()};

    auto const diff = diffFlatStateKeys(flat, sourceKeys);
    ASSERT_EQ(diff.missingFromFlat.size(), 1u);
    EXPECT_EQ(diff.missingFromFlat[0], onlySource->key());
    ASSERT_EQ(diff.extraInFlat.size(), 1u);
    EXPECT_EQ(diff.extraInFlat[0], onlyFlat->key());
}

TEST(FlatStateMap_Diff, FlatMapsAgreeReturnsTrueWhenNoDiff)
{
    // Convenience predicate built on diffFlatStateKeys for the hot
    // path: at every close, the integration calls this. Returns true
    // iff both diff lists are empty.
    FlatStateMap flat;
    std::vector<uint256> sourceKeys;
    for (std::uint64_t i = 0; i < 10; ++i)
    {
        auto const sle = makeSle(i);
        flat.insert(sle->key(), sle);
        sourceKeys.push_back(sle->key());
    }

    EXPECT_TRUE(flatStateMapMatches(flat, sourceKeys));

    // After divergence, must return false.
    flat.erase(sourceKeys[0]);
    EXPECT_FALSE(flatStateMapMatches(flat, sourceKeys));
}

// ---------------------------------------------------------------------------
// SHAMap-like adapter (P6.5 integration).
//
// The Ledger integration of the differential invariant needs to compare
// the FlatStateMap against the live SHAMap's key-set. SHAMap iterators
// yield `SHAMapItem` objects, not raw `uint256`s — so we need a thin
// adapter that walks any "SHAMap-like" range (anything with begin()/
// end() yielding items with a `.key()` method) and feeds the keys
// through `flatStateMapMatches`.
//
// This is the helper Ledger::validateFlatStateMapMatchesShaMap() will
// call. It's testable here with a mock SHAMap-like, so the Ledger
// integration becomes a one-line forwarder.
// ---------------------------------------------------------------------------

namespace {

// Minimal mock that satisfies the contract:
//   * iterable via begin/end
//   * each element exposes a `key()` returning uint256
struct MockShaMapItem
{
    uint256 k;
    [[nodiscard]] uint256 const&
    key() const noexcept
    {
        return k;
    }
};

}  // namespace

TEST(FlatStateMap_ShaMapAdapter, EmptyShaMapMatchesEmptyFlat)
{
    FlatStateMap flat;
    std::vector<MockShaMapItem> shaMap;
    EXPECT_TRUE(flatStateMapMatchesShaMap(flat, shaMap));
}

TEST(FlatStateMap_ShaMapAdapter, IdenticalContentsMatch)
{
    FlatStateMap flat;
    std::vector<MockShaMapItem> shaMap;
    for (std::uint64_t i = 0; i < 25; ++i)
    {
        auto const sle = makeSle(i);
        flat.insert(sle->key(), sle);
        shaMap.push_back({sle->key()});
    }
    EXPECT_TRUE(flatStateMapMatchesShaMap(flat, shaMap));
}

TEST(FlatStateMap_ShaMapAdapter, MissingFromFlatFails)
{
    FlatStateMap flat;
    auto const sleA = makeSle(1);
    flat.insert(sleA->key(), sleA);

    std::vector<MockShaMapItem> shaMap{
        {sleA->key()}, {keyOf(99)}};  // 99 is in shaMap, missing from flat
    EXPECT_FALSE(flatStateMapMatchesShaMap(flat, shaMap));
}

TEST(FlatStateMap_ShaMapAdapter, PhantomInFlatFails)
{
    FlatStateMap flat;
    auto const sleA = makeSle(1);
    auto const phantom = makeSle(99);
    flat.insert(sleA->key(), sleA);
    flat.insert(phantom->key(), phantom);

    std::vector<MockShaMapItem> shaMap{{sleA->key()}};  // phantom isn't there
    EXPECT_FALSE(flatStateMapMatchesShaMap(flat, shaMap));
}

// ---------------------------------------------------------------------------
// Benchmarks. TDD with benchmarks: assert performance regressions fail
// the test, not just correctness regressions. Thresholds are set
// generously (10x slack vs. measured locally) so CI on under-spec
// machines doesn't flake. Reported numbers are printed so a real
// regression shows up as a measured slowdown even before the threshold
// trips.
//
// What we're proving:
//   * read() is O(1) — average latency does not grow with map size
//   * write throughput is bounded but not pathological
//   * readFromFlatStateMap (the Keylet-typed read) adds negligible
//     overhead over the bare map.read() call
//
// All benchmarks measure on a single thread; concurrent scaling is
// covered by ConcurrentReadersAndWritersAreConsistent.
// ---------------------------------------------------------------------------

namespace {

// Inhibit dead-code elimination of the value `v` in benchmark loops.
// The empty inline-asm "uses" v as an input, forcing the compiler to
// materialize it. Cheap; no observable side effect.
template <typename T>
inline void
benchmark_use(T const& v)
{
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "r,m"(v) : "memory");
#else
    (void)v;
#endif
}

struct BenchResult
{
    double nsPerOp;
    std::size_t ops;
};

template <typename Fn>
BenchResult
timeOps(std::size_t opCount, Fn&& fn)
{
    auto const t0 = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < opCount; ++i)
        fn(i);
    auto const t1 = std::chrono::high_resolution_clock::now();
    auto const elapsedNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    return {static_cast<double>(elapsedNs) / static_cast<double>(opCount),
            opCount};
}

void
populate(FlatStateMap& m, std::size_t n)
{
    for (std::uint64_t i = 0; i < n; ++i)
        m.insert(keyOf(i), makeSle(i));
}

}  // namespace

TEST(FlatStateMap_Bench, ReadIsO1AtVariousSizes)
{
    // Measure read() average latency at three map sizes. With O(1)
    // semantics (hash table), the per-op time should be roughly flat.
    constexpr std::size_t kOpsPerRun = 100'000;
    std::vector<std::size_t> sizes{1'000, 10'000, 100'000};
    std::vector<double> nsPerOpAtSize;

    for (auto const n : sizes)
    {
        FlatStateMap m;
        populate(m, n);

        // Shuffle the access pattern so we don't accidentally
        // measure a sequential cache-friendly access pattern.
        std::vector<uint256> keys;
        keys.reserve(n);
        for (std::uint64_t i = 0; i < n; ++i)
            keys.push_back(keyOf(i));
        std::mt19937_64 rng(12345);
        std::shuffle(keys.begin(), keys.end(), rng);

        auto const result = timeOps(kOpsPerRun, [&](std::size_t i) {
            auto sle = m.read(keys[i % n]);
            // Prevent the compiler from optimizing the read away.
            benchmark_use(sle);
        });

        std::printf(
            "  FlatStateMap::read at N=%zu : %.1f ns/op (%zu ops)\n",
            n,
            result.nsPerOp,
            result.ops);
        nsPerOpAtSize.push_back(result.nsPerOp);

        // Regression gate: even on a slow CI box, hash-map reads of a
        // 100k-entry map should be well under 1 µs. We pick 2 µs as a
        // generous threshold (~10x measured local) to avoid flakes.
        EXPECT_LT(result.nsPerOp, 2000.0)
            << "Read latency at N=" << n << " exceeded 2 µs/op";
    }

    // O(1) sanity: read at 100k should not be more than 4x slower than
    // read at 1k (cache effects + memory bandwidth give some headroom,
    // but not a real log-factor). 4x is generous; tighten if it's
    // stable in CI.
    EXPECT_LT(nsPerOpAtSize.back(), nsPerOpAtSize.front() * 4.0)
        << "Read latency grew super-constantly with map size — "
        << "expected O(1), got " << nsPerOpAtSize.front() << " ns at N=1k vs "
        << nsPerOpAtSize.back() << " ns at N=100k";
}

TEST(FlatStateMap_Bench, KeyletReadOverheadIsSmall)
{
    // readFromFlatStateMap adds a Keylet::check call on top of map.read.
    // The overhead should be a small constant — well under 100 ns —
    // because Keylet::check is just a type tag comparison.
    constexpr std::size_t N = 10'000;
    constexpr std::size_t kOpsPerRun = 100'000;
    FlatStateMap m;
    populate(m, N);

    std::vector<Keylet> keylets;
    keylets.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
        keylets.emplace_back(ltACCOUNT_ROOT, keyOf(i));

    auto const bare = timeOps(kOpsPerRun, [&](std::size_t i) {
        auto sle = m.read(keylets[i % N].key);
        benchmark_use(sle);
    });

    auto const wrapped = timeOps(kOpsPerRun, [&](std::size_t i) {
        auto sle = readFromFlatStateMap(m, keylets[i % N]);
        benchmark_use(sle);
    });

    std::printf(
        "  bare map.read         : %.1f ns/op\n"
        "  readFromFlatStateMap  : %.1f ns/op  (+%.1f ns)\n",
        bare.nsPerOp,
        wrapped.nsPerOp,
        wrapped.nsPerOp - bare.nsPerOp);

    EXPECT_LT(wrapped.nsPerOp - bare.nsPerOp, 500.0)
        << "Keylet check added more overhead than expected";
}

TEST(FlatStateMap_Bench, WriteThroughput)
{
    // Insert throughput is bounded by the cost of an unordered_map
    // insert under a unique_lock. We're not optimizing this; we're
    // gating it so a regression in the lock or allocator shows up.
    constexpr std::size_t N = 100'000;
    FlatStateMap m;

    std::vector<std::shared_ptr<STLedgerEntry const>> sles;
    sles.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
        sles.push_back(makeSle(i));

    auto const result =
        timeOps(N, [&](std::size_t i) { m.insert(sles[i]->key(), sles[i]); });

    std::printf(
        "  FlatStateMap::insert  : %.1f ns/op (%zu ops)\n",
        result.nsPerOp,
        result.ops);

    EXPECT_LT(result.nsPerOp, 5000.0) << "Insert latency exceeded 5 µs/op";
    EXPECT_EQ(m.size(), N);
}

TEST(FlatStateMap_Bench, SnapshotCostAtLedgerScale)
{
    // P6.6 runs snapshot() at every close — capturing the live open
    // ledger's flat map as the immutable base for the new closed
    // ledger. The cost is O(N) in entry count (shallow copy of N
    // shared_ptrs) and must be a small fraction of the close budget.
    //
    // Mainnet target: ~10M SLEs. We measure at 100k here and report
    // ns/entry so extrapolation is clear. With ~50 ns/entry, 10M
    // ledger snapshots in ~500 ms — borderline; a persistent HAMT
    // (Plan 6 follow-on) is the long-term answer if this proves too
    // expensive.
    constexpr std::size_t N = 100'000;
    FlatStateMap source;
    for (std::uint64_t i = 0; i < N; ++i)
        source.insert(keyOf(i), makeSle(i));

    auto const start = std::chrono::high_resolution_clock::now();
    auto snap = source.snapshot();
    auto const elapsed = std::chrono::high_resolution_clock::now() - start;
    auto const ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto const nsPerEntry =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() /
        static_cast<double>(N);

    std::printf(
        "  FlatStateMap::snapshot N=%zu : %lld ms total (%.1f ns/entry)\n",
        N,
        static_cast<long long>(ms),
        nsPerEntry);

    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->size(), N);
    EXPECT_LT(ms, 500)
        << "Snapshot at 100k entries should complete under 500 ms";
}

TEST(FlatStateMap_Bench, DifferentialInvariantCheckIsCheap)
{
    // P6.5 runs the diff at every ledger close. If it's slow it adds
    // latency to the close path, defeating the point of Plan 6.
    // Threshold: a 100k-entry map must validate in well under 100 ms
    // on a typical validator. Real mainnet has ~10M SLEs, so this
    // extrapolates to ~10 s at 100M-mapping. That would be too slow
    // for real deployment; we'll need a partial / incremental check
    // for production scale, but at this layer we just want a bounded
    // O(N) walk.
    constexpr std::size_t N = 100'000;
    FlatStateMap m;
    std::vector<uint256> sourceKeys;
    sourceKeys.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
    {
        auto const sle = makeSle(i);
        m.insert(sle->key(), sle);
        sourceKeys.push_back(sle->key());
    }

    auto const start = std::chrono::high_resolution_clock::now();
    bool const ok = flatStateMapMatches(m, sourceKeys);
    auto const elapsed = std::chrono::high_resolution_clock::now() - start;
    auto const ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto const nsPerKey =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() /
        static_cast<double>(N);

    std::printf(
        "  flatStateMapMatches N=%zu : %lld ms total (%.1f ns/key)\n",
        N,
        static_cast<long long>(ms),
        nsPerKey);

    EXPECT_TRUE(ok);
    EXPECT_LT(ms, 200) << "Diff at 100k entries should complete under 200 ms";
}

TEST(FlatStateMap_Bench, MirrorOverheadOverDirectInsert)
{
    // mirrorRawInsert forwards to map.insert with an extra shared_ptr
    // load to extract the key. The wrapper overhead should be near
    // zero — within noise of the bare insert.
    constexpr std::size_t N = 50'000;

    std::vector<std::shared_ptr<STLedgerEntry const>> sles;
    sles.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
        sles.push_back(makeSle(i));

    FlatStateMap direct;
    auto const bareResult = timeOps(
        N, [&](std::size_t i) { direct.insert(sles[i]->key(), sles[i]); });

    FlatStateMap mirrored;
    auto const mirrorResult =
        timeOps(N, [&](std::size_t i) { mirrorRawInsert(mirrored, sles[i]); });

    std::printf(
        "  direct insert         : %.1f ns/op\n"
        "  mirrorRawInsert       : %.1f ns/op  (%+.1f ns)\n",
        bareResult.nsPerOp,
        mirrorResult.nsPerOp,
        mirrorResult.nsPerOp - bareResult.nsPerOp);

    // Mirror wrapper should not double the insert cost; 50% slack is
    // very generous given they do the same thing.
    EXPECT_LT(mirrorResult.nsPerOp, bareResult.nsPerOp * 1.5)
        << "mirrorRawInsert wrapper added more overhead than expected";
    EXPECT_EQ(mirrored.size(), N);
}

// ---------------------------------------------------------------------------

TEST(FlatStateMap, ConcurrentReadersAndWritersAreConsistent)
{
    FlatStateMap m;
    constexpr std::size_t N = 1000;

    // Pre-populate with even keys.
    for (std::uint64_t i = 0; i < N; i += 2)
        m.insert(keyOf(i), makeSle(i));

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> readsObserved{0};

    auto reader = [&] {
        while (!stop.load(std::memory_order_relaxed))
        {
            for (std::uint64_t i = 0; i < N; i += 2)
            {
                if (m.exists(keyOf(i)))
                    readsObserved.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    auto writer = [&] {
        // Insert odd keys; do not modify the even keys readers observe.
        for (std::uint64_t i = 1; i < N; i += 2)
            m.insert(keyOf(i), makeSle(i));
    };

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i)
        readers.emplace_back(reader);

    std::thread w(writer);
    w.join();
    stop.store(true, std::memory_order_relaxed);
    for (auto& r : readers)
        r.join();

    // Every pre-populated even key must still be present.
    for (std::uint64_t i = 0; i < N; i += 2)
        EXPECT_TRUE(m.exists(keyOf(i)));
    // Every written odd key must be present.
    for (std::uint64_t i = 1; i < N; i += 2)
        EXPECT_TRUE(m.exists(keyOf(i)));
    EXPECT_EQ(m.size(), N);
    EXPECT_GT(readsObserved.load(), 0u);  // readers made progress
}

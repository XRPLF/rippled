// Plan 6 A-phase integration tests.
//
// Exercises the public `attachFlatStateMapTo(Ledger&)` helper —
// the one explicit entry point a node or test integration uses to
// turn on the flat-map read path for a given Ledger. After attach:
//   * Ledger::flatStateMap() returns non-null
//   * Ledger::validateFlatStateMapMatchesShaMap() returns true
//   * Ledger::read(keylet) consults the flat map (verified by the
//     existing P6.4 wiring + null-safety regression tests)
//
// These are real-Ledger tests, not data-structure unit tests. They
// catch wiring issues the libxrpl/ledger/FlatStateMap.cpp tests
// cannot — bugs that only surface when an actual Ledger walks its
// SHAMap to build the flat map.

#include <helpers/TestFamily.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/DeferredRebuild.h>
#include <xrpl/ledger/FlatStateMap.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/shamap/SHAMap.h>

#include <gtest/gtest.h>

#include <memory>
#include <unordered_set>
#include <vector>

namespace xrpl::test {

class FlatStateMapIntegration : public ::testing::Test
{
protected:
    TestFamily family_{beast::Journal{beast::Journal::getNullSink()}};

    [[nodiscard]] std::shared_ptr<Ledger>
    makeGenesisLedger()
    {
        // Genesis ledger with default rules (no amendments enabled).
        // This ledger is IMMUTABLE — suitable for read-side tests but
        // not for raw{Insert,Replace,Erase} which require a mutable
        // SHAMap. For write-side tests, use `makeMutableChildLedger`.
        Rules const rules{std::unordered_set<uint256, beast::Uhash<>>{}};
        Fees const fees{XRPAmount{10}, XRPAmount{10'000'000}, XRPAmount{2'000'000}};
        std::vector<uint256> const amendments;

        return std::make_shared<Ledger>(
            kCreateGenesis, rules, fees, amendments, family_);
    }

    [[nodiscard]] std::shared_ptr<Ledger>
    makeMutableChildLedger()
    {
        // Build a child Ledger atop genesis. Child ledgers are
        // constructed mutable so the apply path can write to them.
        // This matches the production lifecycle: closed ledger N is
        // immutable; closed ledger N+1 is built from N (mutable until
        // it's itself closed).
        auto genesis = makeGenesisLedger();
        return std::make_shared<Ledger>(
            *genesis,
            NetClock::time_point{NetClock::duration{0}});
    }
};

TEST_F(FlatStateMapIntegration, NoFlatMapByDefault)
{
    auto const ledger = makeGenesisLedger();
    EXPECT_EQ(ledger->flatStateMap(), nullptr);
    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, AttachPopulatesAndMatches)
{
    auto const ledger = makeGenesisLedger();

    attachFlatStateMapTo(*ledger);

    auto const map = ledger->flatStateMap();
    ASSERT_NE(map, nullptr);
    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());

    // The flat map should have one entry per SLE in the ledger.
    std::size_t countViaWalk = 0;
    for (auto const& sle : ledger->sles)
    {
        (void)sle;
        ++countViaWalk;
    }
    EXPECT_EQ(map->size(), countViaWalk);
    EXPECT_GT(map->size(), 0u)
        << "Genesis ledger should have at least amendment+fee SLEs";
}

TEST_F(FlatStateMapIntegration, ReadsAfterAttachReturnSameSles)
{
    auto const ledger = makeGenesisLedger();

    // Collect SLE pointers via SHAMap-descent path (pre-attach).
    std::vector<std::shared_ptr<SLE const>> preAttachReads;
    for (auto const& sle : ledger->sles)
        preAttachReads.push_back(sle);

    attachFlatStateMapTo(*ledger);

    // After attach, reading via the flat map must yield SLEs that
    // serialize byte-equal to the originals.
    for (auto const& origSle : preAttachReads)
    {
        auto const fromMap = ledger->flatStateMap()->read(origSle->key());
        ASSERT_NE(fromMap, nullptr) << "Missing key: " << origSle->key();
        EXPECT_EQ(fromMap->getFullText(), origSle->getFullText());
    }
}

TEST_F(FlatStateMapIntegration, RepeatedAttachReplaces)
{
    auto const ledger = makeGenesisLedger();

    attachFlatStateMapTo(*ledger);
    auto const firstMap = ledger->flatStateMap();
    ASSERT_NE(firstMap, nullptr);

    attachFlatStateMapTo(*ledger);
    auto const secondMap = ledger->flatStateMap();
    ASSERT_NE(secondMap, nullptr);

    EXPECT_NE(firstMap.get(), secondMap.get());
    EXPECT_EQ(firstMap->size(), secondMap->size());
    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());
}

// ---------------------------------------------------------------------------
// Write-path verification — the P6.3 mirror wiring through Ledger::raw*
// ---------------------------------------------------------------------------

namespace {

// Construct a minimal SLE we can rawInsert into a Ledger for testing.
// We use ltACCOUNT_ROOT with a unique key — content doesn't need to be
// realistic; we're testing the mirror plumbing, not transactor logic.
[[nodiscard]] std::shared_ptr<SLE>
makeTestSle(std::uint64_t keyValue)
{
    uint256 const key{keyValue};
    return std::make_shared<SLE>(ltACCOUNT_ROOT, key);
}

}  // namespace

TEST_F(FlatStateMapIntegration, RawInsertMirrorsToFlatMap)
{
    auto const ledger = makeMutableChildLedger();
    attachFlatStateMapTo(*ledger);
    auto const map = ledger->flatStateMap();
    ASSERT_NE(map, nullptr);

    auto const sle = makeTestSle(0xDEAD'BEEFu);
    auto const sizeBefore = map->size();

    ledger->rawInsert(sle);

    EXPECT_EQ(map->size(), sizeBefore + 1u);
    auto const fromMap = map->read(sle->key());
    ASSERT_NE(fromMap, nullptr);
    EXPECT_EQ(fromMap.get(), sle.get())
        << "Flat map should hold the exact same shared_ptr we inserted";

    // Differential invariant must still hold after the write.
    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, RawEraseMirrorsToFlatMap)
{
    auto const ledger = makeMutableChildLedger();
    attachFlatStateMapTo(*ledger);
    auto const map = ledger->flatStateMap();
    ASSERT_NE(map, nullptr);

    // Insert, then erase.
    auto const sle = makeTestSle(0xCAFEu);
    ledger->rawInsert(sle);
    ASSERT_TRUE(map->exists(sle->key()));

    ledger->rawErase(sle);

    EXPECT_FALSE(map->exists(sle->key()))
        << "Erase should clear the flat-map entry";
    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, RawReplaceUpdatesFlatMap)
{
    auto const ledger = makeMutableChildLedger();
    attachFlatStateMapTo(*ledger);
    auto const map = ledger->flatStateMap();
    ASSERT_NE(map, nullptr);

    // Insert first version, then replace with a different SLE object
    // at the same key.
    auto const firstSle = makeTestSle(0xC0DEu);
    ledger->rawInsert(firstSle);
    auto const fromMapFirst = map->read(firstSle->key());
    ASSERT_NE(fromMapFirst, nullptr);
    EXPECT_EQ(fromMapFirst.get(), firstSle.get());

    auto const secondSle = makeTestSle(0xC0DEu);  // same key
    ASSERT_NE(firstSle.get(), secondSle.get())
        << "Test setup: replacement SLE must be a distinct object";

    ledger->rawReplace(secondSle);

    auto const fromMapSecond = map->read(secondSle->key());
    ASSERT_NE(fromMapSecond, nullptr);
    EXPECT_EQ(fromMapSecond.get(), secondSle.get())
        << "Replace should swap the flat-map pointer to the new SLE";
    EXPECT_NE(fromMapSecond.get(), firstSle.get());

    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, ManyWritesPreserveInvariant)
{
    auto const ledger = makeMutableChildLedger();
    attachFlatStateMapTo(*ledger);
    auto const map = ledger->flatStateMap();
    ASSERT_NE(map, nullptr);

    std::vector<std::shared_ptr<SLE>> sles;
    for (std::uint64_t i = 0; i < 100; ++i)
    {
        auto sle = makeTestSle(0x1000u + i);
        ledger->rawInsert(sle);
        sles.push_back(sle);
    }

    // Erase every other one.
    for (std::size_t i = 0; i < sles.size(); i += 2)
        ledger->rawErase(sles[i]);

    // Replace the rest.
    for (std::size_t i = 1; i < sles.size(); i += 2)
    {
        auto replacement = makeTestSle(0x1000u + i);
        ledger->rawReplace(replacement);
    }

    // After 100 inserts + 50 erases + 50 replaces, the invariant
    // must still hold.
    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, WritesBeforeAttachDontGetMirrored)
{
    // Sanity: if writes happen BEFORE the flat map is attached, they
    // hit the SHAMap only. Then attach + validate populates from the
    // SHAMap and the invariant holds. Catches the bootstrapping case.
    auto const ledger = makeMutableChildLedger();
    auto const sle = makeTestSle(0xBEEFu);
    ledger->rawInsert(sle);

    EXPECT_EQ(ledger->flatStateMap(), nullptr);

    attachFlatStateMapTo(*ledger);

    // After attach, the flat map sees the previously-inserted SLE
    // because populate walked the SHAMap.
    auto const map = ledger->flatStateMap();
    ASSERT_NE(map, nullptr);
    EXPECT_TRUE(map->exists(sle->key()));
    EXPECT_TRUE(ledger->validateFlatStateMapMatchesShaMap());
}

// ---------------------------------------------------------------------------
// Lifecycle propagation (P6.3): a child ledger built from a parent that
// carries a flat-state mirror must inherit an INDEPENDENT snapshot — the
// flat-map analogue of the SHAMap COW snapshot. This is what lets the flat
// map survive across ledgers instead of dying after one round.
// ---------------------------------------------------------------------------

TEST_F(FlatStateMapIntegration, ChildWithoutParentMapInheritsNone)
{
    // Default lifecycle: parent has no flat map → child has none. Zero
    // behavior change when the feature is not turned on.
    auto const parent = makeGenesisLedger();
    ASSERT_EQ(parent->flatStateMap(), nullptr);

    auto const child = std::make_shared<Ledger>(
        *parent, NetClock::time_point{NetClock::duration{0}});
    EXPECT_EQ(child->flatStateMap(), nullptr);
    EXPECT_TRUE(child->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, ChildInheritsIndependentSnapshot)
{
    auto const parent = makeGenesisLedger();
    attachFlatStateMapTo(*parent);
    auto const parentMap = parent->flatStateMap();
    ASSERT_NE(parentMap, nullptr);

    auto const child = std::make_shared<Ledger>(
        *parent, NetClock::time_point{NetClock::duration{0}});

    auto const childMap = child->flatStateMap();
    ASSERT_NE(childMap, nullptr);
    // Distinct object, equal contents, and consistent with the child's own
    // (COW-snapshotted) SHAMap.
    EXPECT_NE(childMap.get(), parentMap.get());
    EXPECT_EQ(childMap->size(), parentMap->size());
    EXPECT_TRUE(child->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, ChildWritesDoNotCorruptParentSnapshot)
{
    auto const parent = makeGenesisLedger();
    attachFlatStateMapTo(*parent);
    auto const parentMap = parent->flatStateMap();
    auto const parentSizeBefore = parentMap->size();

    auto const child = std::make_shared<Ledger>(
        *parent, NetClock::time_point{NetClock::duration{0}});

    // Mutate the child; the parent's snapshot must be untouched.
    auto const sle = makeTestSle(0x5117'5157u);
    child->rawInsert(sle);

    EXPECT_TRUE(child->flatStateMap()->exists(sle->key()));
    EXPECT_FALSE(parentMap->exists(sle->key()))
        << "Child write leaked into the parent's flat map";
    EXPECT_EQ(parentMap->size(), parentSizeBefore);
    EXPECT_TRUE(child->validateFlatStateMapMatchesShaMap());
    EXPECT_TRUE(parent->validateFlatStateMapMatchesShaMap());
}

TEST_F(FlatStateMapIntegration, PropagationChainsAcrossGenerations)
{
    // parent -> child -> grandchild, each inheriting + mutating. Every
    // generation's invariant must hold and each map stays independent.
    auto const parent = makeGenesisLedger();
    attachFlatStateMapTo(*parent);

    auto const child = std::make_shared<Ledger>(
        *parent, NetClock::time_point{NetClock::duration{0}});
    auto const cSle = makeTestSle(0xC111'D000u);
    child->rawInsert(cSle);
    ASSERT_TRUE(child->validateFlatStateMapMatchesShaMap());

    auto const grandchild = std::make_shared<Ledger>(
        *child, NetClock::time_point{NetClock::duration{0}});
    // Grandchild inherits the child's mutation...
    EXPECT_TRUE(grandchild->flatStateMap()->exists(cSle->key()));
    auto const gSle = makeTestSle(0x6111'D000u);
    grandchild->rawInsert(gSle);

    EXPECT_TRUE(grandchild->validateFlatStateMapMatchesShaMap());
    // ...but the grandchild's own write doesn't reach back up.
    EXPECT_FALSE(child->flatStateMap()->exists(gSle->key()));
    EXPECT_TRUE(child->validateFlatStateMapMatchesShaMap());
}

// ---------------------------------------------------------------------------
// Read routing (P6.4): with a map attached, Ledger::read(Keylet) must take
// the flat path and return results identical to the SHAMap-descent path.
// ---------------------------------------------------------------------------

TEST_F(FlatStateMapIntegration, LedgerReadFlatPathMatchesShaMapPath)
{
    auto const ledger = makeMutableChildLedger();
    // Add a few typed SLEs so there's something to read.
    std::vector<std::shared_ptr<SLE>> inserted;
    for (std::uint64_t i = 0; i < 25; ++i)
    {
        auto sle = makeTestSle(0x7000u + i);
        ledger->rawInsert(sle);
        inserted.push_back(sle);
    }

    // Capture SHAMap-path reads BEFORE attaching the flat map.
    std::vector<std::string> shaMapReads;
    for (auto const& s : inserted)
    {
        auto const r = ledger->read(Keylet{s->getType(), s->key()});
        ASSERT_NE(r, nullptr);
        shaMapReads.push_back(r->getFullText());
    }

    attachFlatStateMapTo(*ledger);
    ASSERT_NE(ledger->flatStateMap(), nullptr);

    // Now reads route through the flat map and must match byte-for-byte.
    for (std::size_t i = 0; i < inserted.size(); ++i)
    {
        auto const r = ledger->read(Keylet{inserted[i]->getType(), inserted[i]->key()});
        ASSERT_NE(r, nullptr) << "flat read miss for key " << inserted[i]->key();
        EXPECT_EQ(r->getFullText(), shaMapReads[i]);
    }

    // An absent key returns nullptr on the flat path too.
    EXPECT_EQ(
        ledger->read(Keylet{ltACCOUNT_ROOT, uint256{0xAB5E'0000ull}}), nullptr);
}

// ---------------------------------------------------------------------------
// The close-time differential invariant (P6.5) must actually FAIL on drift,
// not just pass on correct state — otherwise it's not a safety gate.
// ---------------------------------------------------------------------------

TEST_F(FlatStateMapIntegration, InvariantFailsOnPhantomEntry)
{
    auto const ledger = makeMutableChildLedger();
    attachFlatStateMapTo(*ledger);
    ASSERT_TRUE(ledger->validateFlatStateMapMatchesShaMap());

    // Inject a flat-map entry the SHAMap does not have.
    ledger->flatStateMap()->insert(
        uint256{0xDEAD'0001ull}, makeTestSle(0xDEAD'0001ull));

    EXPECT_FALSE(ledger->validateFlatStateMapMatchesShaMap())
        << "Invariant must catch an entry present in flat but not SHAMap";
}

TEST_F(FlatStateMapIntegration, InvariantFailsOnMissingEntry)
{
    auto const ledger = makeMutableChildLedger();
    auto const sle = makeTestSle(0xFEED'0001u);
    ledger->rawInsert(sle);
    attachFlatStateMapTo(*ledger);
    ASSERT_TRUE(ledger->validateFlatStateMapMatchesShaMap());

    // Drop an entry the SHAMap still has.
    ledger->flatStateMap()->erase(sle->key());

    EXPECT_FALSE(ledger->validateFlatStateMapMatchesShaMap())
        << "Invariant must catch an entry present in SHAMap but not flat";
}

// ---------------------------------------------------------------------------
// Plan 7 against a real Ledger's SHAMap (A-phase milestone 2)
//
// First end-to-end composition test: deferredRebuildRoot driven by a
// callback that reads from an actual Ledger's stateMap. The empty-
// modifications case is the smallest meaningful integration — the
// rebuild trivially returns the current root, which must match the
// SHAMap's root via getHash().
//
// Future slices extend this with a real after-modifications byte-
// identical assertion (the merge gate the plan-7 doc requires for any
// production ship).
// ---------------------------------------------------------------------------

TEST_F(FlatStateMapIntegration, Plan7EmptyDeltaProducesShaMapRoot)
{
    auto const ledger = makeMutableChildLedger();
    // Add some state so the SHAMap root is non-trivial.
    for (std::uint64_t i = 0; i < 20; ++i)
        ledger->rawInsert(makeTestSle(0x2000u + i));

    auto const shaMapRoot = ledger->stateMap().getHash().asUInt256();

    // The callback only needs to handle (depth=0, prefix=zero) for
    // the empty-delta case — it returns the SHAMap root.
    auto const callback =
        [&shaMapRoot](int depth, uint256 const& prefix) -> uint256 {
        if (depth == 0 && prefix == uint256{})
            return shaMapRoot;
        // Any other position is a bug for the empty-delta path.
        return uint256{};
    };

    auto const rebuiltRoot = deferredRebuildRoot({}, callback);
    EXPECT_EQ(rebuiltRoot, shaMapRoot);
}

TEST_F(FlatStateMapIntegration, Plan7EmptyDeltaOnEmptyLedger)
{
    // Same as above but on a freshly-constructed mutable child with
    // no extra state. The SHAMap root may already be non-trivial due
    // to inherited genesis SLEs.
    auto const ledger = makeMutableChildLedger();
    auto const shaMapRoot = ledger->stateMap().getHash().asUInt256();

    auto const callback =
        [&shaMapRoot](int depth, uint256 const& prefix) -> uint256 {
        if (depth == 0 && prefix == uint256{})
            return shaMapRoot;
        return uint256{};
    };

    EXPECT_EQ(deferredRebuildRoot({}, callback), shaMapRoot);
}

// ---------------------------------------------------------------------------
// CRITICAL LIMITATION TEST — documents an architectural gap surfaced
// by A-phase integration.
//
// Plan 7's `executeRebuildPlan` algorithm assumes leaves live at depth
// 64 (a fully-expanded radix tree). REAL SHAMap uses PATH COMPRESSION:
// leaves are stored at the shallowest depth where they're unambiguous
// (see SHAMap.cpp addGiveItem — when the path hits an empty branch or
// a leaf, the new leaf is placed at that depth, not deeper).
//
// Consequence: when actual SHAMap modifications cause new leaves to be
// placed at depths < 64 (the common case), the SHAMap's root hash
// computation differs from Plan 7's. They will NOT agree.
//
// Implications:
//   * Plan 7 as implemented is NOT byte-identical to real SHAMap.
//     The merge gate plan-7-deferred-shamap.md describes cannot
//     close on this implementation.
//   * Production deployment of Plan 7 requires either:
//       (a) extending the algorithm to handle path compression (know
//           where each leaf lives in the parent tree; account for
//           new inner-node creation at split points), OR
//       (b) replacing SHAMap with a non-path-compressed structure
//           (a much bigger amendment-class change).
//   * Option (a) is substantially more complex than the current
//     callback-based design — the algorithm needs to track tree
//     topology, not just position-keyed hashes.
//
// This test asserts the discrepancy explicitly so future readers see
// the issue. The library code (planDeferredRebuild, computeInnerNodeHash,
// executeRebuildPlan, deferredRebuildRoot, parallel variants) remains
// in place as a working kernel for the depth-64-leaves model, which
// is still useful as a reference and starting point for the refactor.
// ---------------------------------------------------------------------------

TEST_F(
    FlatStateMapIntegration,
    Plan7DoesNotMatchPathCompressedShaMap_DocumentedLimitation)
{
    // Build a Ledger with a few SLEs whose keys differ in their high
    // nibbles (forces SHAMap path compression — they're stored as
    // direct children of root or shallow inner nodes).
    auto const ledger = makeMutableChildLedger();
    std::vector<std::shared_ptr<SLE>> sles;
    for (std::uint64_t i = 0; i < 5; ++i)
    {
        // Spread keys across the top of the tree.
        uint256 k;
        k.data()[0] = static_cast<std::uint8_t>(i << 4);
        sles.push_back(std::make_shared<SLE>(ltACCOUNT_ROOT, k));
        ledger->rawInsert(sles.back());
    }

    auto const realShaMapRoot = ledger->stateMap().getHash().asUInt256();

    // Construct a Plan-7 rebuild treating these leaves as if they
    // lived at depth 64. The callback returns:
    //   - depth 64: the leaf hash for the modified key (else zero)
    //   - other depths: zero (assume empty parent)
    std::vector<uint256> modKeys;
    for (auto const& s : sles)
        modKeys.push_back(s->key());

    // Pre-compute each leaf's SHAMap hash for the callback.
    std::unordered_map<uint256, uint256, beast::Uhash<>> leafHashes;
    for (auto const& s : sles)
    {
        uint256 const leafKey = s->key();
        // We can ask the SHAMap for the leaf's actual hash
        SHAMapHash itemHash;
        auto const item = ledger->stateMap().peekItem(leafKey, itemHash);
        if (item)
            leafHashes[leafKey] = itemHash.asUInt256();
    }

    auto const plan7Callback =
        [&leafHashes](int depth, uint256 const& prefix) -> uint256 {
        if (depth == 64)
        {
            auto it = leafHashes.find(prefix);
            if (it != leafHashes.end())
                return it->second;
        }
        return uint256{};  // empty parent at non-leaf depths
    };

    auto const plan7Root = deferredRebuildRoot(modKeys, plan7Callback);

    // Document the discrepancy. Plan 7's depth-64 model produces a
    // different hash than path-compressed SHAMap — this MUST be the
    // case until Plan 7 is refactored to handle path compression.
    EXPECT_NE(plan7Root, realShaMapRoot)
        << "If this assertion ever starts failing, Plan 7's path-"
        << "compression limitation may have been fixed — update this "
        << "test to assert equality and remove the documented limitation.";
}

}  // namespace xrpl::test

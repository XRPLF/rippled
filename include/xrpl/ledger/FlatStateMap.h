#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xrpl {

/** Flat keylet-indexed materialization of XRPL state.

    The SHAMap is XRPL's authoritative state structure — it produces
    the state root that consensus agrees on. But on its own, it forces
    every state read to walk the trie: ~6–10 spinlocked inner-node
    fetches per `read(Keylet)`.

    `FlatStateMap` materializes the same keylet → SLE mapping into a
    flat hash table. Once populated, lookups are a single
    `unordered_map::find()` — nanoseconds, not microseconds.

    This is the 2-writes-for-1-read pattern (Plan 6):
      * On apply, the apply path dual-writes to SHAMap and FlatStateMap.
      * On read, only FlatStateMap is consulted.

    The flat map is purely auxiliary. The SHAMap remains authoritative,
    can always be rebuilt from the underlying NodeStore, and is what the
    network's state-root commitment is computed from. If FlatStateMap is
    wrong, the differential invariant check at ledger close (Plan 6 P6.5)
    catches it; there is **no runtime fallback to SHAMap descent on
    miss** — a miss is a bug.

    Thread-safe via a shared_mutex. Concurrent reads do not block each
    other; writes are exclusive. Future phases may replace this with
    a lock-free atomic-pointer-swap design.
*/
class FlatStateMap
{
public:
    using key_type = uint256;
    using value_type = std::shared_ptr<STLedgerEntry const>;

    FlatStateMap() = default;
    ~FlatStateMap() = default;

    // Non-copyable, non-movable. The map owns a shared_mutex (not movable)
    // and a potentially large hash table; callers that need ownership
    // transfer should wrap in std::unique_ptr<FlatStateMap>.
    FlatStateMap(FlatStateMap const&) = delete;
    FlatStateMap&
    operator=(FlatStateMap const&) = delete;
    FlatStateMap(FlatStateMap&&) = delete;
    FlatStateMap&
    operator=(FlatStateMap&&) = delete;

    /** Look up an SLE by its SHAMap key.

        Returns nullptr if the key is not in the map. In Plan 6's pure
        2w/1r model, callers reading state that *should* exist treat
        nullptr as a precondition violation — there is no fallback path
        that would recover from a missed entry.
    */
    [[nodiscard]] value_type
    read(key_type const& key) const;

    /** Test whether an SLE is present. O(1). */
    [[nodiscard]] bool
    exists(key_type const& key) const;

    /** Insert a new SLE. Replaces any prior entry under the same key.

        Used by both:
          * the apply path's `view.insert()` (new ledger object), and
          * the apply path's `view.update()` (mutating an existing SLE
            produces a new shared_ptr value).
    */
    void
    insert(key_type const& key, value_type sle);

    /** Remove an SLE. No-op if the key is absent. */
    void
    erase(key_type const& key);

    /** Number of SLEs currently materialized. */
    [[nodiscard]] std::size_t
    size() const;

    /** Test whether the map is empty. O(1). */
    [[nodiscard]] bool
    empty() const;

    /** Remove every entry. Used by tests and by snapshot reset. */
    void
    clear();

    /** Deep-copy snapshot of the current state.

        The snapshot is a frozen FlatStateMap (returned by value-like
        unique_ptr) that shares the underlying SLE objects via
        shared_ptr but has its own hash table. Subsequent writes to the
        source FlatStateMap do not affect the snapshot.

        Snapshot cost is O(N) in entry count — ~one pointer copy per
        entry plus bucket allocation. For current mainnet (~10M SLEs)
        this is ~100–200 ms; expensive enough that snapshots should be
        per-ledger-close, not per-transaction.

        A future phase will replace this with a persistent / HAMT
        structure that gives O(log N) snapshot and structural sharing
        across versions.
    */
    [[nodiscard]] std::unique_ptr<FlatStateMap>
    snapshot() const;

    /** Visit every (key, SLE) pair under a single shared lock.

        @param visitor  Called as `void(key_type const&, value_type const&)`.

        The lock is held for the duration of the iteration; visitors
        must not call back into the same FlatStateMap (deadlock /
        recursive shared_lock UB). Visitors that want to mutate state
        should collect keys first and apply mutations after iteration
        returns.
    */
    template <typename F>
    void
    forEach(F&& visitor) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (auto const& [key, sle] : map_)
            visitor(key, sle);
    }

private:
    using HashFn = HardenedHash<>;
    using MapType = std::unordered_map<key_type, value_type, HashFn>;

    mutable std::shared_mutex mutex_;
    MapType map_;
};

// Forward declarations to avoid pulling heavy headers into this file.
class ReadView;
class Ledger;

/** Populate a FlatStateMap from every SLE in a ReadView.

    Used at node startup to build the flat materialization from a
    SHAMap-backed authoritative ledger. Cost is O(N) iterations of
    `view.sles`, each of which descends the SHAMap, so this is
    expected to take seconds-to-minutes for a current mainnet-sized
    ledger (~10M SLEs). Run once on startup; subsequent ledgers are
    maintained incrementally via dual-write at apply time (P6.3).

    @pre `target` is empty. (Not enforced — replacing existing entries
         is well-defined, but mixing populated state with an externally-
         provided ReadView is a bug-shaped pattern; assert in DEBUG.)
*/
void
populateFromReadView(FlatStateMap& target, ReadView const& source);

/** Populate a FlatStateMap from any forward range of `shared_ptr<SLE const>`.

    Lower-level building block underlying `populateFromReadView`. Useful
    in tests (the range can be a `std::vector<shared_ptr<SLE const>>`)
    and in non-ReadView contexts (e.g., reloading a persisted flat-map
    sidecar at startup).

    The range element type must be `shared_ptr<SLE const>` (or
    implicitly convertible). Each element's `.key()` becomes the
    FlatStateMap key.
*/
template <typename Range>
void
populateFromRange(FlatStateMap& target, Range const& sles)
{
    for (auto const& sle : sles)
        target.insert(sle->key(), sle);
}

// ---------------------------------------------------------------------------
// Mirror helpers (P6.3).
//
// The xrpld `RawView` interface defines three pure-virtual methods that
// every state mutation flows through: `rawInsert`, `rawReplace`, and
// `rawErase`. In plan-6's 2-writes-for-1-read pattern, every such call
// must also update the flat map. These helpers perform the flat-map side
// of that dual-write — they exist as standalone functions (rather than
// methods on FlatStateMap) so the Ledger integration is a one-line
// addition at each `raw*` override site, with no FlatStateMap class
// surface added for purely Ledger-specific semantics.
//
// Permissive semantics: `mirrorRawReplace` on an absent key inserts;
// `mirrorRawErase` on an absent key is a no-op. The SHAMap side enforces
// the precondition (replace requires existence); the flat mirror ensures
// the post-state matches whatever the SHAMap committed. If the caller
// gets it wrong, the differential invariant check at close (P6.5) is the
// stop-the-line gate.
// ---------------------------------------------------------------------------

void
mirrorRawInsert(FlatStateMap& map, std::shared_ptr<STLedgerEntry const> sle);

void
mirrorRawReplace(FlatStateMap& map, std::shared_ptr<STLedgerEntry const> sle);

void
mirrorRawErase(FlatStateMap& map, std::shared_ptr<STLedgerEntry const> const& sle);

void
mirrorRawErase(FlatStateMap& map, uint256 const& key);

// ---------------------------------------------------------------------------
// Keylet-aware read (P6.4).
//
// This is the read-side counterpart to `mirrorRaw*` — the testable unit
// underlying `Ledger::read(Keylet)`'s flat-map path. It looks up the
// SLE by `k.key` and verifies the SLE matches the keylet's expected
// type via `Keylet::check`. On either a miss or a type mismatch, it
// returns nullptr — matching the contract of `Ledger::read`.
//
// Plan 6 v2 semantics: this function does not consult a SHAMap or any
// other source on miss. When a FlatStateMap is the read source of
// truth, a miss IS the answer. The differential invariant check at
// close (P6.5) is what makes that safe.
// ---------------------------------------------------------------------------

std::shared_ptr<STLedgerEntry const>
readFromFlatStateMap(FlatStateMap const& map, Keylet const& k);

// ---------------------------------------------------------------------------
// Differential invariant (P6.5).
//
// At every ledger close, the flat map's key-set must match the
// SHAMap's key-set. `diffFlatStateKeys` produces both sides of the
// disagreement; `flatStateMapMatches` is the boolean predicate the
// hot-path integration calls (and fails the close if it returns false).
//
// Content drift (right keys, wrong SLE bodies) is a separate, stronger
// invariant. It's prevented by construction: the mirror helpers write
// exactly the SLE the caller passed to raw*. If mirror helpers and
// wiring are both correct, the membership check above is sufficient.
// ---------------------------------------------------------------------------

struct FlatStateKeyDiff
{
    std::vector<uint256> missingFromFlat;
    std::vector<uint256> extraInFlat;
};

template <typename SourceRange>
[[nodiscard]] FlatStateKeyDiff
diffFlatStateKeys(FlatStateMap const& flat, SourceRange const& sourceKeys)
{
    FlatStateKeyDiff diff;

    // Pass 1: walk source keys; collect any absent from flat. Record
    // which keys we've seen so pass 2 can spot phantoms.
    std::unordered_set<uint256, HardenedHash<>> seen;
    seen.reserve(static_cast<std::size_t>(std::distance(
        std::begin(sourceKeys), std::end(sourceKeys))));

    for (auto const& key : sourceKeys)
    {
        seen.insert(key);
        if (!flat.exists(key))
            diff.missingFromFlat.push_back(key);
    }

    // Pass 2: walk flat; anything not in `seen` is a phantom.
    flat.forEach(
        [&seen, &diff](uint256 const& key, auto const& /*sle*/) {
            if (!seen.contains(key))
                diff.extraInFlat.push_back(key);
        });

    return diff;
}

template <typename SourceRange>
[[nodiscard]] bool
flatStateMapMatches(FlatStateMap const& flat, SourceRange const& sourceKeys)
{
    auto const diff = diffFlatStateKeys(flat, sourceKeys);
    return diff.missingFromFlat.empty() && diff.extraInFlat.empty();
}

/** Compare a FlatStateMap against a SHAMap-like source.

    `ShaMapLike` is any range whose elements expose a `key()` accessor
    returning a `uint256`. The real `SHAMap` satisfies this contract
    (its iterators yield `SHAMapItem`s with `.key()`), as does the
    `MockShaMapItem` used in tests.

    This is the helper the Ledger integration calls to run the P6.5
    differential invariant. It extracts keys into a transient buffer
    (O(N) allocation, ~N pointers' worth of memory) and forwards to
    `flatStateMapMatches`. The transient buffer is acceptable at close
    cadence; the integration can later optimize by walking the SHAMap
    in-place once profiling shows the allocation matters.
*/
template <typename ShaMapLike>
[[nodiscard]] bool
flatStateMapMatchesShaMap(FlatStateMap const& flat, ShaMapLike const& shaMap)
{
    std::vector<uint256> keys;
    for (auto const& item : shaMap)
        keys.push_back(item.key());
    return flatStateMapMatches(flat, keys);
}

// ---------------------------------------------------------------------------
// A-phase integration: attach a populated FlatStateMap to a Ledger.
//
// This is the public entry point a node or test uses to "turn on" the
// flat-map read path for a Ledger. The function:
//   1. Allocates a new FlatStateMap.
//   2. Eagerly populates it by walking every SLE in the Ledger.
//   3. Attaches it via `Ledger::setFlatStateMap`.
//
// After this call:
//   * `Ledger::flatStateMap()` returns the populated map
//   * `Ledger::read(keylet)` routes through the flat map (no SHAMap
//     descent on the hot path; see P6.4 wiring)
//   * `Ledger::raw{Insert,Replace,Erase}` mirror writes to the flat
//     map alongside the SHAMap (see P6.3 wiring)
//   * `Ledger::validateFlatStateMapMatchesShaMap()` returns true
//
// Repeat calls discard the prior map and produce a fresh one.
//
// Cost: O(N) walk over the Ledger's state SHAMap to populate the flat
// map. For mainnet-scale state, this is bounded by SHAMap traversal
// speed — typically minutes once. Run at node startup or whenever a
// Ledger first becomes "live" (the one apply writes to).
// ---------------------------------------------------------------------------

void
attachFlatStateMapTo(Ledger& ledger);

}  // namespace xrpl

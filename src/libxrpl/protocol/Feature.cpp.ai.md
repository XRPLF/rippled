# `src/libxrpl/protocol/Feature.cpp` — Amendment Registry

## Purpose

`Feature.cpp` implements the central registry for all XRPL protocol amendments (called "features" in code). On the XRP Ledger, validator voting activates amendments on-chain; every conditional code path gated by an amendment queries this registry at runtime. The file is responsible for three things: holding all amendment metadata in a queryable form, providing the bidirectional mapping between a feature's `uint256` on-chain identifier and a compact bitset index used by `FeatureBitset`, and enforcing that the registry is fully populated before any queries reach it.

## The `FeatureCollections` Internal Singleton

The entire registry lives in a file-scope `FeatureCollections featureCollections` instance. The class is deliberately anonymous within an unnamed namespace — callers reach it only through the thin free-function wrappers at the bottom of the file.

`FeatureCollections` maintains three data structures simultaneously:

- **`features`** — a `boost::multi_index_container` that stores `Feature` structs (name + `uint256`) with three simultaneous indexes: random-access by insertion order (`byIndex`), hash-unique by `uint256` (`byFeature`), and hash-unique by string name (`byName`).
- **`all`** — a `std::map<std::string, AmendmentSupport>` covering every registered amendment, including retired ones.
- **`supported`** — a `std::map<std::string, VoteBehavior>` covering only amendments the server can vote on (`Supported::yes`).

The `upVotes` and `downVotes` counters shadow the supported map's content so test code can verify vote tallies without iterating the map.

The multi-index container is the architectural centerpiece. A simple `unordered_map` would support name→hash and hash→name lookups, but it cannot provide a stable integer index. The stable index is essential because `FeatureBitset` (defined in `Feature.h`) is a `std::bitset<numFeatures>` — each bit corresponds to one amendment's *insertion-order position* in this container. The `featureToBitsetIndex()` / `bitsetIndexToFeature()` pair translate between `uint256` and bitset index at every hot-path amendment check. Using a random-access index inside a single container avoids maintaining a separate parallel array and keeps all three lookups O(1) without cache-thrashing.

## Registration and the `readOnly` Fence

The `std::atomic<bool> readOnly` member acts as a write-once fence. During registration, `readOnly` is false and all writes are allowed. Once the final static variable `readOnlySet` is initialized (the last statement in the file), `registrationIsDone()` flips `readOnly` to true. Every subsequent query method asserts `readOnly.load()` via `XRPL_ASSERT`.

This design exploits C++'s guarantee that file-scope variables in a single translation unit are initialized top-to-bottom. All `uint256 const feature##name` variables are initialized in sequence before `readOnlySet`, so by the time anything outside the translation unit can run, registration is complete and the registry is permanently read-only. No runtime lock is ever needed.

`registerFeature()` enforces several invariants at registration time using a small `check()` helper that calls `LogicError()` on failure:
- `Supported::no` features must have `VoteBehavior::DefaultNo` (you cannot vote for an unsupported feature).
- Duplicate names are a hard error — the duplicate check uses `getByName()` before inserting.
- The container size must remain within the pre-allocated `detail::numFeatures` bound.
- `upVotes + downVotes` must always equal `supported.size()`.

## Feature ID Derivation

Each amendment's `uint256` on-chain identifier is computed as `sha512Half(Slice(name.data(), name.size()))` at registration time. The name string *is* the canonical source — the hash is deterministic and reproducible from just the ASCII name. This means two builds will produce identical identifiers as long as the macro list hasn't changed, and the ledger can reference amendments by their hash without any out-of-band identifier distribution.

## The X-Macro Pattern

The master list of all amendments lives in `include/xrpl/protocol/detail/features.macro`. That single file is `#include`d three times with different macro bindings:

1. **In `Feature.h` (inside `detail` namespace)**: `XRPL_FEATURE` and friends expand to `+1`, so `numFeatures` is computed as a `constexpr std::size_t` at compile time. This is the bitset size.

2. **In `Feature.h` (near the bottom)**: `XRPL_FEATURE(name, ...)` expands to `extern uint256 const feature##name;` — public declarations that let any translation unit reference a specific amendment as a global constant.

3. **In `Feature.cpp`**: `XRPL_FEATURE(name, supported, vote)` expands to `uint256 const feature##name = registerFeature(enforceValidFeatureName([] { return #name; }), supported, vote);` — the actual definitions that trigger registration via the initializer.

The push/pop macro scaffolding (`#pragma push_macro`) around each inclusion ensures these redefinitions don't permanently pollute the macro namespace.

## Compile-Time Name Validation

`enforceValidFeatureName()` is a `consteval` wrapper that calls two `consteval` predicates from `Feature.h`:

- `validFeatureName()` rejects any name containing bytes with the high bit set (non-ASCII) or below `0x20` (control characters). This prevents visually confusable Unicode identifiers from making it into a production build.
- `validFeatureNameSize()` rejects names longer than 63 bytes and names exactly 32 bytes long. The 32-byte exclusion reserves that length for raw `uint256` hashes in WASM/interop contexts, preventing a name-as-hash collision.

Both checks fire as `static_assert` failures at compile time, so an invalid name in `features.macro` is a build error, not a runtime surprise.

## Amendment Lifecycle States

`VoteBehavior` and `AmendmentSupport` together express the full lifecycle:

- **`Supported::no, DefaultNo`** — feature in active development; registered but neither voted for nor available in released software.
- **`Supported::yes, DefaultNo`** — complete and supported; validators can enable it but the server does not vote for it by default, leaving the decision to the community.
- **`Supported::yes, DefaultYes`** — critical bug fix; the server actively votes for it (reserved for urgent fixes with explicit community communication).
- **`Supported::yes, Obsolete`** — once-supported amendment that never passed; the server still understands it if somehow enabled, but no longer votes for it.
- **`XRPL_RETIRE_FEATURE` / `XRPL_RETIRE_FIX`** — calls `retireFeature()`, which registers the name as `Supported::yes, VoteBehavior::Obsolete` but also marks it `AmendmentSupport::Retired`. The conditional code guarded by the feature has been removed from the codebase. The amendment must remain registered because it may appear in the Amendments ledger object; removing it entirely would cause amendment-blocking.

## Relationship to `FeatureBitset`

`FeatureBitset`, defined entirely in `Feature.h`, is a `std::bitset<numFeatures>` wrapper that overloads `set()`, `reset()`, `flip()`, and `operator[]` to accept `uint256` directly, internally calling `featureToBitsetIndex()` on each access. The bitwise operators (`&`, `|`, `^`, `-` for set difference) allow the ledger's `Rules` object to efficiently compute which amendments are active by intersecting sets. This is the data structure that every transaction-processing code path queries via `view.rules.enabled(featureFoo)`.
# `include/xrpl/protocol/Feature.h`

## Role in the System

This header is the central registration and manipulation point for XRPL's **amendment system** — the protocol's mechanism for introducing new ledger behavior in a network-wide, validator-voted way. Every protocol extension, from NFT support to AMM integration, is controlled by an amendment identifier that gates conditional code paths at runtime. `Feature.h` defines how amendments are declared, how their identities are computed, and how sets of enabled amendments are represented efficiently at ledger-processing time.

## The X-Macro Pattern: A Single Source of Truth

The defining architectural choice in this file is the use of `features.macro` as a single declarative list of all amendments. This `.macro` file is `#include`d multiple times with different macro definitions each time, a classic X-macro pattern. Each invocation achieves a different goal:

**Counting at compile time.** Inside `namespace detail`, the macros are redefined to expand each entry to `+1`. The resulting sum is the compile-time constant `detail::numFeatures`, which determines the template parameter of the underlying `std::bitset`. This constant must never be _less than_ the actual number of amendments — a `LogicError` on startup verifies this — but it may be larger, reserving headroom.

**Declaring extern variables.** Near the bottom of `Feature.h`, the macros are redefined so each `XRPL_FEATURE(name, ...)` becomes `extern uint256 const featureName;` and each `XRPL_FIX(name, ...)` becomes `extern uint256 const fixName;`. These are the identifiers used everywhere in the codebase in `rules.enabled(featureName)` checks. Retired features expand to nothing — their conditional code has already been removed.

**Registering at startup.** In `Feature.cpp`, the macros are expanded to call `registerFeature()`, which SHA-512/half-hashes the feature's string name to produce its `uint256` identifier and stores it in an internal `boost::multi_index_container`. Because these are static variable initializers, registration happens before `main()`. A final static variable calls `registrationIsDone()`, setting an atomic `readOnly` flag that prevents further registration and permits lookups to proceed safely.

This three-context expansion means the list of amendments never needs to be maintained in more than one place.

## Feature Name Validation at Compile Time

The `consteval` functions `validFeatureNameSize()` and `validFeatureName()` run entirely during compilation. `validFeatureName()` rejects any name containing characters below `0x20` (non-printable control characters) or with the `0x80` bit set (non-ASCII / UTF-8 multibyte sequences). This guards against visually confusable Unicode identifiers that C++ technically permits.

`validFeatureNameSize()` enforces two bounds: names must not exceed 63 characters, and names must not be _exactly_ 32 bytes. The 32-byte exclusion is a deliberate forward-compatibility reserve: a `uint256` (XRPL's 32-byte hash type) could theoretically appear as a compact feature selector in WASM or interop contexts, and allowing a human-readable 32-character name would create an ambiguous namespace collision.

Both functions are called inside `enforceValidFeatureName()` in `Feature.cpp`, which wraps them in `static_assert` — any invalid feature name causes a hard compile error rather than a runtime failure.

## `FeatureBitset`: Efficient Feature Sets

`FeatureBitset` is a thin wrapper around `std::bitset<detail::numFeatures>` that replaces integer-index access with `uint256`-based access. It inherits the base `bitset` privately and selectively exposes its interface via `using` declarations.

The fundamental insight is a two-level identity scheme. Externally, every amendment is a `uint256` hash; internally, it maps to a compact sequential index via `featureToBitsetIndex()`. This translation lives in `FeatureCollections::featureToBitsetIndex()`, which queries the `multi_index_container` by `uint256` and returns the element's position in the random-access index. Once translated, all bitset operations — `set`, `reset`, `flip`, `test`, and the full suite of bitwise operators — run in O(1) with no further hashing.

The class provides overloaded `operator&`, `operator|`, and `operator^` for set-algebra across `FeatureBitset` objects and between `FeatureBitset` and individual `uint256` values. The `operator-` overload is defined as **set difference** (`lhs & ~rhs`), critical for amendment voting logic where the system needs to compute "amendments I support but that are not yet enabled."

The `foreachFeature()` free function iterates all set bits and translates each index back to its `uint256` via `bitsetIndexToFeature()` — the inverse lookup — before passing it to a callback.

## Amendment Lifecycle and Governance

`Feature.h` documents a precise lifecycle enforced by the `VoteBehavior` and `AmendmentSupport` enumerations:

- **`Unsupported` / `DefaultNo`**: new amendments enter in this state. The conditional code exists but validators do not vote for activation.
- **`Supported` / `DefaultNo`**: development complete; validators recognize the amendment but still abstain by default. External governance determines when the network is ready.
- **`Supported` / `DefaultYes`**: reserved for critical bug fixes, after off-chain consensus. Validators actively vote for activation.
- **`VoteBehavior::Obsolete`**: the amendment was once supported and votable but never passed; it must remain registered (to handle the theoretical case that it eventually activates) but no validator will vote for it.
- **`XRPL_RETIRE_*`**: amendments active for many years whose conditional code has been deleted. They are registered via `retireFeature()` — internally `registerFeature(name, Supported::yes, VoteBehavior::Obsolete)` — so that nodes encountering them in the ledger's Amendments object remain amendment-compatible rather than blocked.

The rule that a `Supported::yes` amendment can **never** revert to `Supported::no` once released prevents a dangerous situation: a future validator binary that does not recognize an amendment it could plausibly encounter would become amendment-blocked on any network where that amendment is enabled.

## Relationship to `Rules`

The runtime consumer of `FeatureBitset` is `Rules` (in `Rules.h`), which holds the set of currently active amendments derived from the validated ledger. All feature-gated code calls `view.rules.enabled(featureName)`, where `featureName` is one of the `extern uint256 const` variables declared by the macro expansion in `Feature.h`. Because `Rules::enabled()` takes a `uint256` and `FeatureBitset` translates it to a bitset index, the hot path for every transaction application is a hash-map lookup followed by a bitset test — both effectively O(1) with small constants.
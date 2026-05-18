# `include/xrpl/protocol/Rules.h` — Protocol Feature Rules

## Purpose

`Rules.h` defines the `Rules` class, the authoritative source-of-truth for which XRPL protocol amendments (feature flags) are active during transaction processing for a given ledger. Every behavioral branch in the transaction engine that depends on a conditionally-enabled feature — fee schedules, new transaction types, bug fixes, math semantics — ultimately gates through a `Rules::enabled()` call. The file also declares the thread-local plumbing (`getCurrentTransactionRules`, `setCurrentTransactionRules`, `CurrentTransactionRulesGuard`) that lets deeply nested code query active rules without passing a `Rules` reference through every call frame.

## The `Rules` Class

`Rules` is a value type with value semantics: it is default-copy/move-constructible and assignable. Internally it uses a `std::shared_ptr<Impl const>` (the classic pimpl idiom), which keeps copy cost to a single atomic refcount bump regardless of how many amendments are active. This matters because `Rules` is copied into every `ApplyContext` and flows through the entire transaction-application stack.

### `Rules::Impl` (implementation detail, in Rules.cpp)

The private `Impl` holds three members:

- `set_` — an `unordered_set<uint256, hardened_hash<>>` populated from the ledger's `sfAmendments` field. The `hardened_hash` hasher (instead of the lighter `beast::uhash`) is chosen deliberately to resist hash-flooding attacks in production — a set populated from network data should not be DoS-vulnerable through adversarial hash collisions.
- `digest_` — an `optional<uint256>` holding the digest (state hash key) of the ledger's Amendments object. When present, two `Rules` instances can be compared in O(1) by comparing digests instead of iterating their amendment sets.
- `presets_` — a const reference to a caller-supplied `unordered_set` of always-enabled features. These represent features baked in at genesis or forced on in test/devnet configurations.

`enabled(feature)` checks `presets_` first (features that are unconditionally on), then falls back to `set_`. The two-tier lookup lets test harnesses inject preset features without touching the ledger state.

`operator==` on `Impl` is digest-based: two rule sets with no digest are considered equal (both represent the empty genesis state), and two rule sets with differing digests are unequal. A digest mismatch is a fast O(1) check. This makes `Rules` comparison cheap for the diagnostics path without requiring a full set-intersection walk.

### Construction

The public constructor `Rules(presets)` builds an empty rule set from a preset collection — intended for the genesis ledger, which has no amendments yet. The private constructor `Rules(presets, digest, amendments)` is used by the factory functions.

### Factory functions: `makeRulesGivenLedger`

Declared as `friend` and defined in `src/libxrpl/ledger/ReadView.cpp`, these two overloads construct a `Rules` from a live ledger view:

```cpp
Rules makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current);
Rules makeRulesGivenLedger(DigestAwareReadView const& ledger,
                           std::unordered_set<uint256, beast::uhash<>> const& presets);
```

The implementation reads `keylet::amendments()` from the ledger, extracts the `sfAmendments` vector, and constructs a `Rules` with the digest of the SLE for fast future comparisons. If the amendments SLE is absent (as in the genesis ledger), it returns a preset-only `Rules`. Keeping construction here rather than inside `Rules` itself keeps the ledger-access dependency out of the protocol library and satisfies the layering requirement.

## Thread-Local Rules and `CurrentTransactionRulesGuard`

`getCurrentTransactionRules()` and `setCurrentTransactionRules()` maintain a per-thread `optional<Rules>` via `LocalValue<optional<Rules>>` — a `boost::thread_specific_ptr`-backed container that avoids static-initialization-order issues by constructing on first use. This design enables deeply-nested transaction processing code to call the freestanding `isFeatureEnabled(feature)` without threading a `Rules` parameter through every function.

`setCurrentTransactionRules` is not a trivial setter. It also pushes a related side effect: it calls `Number::setMantissaScale(...)` based on whether `featureSingleAssetVault` or `featureLendingProtocol` is active. These amendments unlock a wider `MantissaRange::large` for arithmetic operations. The comment explains the deliberate push strategy: because `Number` operations happen far more often than rule changes, propagating the scale setting at rule-install time (push) is vastly cheaper than rechecking the rule on every arithmetic operation (pull).

`CurrentTransactionRulesGuard` is a straightforward RAII wrapper: the constructor calls `setCurrentTransactionRules` with the new rules, saving the old value; the destructor restores the old value. It is non-copyable to prevent accidental aliasing. Callers in `applySteps.cpp` and `Transactor.cpp` use this guard to bracket transaction application, ensuring the thread-local state is always restored even on exception paths.

## `isFeatureEnabled` — Global Convenience Query

The freestanding `isFeatureEnabled(feature)` delegates to the thread-local current rules and safely returns `false` if no rules are installed (i.e., outside any transaction context). This is the single API used in lower-level protocol code (e.g., `STAmount.cpp`, `AMMHelpers.cpp`) that cannot easily take a `Rules` parameter, trading explicit dependency for convenience. The implicit reliance on thread-local state means callers must ensure `CurrentTransactionRulesGuard` is active on the call stack; calling it outside a transaction context silently returns `false`.

## Design Notes

The `Rules()` default constructor is deleted, enforcing that every `Rules` instance carries an explicit preset set — an invariant that prevents accidentally propagating a "no features" state. The `operator!=` is derived from `operator==` rather than independently implemented, keeping equality semantics consistent. The `operator==` is documented as diagnostic-only, acknowledging that the digest shortcut is correct in practice but not a strict semantic equality: two identical amendment sets from different ledgers with the same digest would compare equal even if their presets differed (an assertion guards this case).
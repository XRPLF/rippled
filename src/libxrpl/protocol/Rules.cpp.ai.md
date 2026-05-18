# `src/libxrpl/protocol/Rules.cpp`

## Role in the System

`Rules.cpp` implements the machinery that makes XRPL's amendment system visible to transaction-processing code. Every transaction on the ledger must run under a consistent view of which protocol features are active. This file provides that view — a lightweight, immutable snapshot of enabled amendments — and also manages the thread/coroutine-local slot that holds the active ruleset so that any code in the processing path can query it without having to accept a `Rules` argument explicitly.

## The `Rules` and `Rules::Impl` Classes

`Rules` uses the pimpl idiom: the actual state lives in `Rules::Impl`, referenced through `std::shared_ptr<Impl const>`. The header comment calls this out explicitly — it makes `Rules` cheap to copy because all copies share the same `Impl`. Because ledger processing may propagate a `Rules` object through many call frames and data structures, this sharing matters in practice.

`Impl` stores two things: a reference to `presets_` (an externally-owned `unordered_set` of features that are always on, such as genesis-era behavior or test overrides), and `set_`, a locally-owned `unordered_set` populated from the ledger's `sfAmendments` field. Notably, `presets_` uses `beast::uhash<>` while `set_` uses `hardened_hash<>`. The hardened hash guards against hash-flooding when inserting validator-supplied amendment IDs into the owned set, whereas presets are controlled by node operators and don't need the same protection.

The `enabled()` method checks `presets_` first (a O(1) lookup into the externally-owned set), then `set_` (O(1) in the owned set). No locking is needed because both the `Impl` and the `set_` it owns are immutable after construction.

### Construction Path and Encapsulation

The constructor that accepts a ledger digest and an `STVector256` of amendments — the one that actually builds the full ruleset from on-ledger state — is `private`. Only two friend functions declared in `Rules.h` can invoke it: both overloads of `makeRulesGivenLedger`, defined in `src/libxrpl/ledger/ReadView.cpp`. There, `makeRulesGivenLedger` reads the `keylet::amendments()` state-map entry from a `DigestAwareReadView`, extracts the `sfAmendments` vector, and calls the private constructor. This design enforces that production `Rules` objects are always grounded in a real ledger view. The publicly accessible constructor that takes only `presets` exists specifically for genesis ledger semantics and unit tests.

### Equality via Digest

`Impl::operator==` deliberately avoids comparing the full amendment sets. Instead it compares `digest_` values — an `optional<uint256>` hash of the ledger object containing the amendment list. If both `Impl` objects have no digest (both are genesis/preset-only rules), they're considered equal. If exactly one has a digest, they're unequal. If both have digests, it compares the digests and also asserts that `presets_` match: two `Rules` constructed from different preset configurations but the same ledger amendments would produce different behavior, making a digest-equal comparison incorrect. The outer `Rules::operator==` short-circuits on pointer identity before delegating to `Impl`, so comparing a `Rules` object with itself is O(1) pointer comparison.

## Thread/Coroutine-Local Current Rules

Transaction processing code needs access to the active `Rules` without threading the object through every call site. `setCurrentTransactionRules` and `getCurrentTransactionRules` manage a `LocalValue<std::optional<Rules>>` — a type from `xrpl/basics/LocalValue.h` that provides per-coroutine or per-thread storage. When XRPL's job system runs code on a coroutine, `LocalValue` stores a per-coroutine instance; when running on a plain thread, it falls back to thread-local semantics via `boost::thread_specific_ptr`. This dual-mode storage is critical because XRPL's application layer uses coroutines heavily, and true thread-locals would cause coroutines sharing a thread to stomp each other's rule context.

The static `LocalValue` is wrapped inside a function (`getCurrentTransactionRulesRef`) rather than declared at namespace scope. This sidesteps C++ static initialization order issues — the `LocalValue` is constructed on first call, not at program startup.

The header also provides `CurrentTransactionRulesGuard`, a RAII wrapper that saves the current rules on construction and restores them on destruction, enabling safe rule overrides in nested processing contexts and test harnesses.

## Arithmetic Precision Side Effect

`setCurrentTransactionRules` carries a non-obvious side effect: it calls `Number::setMantissaScale` immediately after storing the new rules. The `Number` type used throughout XRPL's financial arithmetic supports two mantissa ranges — `small` (standard XRP precision) and `large` (extended range for DeFi features). When `featureSingleAssetVault` or `featureLendingProtocol` is enabled, the large range is required to avoid overflow in AMM and lending calculations. Rather than having `Number` query the current rules on every arithmetic operation (which would be called millions of times per ledger), the precision mode is pushed once when rules change. If `r` is `nullopt` (no rules set), large numbers are allowed as a safe default. This push-rather-than-pull architecture is called out explicitly in a code comment.

## The `isFeatureEnabled` Free Function

`isFeatureEnabled` is the primary API for feature checks scattered throughout transaction processing. It fetches the thread/coroutine-local `Rules`, returns `false` if no rules are set, and otherwise delegates to `Rules::enabled`. The safe-default behavior (returning `false` when rules are absent) prevents accidental feature activation during startup or in code paths that haven't set up a rule context.
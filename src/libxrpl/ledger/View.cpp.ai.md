# `src/libxrpl/ledger/View.cpp`

## Role in the System

`View.cpp` implements the free-function utility layer that sits between the raw `ReadView`/`ApplyView` interfaces and higher-level transaction processing code. Where `ReadView` and `ApplyView` define *how* to access or write ledger state, this file defines *what* to do with that access — encoding business-logic queries and mutations that are common across many transaction types: expiration checks, asset freeze detection, ledger chain validation, amendment introspection, directory management, withdrawal validation, and account cleanup.

The file is organized into two clear sections: read-only **observers** that take `ReadView const&`, and state-mutating **modifiers** that require `ApplyView&`.

---

## Observers

### `hasExpired()`

Converts an `std::optional<uint32_t>` XRPL-epoch timestamp to a `NetClock::time_point` and compares it against the view's `parentCloseTime()`. The design choice to use *parent* close time rather than the current ledger's time is deliberate: the parent ledger's close time is consensus-finalized and deterministic across all validators, whereas the in-flight ledger's close time is not yet agreed upon. This makes expiry evaluation reproducible across nodes.

### `hashOfSeq()`

Navigates the ledger's multi-level skip list to retrieve the hash of a historical ledger by sequence number. The skip list has two tiers:

1. The most recent 256 predecessor hashes are stored in the current ledger's `keylet::skip()` node (the normal list). This allows direct O(1) lookup for any ledger within 256 back.
2. For ledgers older than 256, the code requires the sequence to be 256-aligned (`(seq & 0xff) == 0`). Those ledgers each carry a `keylet::skip(seq)` node holding hashes of every 256th ancestor up to their own position, making them permanent historical anchors.

Non-aligned sequences older than 256 steps simply cannot be resolved — the function returns `std::nullopt` and logs at debug level. This is an intentional design constraint: the skip list is not an arbitrary-access historical index, but a space-efficient structure for the most common lookup patterns (recent ancestors and aligned milestones).

### `areCompatible()`

Two overloads check whether a test ledger is on the same chain as a known-valid ledger. The first form accepts both as `ReadView` references; the second accepts only the valid ledger's hash and index (for callers who have not yet fetched the valid ledger object). Both forms use `hashOfSeq()` to reconstruct the expected hash at the overlapping sequence number and compare. If they differ at the same sequence, the ledgers are on incompatible forks. This is used by consensus validation machinery to detect and log chain splits.

### `isVaultPseudoAccountFrozen()`

Determines whether a vault pseudo-account's MPT share token is indirectly frozen because the vault's underlying asset is frozen. The function traverses upward through the ledger graph: MPT issuance → issuer account root → vault object → vault asset, then delegates to `isAnyFrozen()`. A `depth` parameter guards against infinite recursion in pathological configurations. The `UNREACHABLE` macros at the null-check points for the issuer account and vault object reflect ledger invariants that should never be violated in practice — those paths are excluded from coverage intentionally.

The feature-flag guard (`featureSingleAssetVault`) means the function unconditionally returns `false` before the amendment is enabled, providing clean backward compatibility.

### `isLPTokenFrozen()`

A thin wrapper that applies `isFrozen()` to both legs of an AMM pool. LP tokens are frozen if *either* of their constituent assets is frozen for the given account.

### Amendment Queries

`getEnabledAmendments()` and `getMajorityAmendments()` both read from the singleton `keylet::amendments()` SLE. The former returns a `std::set<uint256>` of all enabled amendment hashes; the latter returns a `std::map<uint256, NetClock::time_point>` of amendments that have reached validator supermajority but have not yet been activated. These are thin ledger-state accessors that make the amendment state observable without requiring callers to understand SLE field layouts.

---

## Modifiers

### `dirLink()`

Inserts an SLE into an account's owner directory and stores the returned page number back into the SLE's designated field (`sfOwnerNode` by default). Returns `tecDIR_FULL` if no page is available — a hard capacity limit enforced at the directory layer.

### `canWithdraw()` and `withdrawToDestExceedsLimit()`

Three overloads of `canWithdraw()` form a validation cascade. The most granular form takes a pre-read SLE for the destination account (avoiding a redundant `view.read()` when the caller already has it), checks for destination tag requirements via `checkDestinationAndTag()`, verifies deposit authorization via `lsfDepositAuth` + `keylet::depositPreauth`, and delegates asset-limit checking to the internal `withdrawToDestExceedsLimit()`.

The IOU/MPT asymmetry in `withdrawToDestExceedsLimit()` is architecturally important and documented in a block comment: IOU withdrawals check that the recipient's trust line limit won't be exceeded. MPT withdrawals **skip this check entirely** because vault withdrawals transfer existing tokens rather than minting new ones — the `MaximumAmount` supply cap is a minting constraint, not a transfer constraint. Returning `tesSUCCESS` unconditionally for MPTs in `std::visit` makes this policy explicit and enforced at compile time via the exhaustive visitor.

### `doWithdraw()`

Executes the physical asset transfer from a source pseudo-account to a destination. When withdrawing to self, it calls `addEmptyHolding()` to create a trust line or MPToken record if needed (tolerating `tecDUPLICATE` if one already exists). For withdrawals to a third party, it delegates to `verifyDepositPreauth()`. Before calling `accountSend()`, it asserts via `accountHolds()` that the source actually has sufficient balance — a sanity check that, if triggered, surfaces `tefINTERNAL` rather than attempting an overdraft.

### `cleanupOnAccountDelete()`

Iterates an account's entire owner directory and deletes every entry using the caller-provided `EntryDeleter` callback. The design uses a `std::function` returning `std::pair<TER, SkipEntry>` to decouple the generic traversal loop from type-specific deletion logic — the regular account delete path and AMM account delete path share this loop but inject different deleters.

A subtle and well-commented iterator invalidation issue is handled explicitly. The `dirFirst()`/`dirNext()` functions use *exposed internal state* (a `uint256& page` and `unsigned int& uDirEntry`) rather than opaque iterators. After deleting the entry at index `it`, the entry that was at `it+1` shifts to `it`. The code compensates by decrementing `uDirEntry` so the next `dirNext()` call picks up the shifted position. The `XRPL_ASSERT` guarding `uDirEntry >= 1` and the `SkipEntry` flag (which bypasses the decrement when an entry was intentionally left in place) make the invariant explicit.

The `maxNodesToDelete` parameter supports partial deletion — returning `tecINCOMPLETE` when the budget is exhausted — allowing expensive account cleanups to be spread across multiple transactions.

### `after()`

A one-liner that converts a `NetClock::time_point` to a raw tick count and compares it against a `uint32_t` mark. Factored out to avoid raw `time_since_epoch().count()` arithmetic scattered across callers.
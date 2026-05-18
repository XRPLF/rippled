# `include/xrpl/ledger/View.h`

This header is the utility layer that sits above the `ReadView`/`ApplyView` abstraction hierarchy and provides the concrete, ledger-aware operations that transaction processors actually call. Where `ReadView.h` defines the abstract interface for querying ledger state and `ApplyView.h` extends it with a checkout/update mutation model, `View.h` supplies the *algorithms* built on top of those interfaces — expiration checks, amendment queries, ledger history navigation, withdrawal authorization, and account-deletion cleanup. The file is organized into two sections that mirror the underlying hierarchy: **Observers** (read-only, take `ReadView const&`) and **Modifiers** (mutating, take `ApplyView&`).

## Time and Expiration

`hasExpired()` encodes a subtle but critical ledger rule: expiration is measured against the **parent ledger's close time**, not the current ledger being built. This is intentional — consensus agrees on the close time of already-closed ledgers, but the close time of the ledger currently under construction is not yet known to validators. The implementation compares the `parentCloseTime()` from `ReadView::header()` against the XRPL epoch–based timestamp stored in the SLE. The `after()` helper performs the symmetric check (whether a given moment has passed a raw epoch-seconds mark) and is used internally by timing logic elsewhere in the codebase.

## Skip List and Ledger History Navigation

`hashOfSeq()` implements a three-tier lookup through the XRPL's skip-list structure to retrieve the hash of an arbitrary past ledger:

1. **Trivial cases**: if the requested sequence equals the current ledger sequence, return its hash; if it's exactly one prior, return `parentHash` directly.
2. **Within 256**: Read the current ledger's `skip` keylet, which stores hashes for the 256 preceding ledgers, and index into the vector by offset.
3. **Deep history**: For sequences that are multiples of 256, read the dedicated `LedgerHashes` page for that epoch and index into it.

`getCandidateLedger()` is a compact bit-manipulation helper that computes the nearest ledger sequence that is both ≥ the requested sequence and a multiple of 256. This is the ledger that permanently stores a `LedgerHashes` page and from which any arbitrary past hash can be recovered. The arithmetic `(requested + 255) & (~255)` rounds up to the next 256 boundary in a single instruction.

Together these two functions underlie the network's ability to prove ledger ancestry without replaying the full chain.

## Ledger Compatibility Checks

`areCompatible()` provides two overloads for detecting ledger forks. The first takes two `ReadView` objects; the second accepts a known `(hash, index)` pair for the "valid" ledger alongside a candidate `ReadView` — useful when the valid ledger hasn't been fully loaded but its identity is known from consensus. In either case the logic uses `hashOfSeq()` to walk the skip list of whichever ledger is later and verify that the earlier ledger's hash appears in that skip list. A mismatch means the two ledgers cannot share an ancestry chain, i.e., one is a fork. Diagnostic output goes to a `beast::Journal::Stream` so callers control log severity.

## Amendment State

`getEnabledAmendments()` and `getMajorityAmendments()` both read the singleton `amendments` SLE from the ledger. The former returns the set of amendment hashes that are already active on-chain. The latter returns a map of amendment hash → `NetClock::time_point` for amendments that have achieved validator supermajority but have not yet activated — used by the amendment process to enforce the two-week waiting period. The type alias `majorityAmendments_t` is defined here alongside the function so callers don't need to repeat the verbose map type.

## Freeze Checks for AMM and Vault Assets

`isLPTokenFrozen()` checks whether either pool asset in an AMM liquidity-provider token pair is frozen for a given account. Because LP tokens derive their value from two underlying assets, a freeze on either one is sufficient to block transfers, so both assets are checked via `isFrozen()`.

`isVaultPseudoAccountFrozen()` handles a more complex recursive case arising from the `SingleAssetVault` feature. Vault pseudo-accounts are synthetic accounts backed by an MPT issuance; determining whether such a pseudo-account is frozen requires resolving the vault's underlying asset and checking *that* asset for freezes — which may itself be a vault-backed MPT (hence the recursion). The `depth` parameter and `maxAssetCheckDepth` guard prevent unbounded recursion in pathological configurations, returning `true` (frozen) if the depth limit is hit as a conservative safe-side default.

## Withdrawal Authorization: `canWithdraw` Overload Family

Three overloads of `canWithdraw()` form a funnel of increasing abstraction:

- The innermost form takes a pre-fetched `toSle` (the destination account's SLE), avoiding a redundant read when the caller already has it.
- The middle form looks up the destination account SLE and delegates to the first.
- The outermost form unpacks a full `STTx`, extracting `sfAccount`, `sfDestination`, `sfAmount`, and `sfDestinationTag`, then delegates to the middle.

All three ultimately enforce the same rules: the destination must exist; if `lsfRequireDestTag` is set, a destination tag must be present even for self-sends; if `lsfDepositAuth` is set, the sender must have a pre-authorized `DepositPreauth` entry; and for IOU amounts the transfer must not push the receiver beyond their trust line credit limit. The MPT path deliberately skips the credit-limit check since withdrawals transfer existing tokens rather than minting new ones.

## `doWithdraw`: Executing Vault/Broker Withdrawals

`doWithdraw()` is the state-mutating complement to `canWithdraw()`. It handles two cases: withdrawal to self (the submitting account equals the destination) and withdrawal to a third party. For self-withdrawals it calls `addEmptyHolding()` to ensure a trust line or MPToken exists, tolerating `tecDUPLICATE` if one already does. For third-party withdrawals it re-validates deposit preauthorization under mutation semantics. The actual fund transfer is delegated to `accountSend()` with `WaiveTransferFee::Yes`, reflecting that vault/broker withdrawals do not charge transfer fees.

## `dirLink`: Owner Directory Maintenance

`dirLink()` inserts a newly created SLE into an account's owner directory and records the resulting page number in the SLE's `sfOwnerNode` field (or a custom field passed via the defaulted `node` parameter). Failure returns `tecDIR_FULL` if the directory has hit the protocol-defined page limit — a situation that can only arise in practice with very large accounts.

## Account Deletion Cleanup

`cleanupOnAccountDelete()` is the core of the `DeleteAccount` transaction's multi-step iteration. It iterates the owner directory using the exposed-internal-state `dirFirst`/`dirNext` pattern and calls the caller-supplied `EntryDeleter` for each node. The `EntryDeleter` typedef is a `std::function` returning `std::pair<TER, SkipEntry>`, where `SkipEntry::Yes` tells the loop not to decrement the directory cursor — this handles cases where the deleter chose not to actually remove the current entry and the cursor therefore doesn't need to be rewound. The loop counter re-validation comment in the implementation explains the trick precisely: after a successful delete the entry that was at index `i+1` shifts to `i`, so the iterator must be decremented by one to stay valid.

The optional `maxNodesToDelete` parameter supports incremental deletion: when the limit is reached the function returns `tecINCOMPLETE`, signaling that the account delete transaction should be retried in a future ledger. This prevents a single transaction from consuming unbounded execution time or exceeding the ledger's compute budget.

## Design Observations

The strict separation between observer functions (taking `ReadView const&`) and modifier functions (taking `ApplyView&`) is not cosmetic. It enables the same observer logic to run against any view in the hierarchy — the live ledger, a sandbox, or a `PaymentSandbox` — without the caller needing to know which. The `[[nodiscard]]` attribute on every `TER`-returning function ensures callers cannot silently ignore error codes, a common pitfall in transaction processing.
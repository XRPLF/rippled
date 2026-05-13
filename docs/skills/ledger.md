# Ledger

Each ledger is an immutable snapshot: header (seq, hashes, close time) + state SHAMap + transaction SHAMap. `LedgerMaster` is the central coordinator. The module spans `Ledger` itself, the view hierarchy (`ReadView` → `ApplyView` → `OpenView`/`Sandbox`/`PaymentSandbox`), directory primitives, subscription/order-book fan-out (`BookListeners`, `OrderBookDB`), governance state (`AmendmentTable`), pending-save bookkeeping, and a large family of per-object-type helper free functions.

## Key Invariants

- Once `setImmutable()` is called, the ledger and its SHAMaps cannot change; only immutable ledgers can be shared across threads. Mutable ledgers must not be shared.
- Every server always has an open ledger; the open ledger cannot close until previous consensus completes.
- Ledger header hashes to the ledger's identity hash; includes state root, tx root, parent hash, total coins, close time.
- `LedgerMaster` tracks: `mPubLedger` (last published), `mValidLedger` (last validated), `mLedgerHistory` (cache).
- Validation requires minimum trusted validations (`minVal`); filtered by Negative UNL.
- Open ledgers store transactions without metadata; closed ledgers store `addVL(tx)||addVL(meta)` and produce `TxMeta` on apply.
- Trust-line `sfBalance` is always stored "low account's perspective"; helpers negate when querying from high side. The `sfLowLimit.account < sfHighLimit.account` ordering is the trust-line orientation invariant — every access decides which side is "us" via `AccountID` comparison.
- Directory invariant: page keys are chosen so the low 96 bits of every token in an NFT page are strictly less than the page key's low 96 bits; for owner/order-book directories, page 0 is the anchor and `sfIndexPrevious` on root points to the tail.
- `PendingSaves` invariant: exactly one of `saveLedgerAsync`/`pendSaveValidated` may run for a given ledger sequence at a time; second caller observes `started == true` and bails.
- `ApplyStateTable` pointer-identity invariant: `erase(sle)` and `update(sle)` require the exact same `shared_ptr` obtained from `peek()` on the same view instance. Crossing views is a `LogicError`.
- `RawStateTable` state-machine collapse: `erase` after `insert` removes the entry entirely (net zero); `insert` after `erase` upgrades to `replace`; double-erase is a `LogicError`.

## Common Bug Patterns

- Modifying a ledger after `setImmutable()` corrupts shared state; always check `mImmutable` before mutation.
- Gap detection: if ledgers 603 and 600 exist but 601-602 are missing, `LedgerMaster` requests 602 first, then backfills 601.
- `InboundLedger::gotData()` queues data for processing; calling `done()` before all data arrives creates incomplete ledgers.
- `checkAccept` won't accept a ledger that isn't ahead of the last validated ledger; stale validations are silently ignored.
- Calling `ApplyViewImpl::apply()` twice or using the view after apply: the only valid operation post-apply is destruction.
- Passing an SLE from `peek()` on view A to `erase()`/`update()` on view B: `ApplyStateTable` enforces pointer identity and `LogicError`s.
- Forgetting that `read()` is change-aware but `slesBegin/End` iterates only the base — pending inserts won't appear in SLE iteration on `ApplyViewBase`.
- Comparing iterators across different `ReadView` instances: `XRPL_ASSERT` fires in debug; UB in release.
- Stale `OpenView::txCount` ordinal in nested/batch views — must use `batch_view_t` constructor to capture `baseTxCount_`.
- Calling `removeExpired`/`deleteSLE` in preclaim — preclaim is `ReadView`-only; expiry-driven deletion only happens in doApply.
- Forgetting that `directSendNoFee` is not `[[nodiscard]]` (for `DirectStep.cpp` compatibility) — its return must still be inspected.
- Accessing trust-line endpoints by raw low/high without first computing orientation — use the trust-line helpers that take `(account, peer)` and resolve which slot to touch.
- Constructing a `PaymentSandbox` from `ApplyView&` when nested within another payment: the nested form takes `PaymentSandbox const*` so `DeferredCredits` chain to the parent. Wrong constructor = double-spend escape.
- Updating an SLE then publishing subscriptions before `havePublished` advances: re-entrant publishers see partial state.
- Submitting to `OrderBookDB` while holding `mLock` from a callback — `setup_` ingests under its own write lock and may re-enter.
- `AcceptedLedgerTx` constructor asserts `!ledger->open()` — constructing from an open ledger aborts in debug.
- Calling `closeChannel` before `sfAmount >= sfBalance` invariant holds — the `XRPL_ASSERT` inside will abort; fix the calling transactor, not the helper.
- `ReadView` copy/move constructors always re-initialize `sles(*this)` and `txs(*this)` — subclasses must not rely on memberwise copy of those range objects.
- `forEachItemAfter` hint-page optimization: if the hint is stale the code falls back to linear scan but still returns `false` if `after` key is never found; callers must handle this as an invalid cursor.
- `dirIsEmpty` requires both empty `sfIndexes` *and* zero `sfIndexNext` — an empty root page does not mean an empty directory if subsequent pages exist.

## Ledger Entry Types

- Defined in `ledger_entries.macro` using `LEDGER_ENTRY(type, code, class, name, fields)`.
- Each entry has an `SOTemplate` defining required/optional fields.
- Key computation: `Indexes.cpp` computes unique keys (keylets) for each ledger object type.
- `STLedgerEntry` wraps the serialized data with type-safe field access.
- Pseudo-account types (AMM, Vault, LoanBroker) are discovered by scanning `ltACCOUNT_ROOT` SOTemplate for `SField::sMD_PseudoAccount`-flagged fields; no manual registration.

## View Hierarchy

```
ReadView (abstract, read-only)
  └── DigestAwareReadView    (adds per-entry digest for CachedView)
        └── Ledger           (final; owns stateMap_ + txMap_)
  └── OpenView               (mutable; ReadView + TxsRawView; delta over base)
  └── detail::ApplyViewBase  (ApplyView + RawView; buffered via ApplyStateTable)
        ├── ApplyViewImpl    (commit path; produces TxMeta; carries deliver_)
        ├── Sandbox          (discardable; flush via apply(RawView&))
        └── PaymentSandbox   (overrides credit/balance hooks; DeferredCredits)
```

- `ApplyStateTable` (per-tx buffer): actions `cache`/`insert`/`modify`/`erase`; generates `TxMeta` with `sfPreviousFields`/`sfFinalFields`/`sfNewFields` driven by `SField::sMD_*` flags; threads `sfPreviousTxnID`/`sfPreviousTxnLgrSeq` on affected account roots and trust-line endpoints. On `apply()` the buffer is flushed into the base `RawView` and the table is reset; using the table afterward is UB. A byte-equal `sfModifiedNode` is silently omitted from metadata.
- `RawStateTable` (used by `OpenView` and `RawStateTable::apply` flush): three actions only; state-machine collapse — `insert + erase → removed entirely`; `insert + replace → insert with new SLE`; `erase + insert (modify) → replace`. No `TxMeta` is produced because `RawView` is the post-apply mutation surface.
- Both tables use `boost::container::pmr::monotonic_buffer_resource` with a 256 KB initial arena; `unique_ptr` for stable address so map allocators work after move. Memory is released only when the table is destroyed — long-lived tables leak peak working set.
- `ReadViewFwdRange` is the iterator/range adapter used for `sles`/`txs` traversal; iterator equality is anchored to the owning view (`XRPL_ASSERT` cross-view comparisons in debug). Virtual clone via `iter_base::copy()` enables value-semantics copying. Deferred dereference caches the result in `mutable std::optional<value_type> cache_`; `operator++` clears the cache.
- `CachedView` (`CachedLedger = CachedView<Ledger>`): two-level cache — per-view `map_<key, digest>` plus process-wide `CachedSLEs` (`TaggedCache<uint256, SLE const>`) keyed by digest. The per-view map uses `hardened_hash` to defeat hash-flooding from adversarial keylet sequences. Hit/hitExpired/miss counters distinguish full hit, digest-known-but-SLE-evicted, and cold miss. The expensive `base_.digest()` and `base_.read()` calls happen outside the lock; `mutex_` protects only the key→digest map.
- Hooks pattern: `balanceHookIOU/MPT`, `ownerCountHook` (read side, on `ReadView`) and `creditHookIOU/MPT`, `adjustOwnerCountHook`, `issuerSelfDebitHookMPT` (write side, on `ApplyView`) are no-ops by default; `PaymentSandbox` overrides them to prevent within-payment double-spend.
- `isDryRun` path in `apply(OpenView&...)`: full `TxMeta` is built and returned as `std::optional<TxMeta>`, but state changes and `rawTxInsert` are suppressed. Used for fee simulation.

## Directory Structures

Three distinct paged-list flavors, all `ltDIR_NODE`-based:

- **Owner / book directories** (`ApplyView::dirInsert`/`dirAppend`/`dirRemove`/`dirDelete`): root at page 0; `sfIndexNext`/`sfIndexPrevious` linked; root's `sfIndexPrevious` points to tail for O(1) append. `dirAppend` preserves insertion order (offers only, asserted); `dirInsert` keeps sorted order within each page. Page overflow detected via deliberate `uint64_t` wraparound (compile-time `static_assert`ed). `describe` callback brands each new page with type-specific fields (e.g., `sfOwner`).
- **NFToken pages** (`NFTokenHelpers`): tokens packed into `STArray`-bearing pages, sorted by `compareTokens()` (low 96 bits, then full ID). Last page anchored at `keylet::nftpage_max(owner)`. Split algorithm respects equivalence groups (identical low 96 bits); merge across adjacent pages on remove. `fixNFTokenPageLinks` amendment changes empty-last-page handling.
- **Quality-keyed order books** (`BookDirs`): two-level — `succ()` finds next quality directory in `[root_, getQualityNext(root_))`, then `cdirFirst`/`cdirNext` walks pages within that quality. `BookDirs` iterator transparently crosses quality boundaries.

The `Dir` class is a simple range adaptor (NFTokenOffer directories + unit tests); `next_page()` is public to allow page-skipping traversal (used by `notTooManyOffers`).

## Helper Module (`include/xrpl/ledger/helpers/`)

Free functions per ledger-object type. The asset-agnostic dispatcher is `TokenHelpers.h`, which routes `Asset` (`std::variant<Issue, MPTIssue>`) via `std::visit` to `RippleStateHelpers` (IOU) or `MPTokenHelpers` (MPT).

Conventional split:
- Stateless / preflight-safe checks: take `ReadView const&`
- State-mutating: take `ApplyView&`
- Two-phase pattern: read-only preclaim function (`credentials::valid`/`validDomain`) paired with mutating doApply counterpart (`verifyDepositPreauth`/`verifyValidDomain`) that prunes expired entries

Key files: `AMMHelpers`, `AccountRootHelpers`, `CredentialHelpers`, `DelegateHelpers`, `DirectoryHelpers`, `EscrowHelpers`, `MPTokenHelpers`, `NFTokenHelpers`, `OfferHelpers`, `PaymentChannelHelpers`, `PermissionedDEXHelpers`, `RippleStateHelpers`, `TokenHelpers`, `VaultHelpers`.

Policy enums (used to avoid bare bools): `FreezeHandling`, `AuthHandling`, `SpendableHandling`, `WaiveTransferFee`, `AllowMPTOverflow`, `AuthType` (`StrongAuth`/`WeakAuth`/`Legacy` — Legacy maps to StrongAuth for MPT, WeakAuth for IOU), `TruncateShares`.

## AMM Rounding Contract

The pool invariant `sqrt(asset1 × asset2) >= LPTokenBalance` is non-negotiable, so every formula has explicit directional rounding:

- Swap-in: output rounds **down** (trader gets less, pool retains).
- Swap-out: input rounds **up** (trader pays more).
- LP token deposit: tokens **down**, assets **up**.
- LP token withdrawal: tokens **up**, assets **down**.

`fixAMMv1_1` introduced per-step rounding (vs end-only); `fixAMMv1_3` extended this discipline to LP/deposit/withdraw paths via `multiply(balance, frac, mode)` and the `getRoundedAsset`/`getRoundedLPTokens` wrappers (two overloads each — direct and lambda-deferred). Pre-amendment paths must be preserved for historic replay.

`adjustLPTokens`: avoids precision loss when adding small token amounts to large `LPTokensBalance` by computing `(balance + tokens) - balance` rather than `tokens`. Becomes a no-op under `fixAMMv1_3`.

`changeSpotPriceQuality`: aligns AMM synthetic offer to CLOB best quality. Solves a quadratic (or linear, for the alternate constraint) and takes the smaller binding result. `fixAMMv1_1` switched the starting side to always-XRP-first to avoid XRP-drop discretization undershoot. `detail::reduceOffer` applies 0.01% rescue multiplier when quality still falls below target.

`solveQuadraticEqSmallest()` uses the numerically stable "citardauq" formula (Blinn's paper) to avoid catastrophic cancellation when `b > 0` in the standard form.

## MPT Specifics

- `OutstandingAmount` can transiently exceed `MaximumAmount` during payment-engine routing — `AllowMPTOverflow::Yes` raises the ceiling to `UINT64_MAX` for that case; direct sends use `No` and strict cap. The `fixSecurity3_1_3` amendment makes `accountSendMulti` accumulate in exact `uint64_t` (not `STAmount`/`Number`) to avoid 19-digit precision loss in aggregate overflow checks.
- `selfDebit` field on `IssuerValueMPT` in `PaymentSandbox::DeferredCredits` tracks issuer-as-seller offers because the payment engine credits first; `balanceHookSelfIssueMPT` caps available issuance at `origBalance - selfDebit`.
- Two-phase auth: `requireAuth` (preclaim, `ReadView`) checks the static authorization predicates; `enforceMPTokenAuthorization` (doApply, `ApplyView`) handles the case where a domain-authorized holder lacks an `MPToken` SLE in preclaim — it lazily allocates the SLE on the fly and consumes the `priorBalance` reserve.
- `lockEscrowMPT` does NOT change `OutstandingAmount` (tokens are still in circulation while escrowed); `unlockEscrowMPT` decreases `OutstandingAmount` only by the fee delta (gross - net) under `fixTokenEscrowV1`.
- `isVaultPseudoAccountFrozen()` recursively checks whether a vault-backed MPT issuance is frozen by the vault's underlying asset; a `depth` parameter bounds recursion (purely defensive — nested vaults cannot currently be created).

## IOU Trust-Line Specifics

- Orientation: every trust line stores its endpoints in fixed `sfLowLimit`/`sfHighLimit` slots based on `AccountID` comparison. Helpers like `accountHolds`, `accountFunds`, `trustCreate`, `trustDelete` take `(account, peer)` and resolve the slot internally — never index by raw low/high outside the helpers.
- Three freeze tiers: `isIndividualFrozen` (issuer froze this specific holder), `isFrozen` (global freeze flag), `isDeepFrozen` (transitive freeze through deep-freeze chains). Each has distinct policy implications for sends, offers, and AMM participation; helper queries take a `FreezeHandling` enum to select policy.
- `rippleCredit`/`rippleSend` apply transfer fees only when the issuer is neither endpoint and `WaiveTransferFee::No`; reserve/quality limits enforced via `accountFunds` rather than raw balance.
- Trust-line deletion (`trustDelete`) requires both endpoints to be at default state (zero balance, default limits/flags); helpers compute "default" against the post-tx state, not raw fields.
- `updateTrustLine` (static helper in `RippleStateHelpers.cpp`): when a sender's balance crosses zero and meets seven specific conditions (zero limit, zero quality flags, no freeze, etc.), it releases the sender's reserve and signals the caller to delete the line.

## Credential System

- Two-phase enforcement mirrors the `ReadView`/`ApplyView` split: `credentials::valid()` and `credentials::validDomain()` are read-only (preclaim); `verifyDepositPreauth()` and `verifyValidDomain()` are mutating (doApply) and delete expired credential objects as a side effect.
- `credentials::deleteSLE()` handles two owner directories (issuer and subject) with reserve accounting that shifts ownership at `lsfAccepted` time. Before acceptance only the issuer pays the reserve; after, the subject does.
- `checkFields()` validates `sfCredentialIDs` (`STVector256`); `checkArray()` validates `STArray` credential pairs and uses `sha512Half(issuer, credentialType)` for duplicate detection.
- `authorizedDepositPreauth()` uses a `lifeExtender` vector to keep `SLE const` shared pointers alive while the `Slice`-based set is in scope — forgetting this causes dangling pointer reads.

## Pseudo-Accounts

Synthetic `AccountRoot` SLEs owned by protocol objects (AMM, Vault, LoanBroker). Address derived from `sha512Half(attempt, parentHash, ownerKey)` → RIPESHA in a loop up to `maxAccountAttempts = 256` (consensus-critical constant). Flags `lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth`; `sfSequence = 0` under `featureSingleAssetVault`/`featureLendingProtocol`. `isPseudoAccount(sle, filter?)` checks for any field tagged `SField::sMD_PseudoAccount` (currently `sfAMMID`, `sfVaultID`, `sfLoanBrokerID`). Pseudo-accounts bypass reserve requirements in `xrpLiquid`.

Amendment checks are the *caller's* responsibility, not `createPseudoAccount`'s. The function asserts the passed `ownerField` carries `sMD_PseudoAccount`.

## Vault Math

`VaultHelpers` converts between asset and share denominations using `sfScale` (decimal precision) and tracks `sfLossUnrealized` separately on deposit vs withdraw paths — the asymmetry is intentional: deposits price against gross `sfAssetsTotal`; withdrawals subtract `sfLossUnrealized` first (existing holders bear the loss, not new depositors). `TruncateShares` policy controls whether sub-unit share remainders are rounded to zero or rejected.

Empty-vault bootstrap: when `sfAssetsTotal == 0`, initial share allocation is `assets × 10^sfScale` (truncated), establishing the starting exchange rate. This reduces susceptibility to the first-depositor donation attack.

## Ledger Timing (`LedgerTiming.h`)

Adaptive close-time binning prevents clock-skew disagreements. Resolutions: `{10, 20, 30, 60, 90, 120}` seconds; default 30, genesis 10. Adjustment is asymmetric:
- On disagreement, coarsen every ledger (`decreaseLedgerTimeResolutionEvery = 1`)
- On agreement, refine only every 8th ledger (`increaseLedgerTimeResolutionEvery = 8`)

`roundCloseTime` is epoch-anchored (uses `time_since_epoch()`, not local offset) for deterministic agreement. `effCloseTime` enforces strict monotonicity: `max(rounded, priorCloseTime + 1s)`. `time_point{}` is the sentinel for "no agreed close time" and is returned unchanged.

## Skip List

Two-tier on-ledger structure for historical hash lookup:
- `keylet::skip()` (the rolling 256-page): hashes of the 256 immediate ancestors; updated every ledger.
- `keylet::skip(seq)`: every 256-aligned ledger stores a permanent record. `getCandidateLedger(seq)` rounds up to the nearest 256-aligned sequence.

Non-aligned ledgers older than 256 are unreachable — `hashOfSeq` returns `nullopt`.

## Canonical Transaction Ordering (`CanonicalTXSet`)

Retry queue between consensus passes. Sort key: `(account ⊕ salt, SeqProxy, txId)`.
- **Salt**: `LedgerHash` XORed into `AccountID`; prevents account-address mining for persistent ordering advantage. Refreshed by `reset()` each round.
- **`SeqProxy`**: sequences sort before tickets unconditionally (so `TicketCreate` always applies before ticket consumers).
- **`popAcctTransaction()`**: returns next eligible same-account tx — either ticket-based, or sequence exactly `+1` from current.

`Key::operator==` compares only `txId_` (asymmetric with `operator<`).

## Subscription Fan-Out

`BookListeners` and `OrderBookDB` together drive WebSocket book-stream subscriptions:

- `BookListeners`: per-book (`Book`-keyed) registry of `InfoSub::wptr`. `publish` builds a `MultiApiJson` once and dispatches by API version, expiring dead weak pointers in-place. Locking is per-listener-set (`std::recursive_mutex`), not global.
- `OrderBookDB`: scans the ledger's `dirNode` index to build `xrpBooks_` (XRP-side, O(1) checked) and `allBooks_` (IOU/MPT, scanned). Scans are throttled by `setup_seq_` — only re-scan on ledger change. `publishOrderBook` walks affected listeners; `havePublished` dedups so the same ledger isn't re-fanned-out across overlapping subscription updates.
- Ingest path under `mLock` (write); subscribe/unsubscribe under read lock. Callbacks must not re-enter `setup_`.
- `havePublished` is a `hash_set<uint64_t>` shared across all `BookListeners::publish()` calls for a single transaction, preventing duplicate delivery to subscribers registered on multiple affected books.

## Accepted Ledger Tx (`AcceptedLedgerTx`)

Eagerly-materialized projection of a transaction-in-ledger: tx, meta, deserialized account fields, affected-accounts list, JSON-serialization cache. Construction is the heavyweight step; downstream consumers (RPC, subscription) read fields by reference. The class is immutable post-construction; thread-safe to share.

Constructor asserts `!ledger->open()`. For `ttOFFER_CREATE` transactions where `account != amount.getIssuer()`, `owner_funds` is injected into `mJson` via `accountFunds(fhIGNORE_FREEZE, ahIGNORE_AUTH)` — a read-time annotation for order-book subscribers, not ledger state.

`getEscMeta()` returns `mRawMeta` as a SQL blob literal; used by `Node.cpp` for transaction DB inserts.

## Pending Saves (`PendingSaves`)

State machine tracking in-flight `Ledger` → DB writes. Each entry keyed by ledger sequence; transitions `requested → started → finished`. `pendSave` is idempotent — second caller for the same sequence is a no-op. Synchronous callers in `shouldWork(seq, true)` block on a `condition_variable` until the in-progress write completes. `getSnapshot()` returns a copy of the map; `LedgerMaster::getValidatedRange()` uses it to exclude in-progress sequences from the reported range.

## Amendment Table (`AmendmentTable`)

Governance state for protocol amendments. Each amendment has a `VoteBehavior`:
- `DefaultYes` — vote yes unless config overrides
- `DefaultNo` — vote no unless config overrides
- `Obsolete` — never vote yes, even with config override; `LogicError` if config tries

`TrustedVotes` tracks per-validator amendment votes across ledgers with anti-flapping (a validator must consistently vote for N consecutive flag ledgers before counting toward majority). The flag-ledger cadence and majority threshold are consensus-critical constants. `doVoting` produces the `EnableAmendment`/`Veto` pseudo-transactions inserted at flag ledger close.

Two-layer API: the pure-virtual `doVoting(LedgerIndex, set<uint256>, majorityAmendments_t)` is wrapped by a non-virtual `doVoting(shared_ptr<ReadView const>, ...)` that calls `getEnabledAmendments()` and `getMajorityAmendments()` from `View.h`. This decouples the implementation from ledger view types.

`needValidatedLedger(seq)` is an optimization gate — most ledgers have no effect on amendment voting; only flag ledgers need the full `doValidatedLedger` pass.

## Review Checklist

- New ledger entry types: add to `ledger_entries.macro`, implement keylet in `Indexes.cpp`, verify acquisition code in `InboundLedger`/`LedgerMaster`, check `LedgerCleaner` handling.
- New pseudo-account types: tag the key field with `SField::sMD_PseudoAccount` in `sfields.macro`; `isPseudoAccount` picks it up automatically. Caller, not `createPseudoAccount`, owns the amendment gate.
- New asset operations: extend `Asset` variant + add branches in `TokenHelpers.h` dispatchers. Don't reach into IOU- or MPT-specific helpers directly unless intentionally bypassing the dispatch layer.
- Helper functions touching balances: respect the read/write hook protocol so `PaymentSandbox` correctly defers credits.
- AMM math changes: gate behind an amendment; preserve the pre-amendment formula path.
- Directory changes: account for both legacy (unsorted) and modern pages in iteration; verify `cdirNext`/`dirNext` cursor semantics if deleting during iteration (see `cleanupOnAccountDelete` workaround in `View.cpp` around line 485).
- New amendment: register `VoteBehavior`; if `Obsolete`, ensure no config path can flip it; add to `TrustedVotes` accounting if it should be majority-tracked.
- New subscription stream: if order-book-related, register with `BookListeners`; for ledger-wide streams use `OrderBookDB::havePublished` to dedup.
- Escrow with non-XRP assets: use `escrowUnlockApplyHelper<T>` (template specialized for `Issue` / `MPTIssue`); check `createAsset` flag gating and `lockedRate` capping logic.
- Delegate transactions: call `checkTxPermission` first; only call `loadGranularPermission` when broad permission check fails.

## Key Patterns

### Immutability Guard
```cpp
// After this, no mutations allowed on the ledger or its SHAMaps
inline void SHAMap::setImmutable()
{
    XRPL_ASSERT(state_ != SHAMapState::Invalid, "...");
    state_ = SHAMapState::Immutable;
}
// VERIFY: code never calls peek()/insert()/erase() after setImmutable()
```

### New Ledger Entry Keylet
```cpp
// REQUIRED: every new ledger entry type needs unique keylet computation
Keylet keylet::myEntry(AccountID const& id)
{
    return {ltMY_ENTRY,
        sha512Half(std::uint16_t(spaceMyEntry), id)};
}
// Also add to ledger_entries.macro and Indexes.cpp
```

### Peek/Update Contract
```cpp
// peek() returns shared_ptr<SLE>; you MUST call update() or erase() with
// the SAME pointer on the SAME view. Crossing views is a LogicError.
auto sle = view.peek(keylet::account(id));
sle->setFieldU32(sfSequence, seq + 1);
view.update(sle);  // promotes cache → modify in ApplyStateTable
```

### Two-Phase Expiry Cleanup
```cpp
// preclaim (ReadView) — detect but don't mutate
if (credentials::validDomain(view, domainID, account) == tecEXPIRED)
    /* allow through; doApply will clean up */;

// doApply (ApplyView) — mutate
if (auto ter = verifyValidDomain(view, domainID, account, j); ter != tesSUCCESS)
    return ter;  // expired credentials deleted as side effect
```

### Directional Multiply (AMM)
```cpp
// post-fixAMMv1_3: rounding mode at the final multiply, not at toSTAmount
auto const tokens = getRoundedLPTokens(
    rules,
    lptAMMBalance,
    [&] { return /* fractional formula */; },
    IsDeposit::Yes);  // → Number::downward
```

### Nested PaymentSandbox
```cpp
// Inside a payment step that needs its own sandbox: chain DeferredCredits
PaymentSandbox inner(&outer);   // PaymentSandbox const* form
// ... inner.creditHookIOU(...) defers against the outer's table too
inner.apply(outer);             // flushes deferred credits up one level
```

### Subscription Fan-Out
```cpp
// publish once, dispatch by version; havePublished shared across all books
MultiApiJson msg = buildBookUpdate(...);
listeners.publish(msg, havePublished);
db.havePublished(ledgerSeq);  // dedup across overlapping streams
```

### Sandbox Commit-or-Discard
```cpp
Sandbox sb(&ctx_.view());
// ... all mutations through sb ...
if (result == tesSUCCESS)
    sb.apply(ctx_.rawView());
// else sb destroyed → changes evaporate
```

## Key Files

- `src/xrpld/app/ledger/Ledger.h` / `src/libxrpl/ledger/Ledger.cpp` — ledger class, genesis/successor/load constructors, immutable transition, skip list, NegUNL
- `src/xrpld/app/ledger/detail/LedgerMaster.cpp` — central coordinator
- `src/xrpld/app/ledger/detail/InboundLedger.cpp` — ledger acquisition
- `include/xrpl/protocol/detail/ledger_entries.macro` — entry type definitions
- `src/libxrpl/protocol/Indexes.cpp` — keylet computation
- `include/xrpl/ledger/ReadView.h` + `ApplyView.h` + `OpenView.h` + `RawView.h` — view interface hierarchy
- `include/xrpl/ledger/detail/ApplyStateTable.h` / `RawStateTable.h` / `ApplyViewBase.h` / `ReadViewFwdRange.h` + `.ipp` — buffered mutation tables, range adapters, `TxMeta` generation
- `include/xrpl/ledger/Sandbox.h` / `PaymentSandbox.h` / `ApplyViewImpl.h` — concrete view types
- `include/xrpl/ledger/CachedView.h` / `CachedSLEs.h` — two-level SLE cache (hardened-hash inner map)
- `include/xrpl/ledger/CanonicalTXSet.h` — retry-pass deterministic ordering
- `include/xrpl/ledger/LedgerTiming.h` — close-time binning
- `include/xrpl/ledger/Dir.h` / `BookDirs.h` — directory iteration
- `include/xrpl/ledger/BookListeners.h` / `OrderBookDB.h` — subscription fan-out
- `include/xrpl/ledger/AcceptedLedgerTx.h` — eager tx-in-ledger projection
- `include/xrpl/ledger/AmendmentTable.h` — governance state, `VoteBehavior`, `TrustedVotes`
- `include/xrpl/ledger/PendingSaves.h` — in-flight DB write coalescing
- `include/xrpl/ledger/View.h` — free-function utility layer (expiry, `hashOfSeq`, `areCompatible`, `dirLink`, `canWithdraw`, `cleanupOnAccountDelete`)
- `include/xrpl/ledger/helpers/*` — per-object-type free functions
- `src/libxrpl/ledger/helpers/*` — implementations (AMM math, NFT page split, credential lifecycle, MPT overflow, vault math)
- `src/libxrpl/ledger/ApplyView.cpp` — directory `createRoot`, `findPreviousPage`, `insertKey`, `insertPage` (in `namespace xrpl::directory`)
- `src/libxrpl/ledger/PaymentSandbox.cpp` — `DeferredCredits` IOU/MPT shadow tables, post-switchover balance algorithm
- `src/libxrpl/ledger/OpenView.cpp` — `RawStateTable` + `txs_map` delta accumulation, PMR arena
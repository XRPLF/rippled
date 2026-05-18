# `OfferCancel.cpp` — DEX Offer Cancellation Transactor

## Role and Purpose

`OfferCancel.cpp` implements the `OfferCancel` transaction type for the XRPL decentralized exchange (DEX). Its sole job is to remove an existing limit order (offer) that the submitting account previously placed on the order book. The file is deliberately minimal — fewer than 65 lines of logic — because the complexity of tearing down an offer is entirely encapsulated in the shared `offerDelete` helper.

The class lives in the `dex/` subdirectory alongside `OfferCreate.cpp` and the AMM transactors, forming the full lifecycle of DEX participation. `OfferCancel` is the simplest of these; it reads one field from the transaction, finds the ledger object, and delegates destruction.

## Three-Phase Transactor Pattern

`OfferCancel` inherits from `Transactor` and participates in the standard three-phase validation/application pipeline that the XRPL transaction engine uses for every transaction type.

**`preflight`** is a static, read-only, ledger-free check. It only verifies that `sfOfferSequence` is non-zero. A zero value is semantically meaningless (sequence numbers on XRPL start at 1), so returning `temBAD_SEQUENCE` here stops the transaction before any network consensus work is done. This check costs almost nothing and prevents a class of trivially malformed transactions.

**`preclaim`** has access to a `ReadView` of the current ledger but must not modify it. It performs two checks: first, that the submitting account actually exists (`terNO_ACCOUNT` if not); second, that `sfOfferSequence` is strictly less than the account's current `sfSequence`. This second check is a forward-reference guard — an offer can only exist if the account has already submitted the transaction that created it, and each transaction increments the sequence. If `offerSequence >= account.sfSequence`, the offer could not have been created yet, making the cancel request either a mistake or an attempted attack, and `temBAD_SEQUENCE` is returned. The ledger is not searched for the specific offer here; that lookup is deferred to `doApply`.

**`doApply`** performs the actual mutation. It re-reads the account (a defensive `tefINTERNAL` guard — marked `LCOV_EXCL_LINE` because it should be unreachable after `preclaim` — acknowledges the logical impossibility without skipping the null check entirely). It then calls `view().peek()` to get a mutable handle to the offer SLE via `keylet::offer(account_, offerSequence)`. If the offer is not found, the method returns `tesSUCCESS` without error.

## Idempotent Success on Missing Offers

The most deliberate non-obvious design choice is that canceling a non-existent offer is **not an error**. `doApply` logs a debug message and returns `tesSUCCESS` regardless. This matters for real-world reliability: an offer may have been consumed by a crossing trade between when a client submitted the cancel and when consensus executes it. Returning success in this case prevents the client from needing to distinguish between "cancel succeeded" and "offer was already gone," and avoids forcing a fee charge for an operation that became a no-op through normal trading activity.

## `offerDelete` — The Bookkeeping Delegate

The actual removal work is done by `offerDelete()` from `xrpl/ledger/helpers/OfferHelpers.h`. Reading its implementation reveals why it's a shared helper rather than inline logic: canceling an offer is structurally identical to expiring an offer during payment processing or deleting an offer during account deletion. The helper removes the offer SLE from three places:

1. The account's owner directory (tracking objects toward the reserve requirement).
2. The primary order book directory identified by `sfBookDirectory`.
3. Any additional book directories if `sfAdditionalBooks` is present, which applies to hybrid domain offers (flagged `lsfHybrid`) that participate in multiple order books simultaneously.

It then decrements the account's owner count via `adjustOwnerCount`, releasing the XRP reserve held against the offer. The fact that `offerDelete` gracefully handles a null SLE by returning success allows callers to be loose about whether the object exists before calling it — though `OfferCancel::doApply` already guards against this with its `peek` check.

## `peek` vs `read` and the Mutability Contract

Within `doApply`, the account is fetched with `view().read()` (returning a `shared_ptr<SLE const>`) because it is only inspected. The offer is fetched with `view().peek()` (returning a mutable `shared_ptr<SLE>`) because `offerDelete` must erase it. This distinction is enforced by the `ApplyView` interface: passing a `const` SLE to `offerDelete` would fail to compile, making the mutability intent explicit at the call site.

## `ConsequencesFactory` and Transaction Consequences

`OfferCancel` declares `ConsequencesFactory{Normal}`, which means the engine treats it as a regular transaction: it may fail with a fee charged, but it does not block other transactions in a batch from executing. This contrasts with `Blocker`-typed transactions that would abort a batch if they fail. The Normal classification is correct since a failed cancel has no side effects on other account operations.
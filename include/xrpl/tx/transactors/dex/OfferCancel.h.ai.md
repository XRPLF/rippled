# `OfferCancel.h` — DEX Offer Cancellation Transactor

`OfferCancel` is the transactor responsible for processing `OfferCancel` transactions on the XRP Ledger's decentralized exchange (DEX). It gives account holders a way to explicitly remove a standing offer they previously placed, identified by the offer's original sequence number. The implementation is deliberately minimal — the actual ledger bookkeeping is delegated to the shared `offerDelete` helper, and the three-phase pipeline (`preflight` → `preclaim` → `doApply`) performs only the checks that are specific to this transaction type.

## Position in the Transactor Framework

Like all transactors, `OfferCancel` inherits from `Transactor` and overrides `doApply()`, while exposing static `preflight` and `preclaim` methods that the framework invokes via `invokePreflight<T>` and the preclaim dispatch. The base class handles signature verification, fee deduction, sequence number consumption, and the outer retry/reset logic — `OfferCancel` only adds domain-specific validation on top.

The `ConsequencesFactory` is set to `Normal`, in contrast to `OfferCreate` which uses `Custom`. `Normal` means the framework can model this transaction's consequences without consulting the transactor: no funds are reserved or locked as a side effect of the cancellation itself beyond the fee. `OfferCreate` needs `Custom` because it may reserve owner reserves and lock funds in an order book, requiring per-transaction consequence computation.

## Validation Pipeline

**`preflight`** performs a single stateless check: it rejects the transaction with `temBAD_SEQUENCE` if `sfOfferSequence` is zero. A zero value is nonsensical — no valid offer can carry sequence number zero — and this is the only field-level validation needed beyond what `preflight1`/`preflight2` in the base already handle (account field, fee, flags, signature).

**`preclaim`** reads the submitter's account from the ledger (returning `terNO_ACCOUNT` if absent, which would be highly unusual at this stage) and then enforces a temporal ordering invariant: the offer's sequence number must be strictly less than the account's current sequence number. If the `sfOfferSequence` value is greater than or equal to the account's current sequence, the offer could not yet exist on the ledger, so the transaction is rejected with `temBAD_SEQUENCE`. This check prevents clients from submitting cancellations for offers that haven't been created yet, which would otherwise succeed vacuously.

## Application Logic and Idempotency

`doApply()` resolves the target offer via `keylet::offer(account_, offerSequence)` — a deterministic ledger key constructed from the account ID and sequence number. If the offer entry exists in the current view, `offerDelete` is called to remove it along with its order book directory entries and any associated bookkeeping. If the offer is not found — because it was already consumed by a matching trade, previously cancelled, or expired — `doApply` returns `tesSUCCESS` anyway.

This idempotent behavior is an intentional protocol design decision. From a client's perspective, a successful `OfferCancel` means the offer is no longer active, regardless of whether it was removed by this transaction or had already been cleaned up by other means. Returning a failure code in the "offer not found" case would be misleading and would require clients to distinguish between "offer was live and got cancelled" versus "offer was already gone," which has no practical value. The fee is still charged — the transaction is valid and was processed — but no ledger state changes result.

## Relationship to `offerDelete`

The actual removal work — unlinking the offer from the owner directory, removing it from the order book directory, updating owner counts — is entirely encapsulated in `offerDelete` from `xrpl/ledger/helpers/OfferHelpers.h`. `OfferCancel::doApply` is a thin policy layer: it determines *which* offer to delete and *whether* to call `offerDelete`, but not *how* deletion works. The same `offerDelete` primitive is shared with offer-crossing in `OfferCreate` and with expiry cleanup elsewhere in the engine, which keeps the teardown logic consistent and centralized.

## Contrast with `OfferCreate`

The structural contrast with `OfferCreate` makes the design philosophy clear. `OfferCreate` brings in `Quality.h`, defines `PaymentSandbox` and `Sandbox` interactions, exposes `applyGuts`, `flowCross`, and `applyHybrid` private methods, and uses a `Custom` consequences factory — all reflecting the complexity of order book insertion and potential immediate crossing. `OfferCancel` has none of this: it imports only `TxFlags.h` and `Transactor.h`, exposes the standard three static/virtual entry points, and fits in under 30 lines of implementation. This asymmetry is appropriate — creation is inherently complex while cancellation is a targeted deletion with a single ledger object as its subject.
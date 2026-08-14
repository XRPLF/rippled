# Trust-line dust mechanism

Companion document to [`xrpl::DustSplit`](../include/xrpl/ledger/helpers/TokenHelpers.h)
and the [`xrpl::vault_dust::`](../include/xrpl/ledger/helpers/VaultHelpers.h)
Vault adopter. Introduced by `featureLendingProtocolV1_1`.

## Why sfDust

An IOU trust line stores its balance in `sfBalance`, an `STAmount`.
`STAmount` carries ~16 significant digits; `Number` (used throughout
protocol accounting) carries 19. When a caller mints or debits a value
that is finer than what `sfBalance` can represent — typical for
cash-basis Vault operations near a scale-refining decade boundary — the
sub-quantum remainder is dropped by rounding. Over many operations the
loss accumulates and drives receivables and available-cash counters
apart.

`sfDust` is a per-trust-line 19-digit `STNumber` field that holds that
sub-quantum remainder. The trust line's true holding is the
**extended balance** `sfBalance + sfDust`. `sfDust` is intentionally
declared without the `kSmdNeedsAsset` metadata flag: `associateAsset`
must not truncate it to `sfBalance`'s precision, since its whole
purpose is to carry value below that precision.

Pre-amendment `sfDust` is `SoeDefault(0)`. The ledger encoding of an
`ltRIPPLE_STATE` entry is byte-identical to before the amendment on any
line whose `sfDust` is zero, so activation is a strict extension.

## Two-legged trust-line touch

A `A -> B` IOU payment where `I` issues touches at most two trust
lines:

- the sender's line `RIPPLE_STATE{A, I}`;
- the receiver's line `RIPPLE_STATE{B, I}`.

`A` and `B` are the non-issuer parties on their respective lines; `I`
holds no line of its own for its own currency. Each non-issuer party
can independently opt into dust semantics on their OWN line by
attaching a per-leg `DustSplit::LegPolicy` — `sender` for the debit
leg, `receiver` for the credit leg. A leg without a sub-policy runs the
classic pre-dust code path byte-identically.

## Modes

`DustSplit::LegPolicy::Mode::Override`
: Trust-line layer keeps `sfBalance` representable at `overrideScale`
and parks any sub-quantum remainder in `sfDust`. Previously deferred
`sfDust` is automatically promoted when the combined `sfDust +
   credit` clears a whole-quantum boundary — no separate "renormalise"
step. Callers supply `overrideScale` from their own accounting
(e.g. the Vault's posterior scale). Bounded scale drift up to one
decade may linger until the next operation that refines the scale.

`DustSplit::LegPolicy::Mode::Drain` (sender-leg only)
: Folds ALL of `sfDust` on the sender's line into the outgoing
transfer, then zeroes `sfDust`. Used for terminal removals where
the sender is winding down. There is no receiver-side counterpart
because a receiver has no reservoir to drain.

## Reporting sign convention

Out-fields (`balanceDelta`, `dustDelta`) on `LegPolicy` are reported
FROM THAT LEG'S NON-ISSUER PARTY'S PERSPECTIVE — party-positive:

- `sender` leg reports SENDER-POSITIVE deltas. A normal send makes the
  sender's `balanceDelta` negative; a Drain reports `dustDelta ==
-sfDust_before`.
- `receiver` leg reports RECEIVER-POSITIVE deltas. A normal receive
  makes the receiver's `balanceDelta` positive.

This lets a consumer reconcile its own bookkeeping symmetrically:
reads from `sender->balanceDelta` on a withdrawal/clawback line up in
sign with reads from `receiver->balanceDelta` on a deposit, without a
sign flip.

## Contract asserts (debug)

- Drain on the receiver leg is a caller error.
- Attaching a policy to the issuer side of a direct payment (where one
  party IS the issuer) is a caller error — the policy must correspond
  to the non-issuer party's leg.
- Any non-empty `DustSplit` requires `featureLendingProtocolV1_1`
  enabled; a policy under an older rules set falls back to `nullptr`
  and asserts in debug.

## Adding a new consumer

Vault is the sole consumer today (`xrpl::vault_dust::`). Adding a
sibling adopter (AMM, LoanBroker, …) is purely additive:

1. Add an eligibility gate in a new sibling namespace, analogous to
   `xrpl::vault_dust::useVaultDust`.
2. Construct a `DustSplit` in that consumer's transactor helpers,
   using the scale implied by that consumer's own accounting.
3. Reconcile the consumer's bookkeeping using the reported deltas.

The trust-line layer needs no per-consumer awareness. Do NOT extend
`xrpl::vault_dust::` to serve non-Vault consumers.

## Amendment-gate policy

Every code path that reads or writes `sfDust` enforces
`rules().enabled(featureLendingProtocolV1_1)`:

- write side: `directSendNoFeeIOU` (this is the canonical anchor);
- read side: `creditBalanceExact`;
- deletion guard: `removeEmptyHolding`;
- consumer eligibility: `vault_dust::useVaultDust`.

In normal operation the gate is redundant — the transactor-level
eligibility predicates already imply the amendment is on — but a
hypothetical replay/testing path that ever presented a dust-aware
request under a pre-amendment `rules()` must NOT touch `sfDust`: the
field would then be neither `SoeDefault(0)` as expected pre-amendment
nor part of the ledger's canonical encoding. The gate asserts in
debug and falls back to the pre-dust code path in release.

## `ValidVault` invariant sign-handling

`sfDust` is stored in the trust line's low-account convention (same as
`sfBalance`) but the vault-facing invariant needs deltas expressed
from the counterparty's own perspective. The transformation happens in
`src/libxrpl/tx/invariants/VaultInvariant.cpp` in two symmetric
steps — get either step wrong and the invariant either accepts an
underflow it should reject, or spuriously rejects a valid
scale-refining move.

1. `ValidVault::visitEntry` accumulates
   `dustDelta = Number{before} - Number{after}` in low-account
   convention, matching how `balanceDelta.delta` is accumulated. The
   convention that both dustDelta and delta share the same low/high
   orientation is load-bearing — the subsequent sign flip
   (`balanceDelta.dustDelta *= sign`) leaves the pair oriented as
   "after − before".

2. `ValidVault::deltaAssets` looks up a trust line for a given
   `AccountID id`. If `id` is the high account, the low-account
   convention is inverted with `result->delta = -result->delta` and
   `result->dustDelta = -result->dustDelta`. This has to be done in
   lockstep on BOTH fields — flipping only one field would silently
   invert one component of the extended-balance delta and produce
   over- or under-reporting in the cash-flow parity check.

The withdrawal cash-flow parity check in `finalize` for
`ttVAULT_WITHDRAW` compares `extendedPseudoDelta = delta + dustDelta`
against `extendedDestinationDelta = delta + dustDelta`. Both operands
must be constructed from the same sign-corrected `DeltaInfo`, which
is why the sign flip in `deltaAssets` covers both fields together.

If a future contributor adds a new field-typed delta or a new
sign-flipping helper, replicate this two-field lockstep. The end-to-
end regression coverage for this convention lives in
`VaultRoundingTrustlineDust_test::testNonTerminalWithdrawAfterDust`
and `testSenderLegOverrideNonTerminalPromotes`.

## Related tests

- `src/test/app/lending/VaultRoundingTrustlineDust_test.cpp`
  covers the end-to-end dust behaviour, including the extended-balance
  invariant path, sender-leg Override reporting on non-terminal
  withdraw, and end-to-end Drain via defaulted-loan terminal
  withdrawal. Also pins the deletion guard both directions
  (`testVaultDeleteRequiresZeroDust` +
  `testRemoveEmptyHoldingBlockedByDust`), the nullptr-policy dust
  preservation contract (`testNullptrPathPreservesExistingDust`),
  and the golden-byte SoeDefault ledger-encoding compatibility
  (`testSfDustGoldenByteCompat`).
- `src/test/app/lending/VaultRounding_test.cpp::testBoundedDust`
  widens its own test-oracle bound (there is no such bound enforced
  by `ValidVault` or anywhere else at the ledger level) from one
  quantum to ten, to allow for the bounded decade-boundary drift
  documented above.
- `src/test/app/VaultHelpers_test.cpp::testMoveVaultAssetsWithDust`
  pins the multi-destination sender-leg-Override + receivers-dust-
  unaware contract.

# `FreezeInvariant.h` — Enforcing Frozen Trust Line Integrity

## Role in the System

`FreezeInvariant.h` declares the `TransfersNotFrozen` invariant checker, which ensures that no transaction can move token balances across frozen trust lines. It is one of roughly two dozen invariants registered in the `InvariantChecks` tuple (defined in `InvariantCheck.h`) and is executed as a post-transaction safety net after every transaction applied to the ledger.

XRPL's invariant framework — described via the `InvariantChecker_PROTOTYPE` prototype in `InvariantCheck.h` — requires each checker to implement `visitEntry()` for streaming ledger-entry changes and `finalize()` to emit a pass/fail verdict. `TransfersNotFrozen` follows this contract exactly. Invariants are the *last line of defense*: they fire even on failed transactions, because a bug or exploit could mutate ledger state regardless of the transaction's declared result.

## Why a Two-Phase Collect-Then-Validate Design

The core insight driving the class's structure is stated in the implementation:

> "A trust line freeze state alone doesn't determine if a transfer is frozen. The transfer must be examined end-to-end because both sides of the transfer may have different freeze states and freeze impact depends on the transfer direction."

This makes a single-pass approach unworkable. During `visitEntry()`, the invariant can only observe individual trust line states; it cannot yet determine the full picture of what tokens moved where. So `visitEntry()` collects balance changes keyed by issuer and trust-line reference, deferring all policy decisions to `finalize()`.

## Data Structures and Perspective Inversion

The central state is `balanceChanges_`, a `ByIssuer` map keyed on `Issue` (a currency+account pair). For each issuer, an `IssuerChanges` record partitions changes into `senders` (trust lines with a decreasing balance) and `receivers` (trust lines with an increasing balance), each as `BalanceChange` structs pairing the trust line SLE with a sign value.

A non-obvious subtlety in `recordBalanceChanges()` is that every trust line change is recorded *twice* — once for each side's perspective as an issuer. Because XRPL stores trust line balances from the "low" account's perspective, the sign is inverted when recording the entry under the high account's key:

```cpp
recordBalance({currency, after->at(sfHighLimit).getIssuer()}, {after, balanceChangeSign});
recordBalance({currency, after->at(sfLowLimit).getIssuer()}, {after, -balanceChangeSign});
```

This ensures `validateIssuerChanges()` sees consistent, issuer-relative directionality regardless of which side of the trust line a given account sits on.

A second map, `possibleIssuers_`, caches `ltACCOUNT_ROOT` entries observed during `visitEntry()`. Because `findIssuer()` first checks this cache before falling back to `view.read()`, the common case — where the issuer account was already touched by the transaction — avoids an extra ledger lookup.

## When a Transfer Is Actually Frozen

The key invariant rule, implemented in `validateIssuerChanges()`, is:

- If `changes.senders` is empty, tokens are being issued from the issuer to holders. Allowed unconditionally.
- If `changes.receivers` is empty, holders are redeeming back to the issuer. Also allowed unconditionally.
- Only when *both* senders and receivers are present does the invariant check freeze state — this is a holder-to-holder transfer, and freeze rules apply.

`validateFrozenState()` then evaluates three layered freeze conditions:
1. **Global freeze** (`lsfGlobalFreeze` on the issuer account): freezes all trust lines with that issuer universally.
2. **Deep freeze** (`lsfLowDeepFreeze`/`lsfHighDeepFreeze`): blocks all transfers regardless of direction for that specific trust line.
3. **Standard freeze** (`lsfLowFreeze`/`lsfHighFreeze`): only blocks outgoing transfers (`balanceChangeSign < 0`), allowing incoming tokens to still arrive.

One carve-out exists for `AMMClawback` transactions: the `overrideFreeze` privilege (from `InvariantCheckPrivilege.h`) allows such transactions to move funds even across individually frozen or deep-frozen trust lines, but *not* when the issuer has set a global freeze.

## Dynamic Trust Line Creation and Deletion

`calculateBalanceChange()` handles two edge cases. When `before` is null (the trust line was created mid-transaction by a payment crossing offers), the pre-existing balance is treated as zero — the line had no prior balance to protect. When `isDelete` is true, the final balance is also treated as zero, correctly modeling the deletion as the balance "going to zero" and enforcing that even a trust-line deletion can't transfer frozen funds to a third party.

## Amendment-Aware Enforcement with Early Warning

The `enforce` flag in `finalize()` is controlled by the `featureDeepFreeze` amendment:

```cpp
bool const enforce = view.rules().enabled(featureDeepFreeze);
```

Before the amendment activates, the invariant still runs and logs at `fatal` severity and fires `XRPL_ASSERT` in debug builds — but in release builds it does *not* return `false` and does not invalidate the transaction. The in-code comment explicitly documents the design intent: if an exploit allowing frozen-asset movement is discovered, the response is to log, alert operators monitoring fatal messages, and accelerate amendment activation (or introduce a new fix amendment). The enforcement switch is specifically wired to that amendment line to make the transition trivial.

This pattern, also documented in `InvariantCheckPrivilege.h` as `assert(enforce)`, is a deliberate developer-facing tool: any code that violates this invariant in a development or test build without the amendment enabled will hit a debug assert, providing early-warning friction that is intentionally annoying so that bad code gets caught and fixed before mainnet deployment.
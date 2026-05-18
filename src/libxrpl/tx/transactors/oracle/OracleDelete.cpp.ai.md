# `OracleDelete.cpp` — Price Oracle Deletion Transactor

## Purpose and Context

This file implements the `OracleDelete` transaction type, which removes a Price Oracle ledger object (`ltORACLE`) from the XRP Ledger. Price Oracles, specified in XLS-47d, serve as on-chain bridges for external price data used by decentralized applications. `OracleDelete` is the counterpart to `OracleSet`: where `OracleSet` creates or updates oracle entries, this transactor tears them down, cleaning up the ledger state and returning the owner reserve.

The file sits in the `xrpl::` namespace alongside `OracleSet.cpp`, and the `OracleDelete` class inherits from `Transactor` via the standard three-phase transaction framework: `preflight` → `preclaim` → `doApply`.

## Three-Phase Transaction Flow

**`preflight`** is intentionally trivial — it returns `tesSUCCESS` unconditionally. This is a deliberate design choice: a delete operation has no stateless properties to validate (no field ranges, no array size constraints), so all meaningful checks are deferred to the phase that can inspect ledger state.

**`preclaim`** does the real validation against a read-only ledger snapshot. It checks two things: that the submitting account exists (`terNO_ACCOUNT` if not), and that an oracle object exists at `keylet::oracle(account, sfOracleDocumentID)` (`tecNO_ENTRY` if not). A third check compares `sfAccount` from the transaction against `sfOwner` stored in the oracle SLE, but the code comments this as unreachable — because the oracle keylet is derived from the account ID, successfully reading the oracle at that key is sufficient proof of ownership. The ownership comparison is retained as a defensive invariant and is excluded from coverage metrics (`LCOV_EXCL_START/STOP`).

**`doApply`** re-fetches the oracle SLE using `peek()` (which returns a mutable reference into the apply-view sandbox) and delegates immediately to the static `deleteOracle()` helper.

## `deleteOracle` — the Core Deletion Logic

The static `deleteOracle()` method is the architectural heart of this file, and its exposure as a `public static` is significant: other transactors (or future extensions) can reuse oracle teardown without creating an `OracleDelete` transactor instance.

The deletion sequence follows a strict order that reflects ledger integrity requirements:

1. **Directory removal first** — `view.dirRemove(keylet::ownerDir(account), (*sle)[sfOwnerNode], sle->key(), true)` removes the oracle from its account's owner directory. This operation reads `sfOwnerNode` from the oracle SLE, which stores the directory page index populated during creation in `OracleSet::doApply`. The SLE must still be alive for this step; erasing it first would lose the page pointer. Failure here returns `tefBAD_LEDGER`, marking internal ledger corruption.

2. **Owner count adjustment** — the owner reserve is decremented by either `-1` or `-2` depending on whether the oracle held more than 5 price data series entries:
   ```cpp
   auto const count = sle->getFieldArray(sfPriceDataSeries).size() > 5 ? -2 : -1;
   adjustOwnerCount(view, sleOwner, count, j);
   ```
   This mirrors the creation path in `OracleSet::doApply`, where oracles with up to 5 entries consume one owner reserve unit and those with more consume two. The asymmetry in reserve cost reflects the ledger storage cost of large price series arrays. By reading the actual current size from the SLE rather than accepting it from the transaction, the logic is immune to a caller passing a mismatched count.

3. **SLE erasure** — `view.erase(sle)` removes the oracle object from the ledger state. This is the final step; after it, the SLE pointer is invalid and no further reads from it are safe.

## Error Handling and Defensive Patterns

Several error paths are explicitly marked `LCOV_EXCL_LINE` or bracketed in `LCOV_EXCL_START/STOP`, signaling that they guard against conditions the framework guarantees cannot occur in correct operation: a null SLE in `deleteOracle` after `doApply` already confirmed existence; a missing account SLE after `preclaim` confirmed the account exists; and the ownership mismatch discussed above. These are treated as internal fault detection rather than expected application logic.

The `tefBAD_LEDGER` return from a failed `dirRemove` is the one truly unrecoverable error path: it would mean the owner directory and the oracle SLE have diverged in state, indicating a ledger consistency bug rather than a user error.

## Relationship to `OracleSet`

Reading `OracleSet.cpp` in context clarifies the reserve accounting symmetry. On creation, `OracleSet::doApply` calls `dirInsert`, sets `sfOwnerNode` on the new SLE, and increments the owner count by 1 or 2 depending on series size. `OracleDelete::deleteOracle` exactly reverses these three operations. The `> 5` threshold used for the decrement here matches the `> 5` threshold used for the increment there, ensuring the reserve returned equals the reserve originally taken.
# `DIDDelete.cpp` — DID Ledger Entry Removal Transactor

## Role in the System

`DIDDelete.cpp` implements the `DIDDelete` transaction type, which removes a Decentralized Identifier (DID) object from the XRP Ledger. DIDs are W3C-standardized self-sovereign identity constructs, and the XRPL stores them as first-class ledger entries (`ltDID`) associated with an account. When an account no longer wants a DID, this transactor handles the coordinated teardown: unlinking the entry from the owner directory, adjusting the reserve counter, and erasing the SLE itself.

## Class Structure and Inheritance

`DIDDelete` inherits from `Transactor`, the standard base class for all XRPL transaction types. The constructor takes an `ApplyContext&` and simply forwards it, while `ConsequencesFactory` is set to `Normal`, indicating no special fee or sequence-number consequences beyond the baseline. The three public entry points the framework calls are `preflight`, and the virtual `doApply`.

## The No-Op `preflight`

`preflight` unconditionally returns `tesSUCCESS`. This is intentional: a `DIDDelete` transaction carries no payload fields that require field-level validation at preflight time. The only thing being deleted is the DID SLE identified by the submitting account's key. Any error conditions (the DID not existing, the owner directory being corrupted) can only be checked against the current ledger state in `doApply`, not statically in preflight.

This contrasts sharply with `DIDSet::preflight`, which validates that at least one of `sfURI`, `sfDIDDocument`, or `sfData` is present and within length bounds. The asymmetry reflects the inherent difference between a creation/update transaction (where the submitted fields must be well-formed) and a pure deletion (where there is nothing to validate ahead of time).

## The Two-Tier `deleteSLE` Design

The most architecturally significant choice in this file is exposing `deleteSLE` as a pair of `static` overloads rather than baking deletion logic directly into `doApply`.

The first overload accepts `ApplyContext& ctx`, a `Keylet`, and an `AccountID`. It is a thin adapter: it resolves the keylet to a concrete `SLE` via `ctx.view().peek()`, returns `tecNO_ENTRY` if the entry does not exist, then delegates to the second overload.

The second overload accepts `ApplyView& view`, the resolved `std::shared_ptr<SLE>`, the owner's `AccountID`, and a `beast::Journal`. This is where the actual mutation logic lives. By accepting a raw `ApplyView&` instead of the full `ApplyContext&`, this overload can be called from any context that has a writable ledger view — including other transactors that may need to delete a DID as a side-effect of some larger operation, without going through the full transactor machinery.

`doApply` itself is three lines: construct the DID keylet via `keylet::did(account_)`, then call the first `deleteSLE` overload with that keylet and the submitting account's ID.

## The Deletion Sequence

Inside the lower-level `deleteSLE(ApplyView&, ...)`, deletion follows a strict three-step sequence that mirrors the inverse of how `DIDSet`'s `addSLE` helper creates entries:

1. **Owner directory removal**: `view.dirRemove(keylet::ownerDir(owner), (*sle)[sfOwnerNode], sle->key(), true)` unlinks the DID from the account's owner directory page. The `sfOwnerNode` field stored on the SLE itself contains the exact directory page number, making this a direct O(1) lookup rather than a scan. If `dirRemove` fails, the code returns `tefBAD_LEDGER` — a fatal-class error indicating ledger state corruption, not a user error. This path is annotated `LCOV_EXCL` because it should be unreachable under correct operation.

2. **Owner count adjustment**: `adjustOwnerCount(view, sleOwner, -1, j)` decrements the `sfOwnerCount` field on the account root by one. Owner count governs the XRP reserve requirement; failing to decrement it would permanently inflate the account's reserve even after the DID is gone. The account root is fetched fresh via `view.peek(keylet::account(owner))`; a missing account root returns `tecINTERNAL` (also `LCOV_EXCL` — an account that submitted a transaction cannot have a missing root). After adjustment, `view.update(sleOwner)` marks the account root dirty for inclusion in the ledger diff.

3. **SLE erasure**: `view.erase(sle)` removes the DID entry from the ledger state entirely.

This ordering — directory first, then count, then erase — ensures that even if an intermediate step were to fail, the ledger would not be left with a dangling SLE that is unreachable via the owner directory.

## Error Handling Philosophy

The error taxonomy used here reflects the XRPL's distinction between user errors and invariant violations. `tecNO_ENTRY` (returned when the DID SLE doesn't exist) is a `tec`-class error: the transaction is applied to the ledger, the fee is consumed, but the requested operation fails because of a valid but unsatisfied precondition. `tefBAD_LEDGER` and `tecINTERNAL` are defensive guards on conditions that should be structurally impossible given a valid ledger — their `LCOV_EXCL` annotations signal that test coverage is neither expected nor required for these paths.
# `SignerListSet.cpp` — Multi-Signature List Transactor

`SignerListSet.cpp` implements the XRPL transaction type that manages multi-signature signer lists for accounts. It handles three distinct operations — creating, replacing, and destroying a `ltSIGNER_LIST` ledger object — all encoded in a single transaction format whose intent is decoded at processing time. The file lives in the account-management transactor group alongside `AccountSet`, `SetRegularKey`, and similar primitives that govern how accounts authenticate transactions.

## Transaction Semantics and Operation Decoding

The XRPL protocol encodes all three operations (create, replace, destroy) into the same `SignerListSet` transaction format. Rather than an explicit operation flag, the combination of `sfSignerQuorum` and the presence of `sfSignerEntries` determines what to do:

- `quorum != 0` AND `sfSignerEntries` present → `set` (create or replace)
- `quorum == 0` AND `sfSignerEntries` absent → `destroy`
- Any other combination → `unknown` → `temMALFORMED`

This decoding lives in `determineOperation()`, a static method deliberately callable without a live view. It is invoked twice: once in `preflight()` for validation before any ledger state is available, and again in `preCompute()` to cache the parsed results (`quorum_`, `signers_`, `do_`) as instance fields for `doApply()`. The parsed `signers` vector is sorted immediately after deserialization from `SignerEntries::deserialize()` so that subsequent duplicate detection via `std::adjacent_find` is O(N) rather than O(N²).

## Preflight Validation

`preflight()` runs all content validation against the raw transaction with no access to ledger state. It delegates structural parsing to `determineOperation()` and semantic validation to `validateQuorumAndSignerEntries()`. The latter enforces five invariants:

1. Signer count is within `[minMultiSigners, maxMultiSigners]` (currently 1–32).
2. No duplicate accounts — checked via `std::adjacent_find` on the sorted list.
3. All weights are strictly positive; zero-weight signers are rejected with `temBAD_WEIGHT`.
4. No signer references the submitting account itself (`temBAD_SIGNER`), preventing circular delegation.
5. The quorum is achievable: the sum of all signer weights must be ≥ the quorum value; an unreachable quorum yields `temBAD_QUORUM`.

Non-existent signer accounts are intentionally *not* rejected — the protocol explicitly allows "phantom" signers whose accounts haven't been funded yet.

## Reserve Accounting and the `lsfOneOwnerCount` Migration

The most architecturally interesting part of this file is the dual owner-count model that handles the `MultiSignReserve` amendment boundary.

Pre-amendment, each signer list cost `2 + N` owner count units (where N is the number of signers). The formula is encoded in `signerCountBasedOwnerCountDelta()`, which returns a signed integer so it can be passed directly to `adjustOwnerCount()` for both additions and removals. The minimum cost was 3 units (1 signer) and the maximum was 34 units (32 signers).

Post-amendment, new signer lists cost exactly 1 owner count unit regardless of size, indicated by the `lsfOneOwnerCount` flag on the `ltSIGNER_LIST` ledger object. `replaceSignerList()` unconditionally writes `lsfOneOwnerCount` and adds only `1` to the owner count. `removeSignersFromLedger()` must handle both models: it inspects the existing list's `lsfOneOwnerCount` flag to decide whether to decrement by 1 or by the old `2 + N` formula. This allows old-model lists created before the amendment to be correctly cleaned up without needing to migrate them.

## Create and Replace as a Single Path

`replaceSignerList()` treats create and replace identically: it always calls `removeSignersFromLedger()` first to delete any pre-existing list, then inserts the new one. This design simplifies the code at the cost of doing extra work on creates (attempting removal of a non-existent list, which returns `tesSUCCESS` immediately). The removal happens *before* the reserve check, which is deliberate: removing an old list may reduce the owner count and lower the reserve requirement, so checking reserve against the post-removal state is more permissive. The comment in the code notes this behavior is consistent with `TicketCreate`.

## Destruction Safety Gate

`destroySignerList()` enforces a critical safety invariant: it refuses to remove the signer list if the master key is disabled (`lsfDisableMaster`) and no regular key is set. Without this check, a user could permanently brick their account — the signer list is the only remaining authentication method. This guard returns `tecNO_ALTERNATIVE_KEY`, matching the same pattern used by `SetRegularKey` and `AccountSet` when manipulating authentication methods.

## Public Removal Interface for AccountDelete

`removeFromLedger()` is a public static method that exposes the removal logic without requiring a full `SignerListSet` transaction context. This exists specifically for `AccountDelete` — when an account is being deleted from the ledger, any owned objects including signer lists must be cleaned up. Exposing the removal as a static method with explicit `ApplyView`, `AccountID`, and `ServiceRegistry` parameters lets `AccountDelete` invoke it without constructing a `SignerListSet` transactor.

## Amendment Guards

Two protocol amendments affect behavior here. `fixInvalidTxFlags`, checked in `getFlagsMask()`, controls whether invalid transaction flags are masked off or cause rejection — returning `tfUniversalMask` enables strict flag checking. `fixIncludeKeyletFields`, checked in `writeSignersToSLE()`, controls whether `sfOwner` is written into the `ltSIGNER_LIST` object; this was added retroactively to make keylet lookups self-describing.

The `DEFAULT_SIGNER_LIST_ID` constant (fixed at zero) and the surrounding comment acknowledge that the data model was designed from the start to support multiple signer lists per account, but that feature has never been activated. The `sfSignerListID` field is reserved and always written as zero.

## Lifecycle Summary

The transactor follows the standard three-phase lifecycle. `preflight()` runs stateless validation and is marked `static` to reflect its independence from ledger state. `preCompute()` re-parses the transaction (relying on `XRPL_ASSERT` that `preflight` has already guaranteed correctness) and populates the instance's `quorum_`, `signers_`, and `do_` fields. `doApply()` dispatches to either `replaceSignerList()` or `destroySignerList()`. The `ConsequencesFactory{Blocker}` declaration marks the transaction as blocking: within a batch, a `SignerListSet` from an account prevents any further transactions from that account in the same round.
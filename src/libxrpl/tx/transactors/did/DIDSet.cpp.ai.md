# `DIDSet.cpp` — DID Creation and Update Transactor

## Role and Context

`DIDSet.cpp` implements the `DIDSet` transaction type, which creates or updates a Decentralized Identifier (DID) object owned by an XRPL account. The implementation conforms to the W3C DID v1.0 specification, mapping the spec's three core payload concepts onto three blob fields stored in a `ltDID` ledger entry: `sfURI` (the DID document URI), `sfDIDDocument` (the raw document body), and `sfData` (arbitrary associated data). Each field is independently optional but at least one must carry meaningful content — an entirely empty DID is rejected as meaningless. All three are capped at 256 bytes each (`maxDIDURILength`, `maxDIDDocumentLength`, `maxDIDDataLength`, all defined in `Protocol.h`).

`DIDSet` inherits from `Transactor` and plugs into the standard two-phase apply pipeline: a stateless `preflight` that runs before any ledger state is touched, followed by `doApply` that commits changes to the current ledger view.

## Validation in `preflight`

`preflight` enforces two distinct emptiness invariants before any ledger access occurs.

The first check rejects a transaction where none of the three fields is even present — a `DIDSet` carrying no payload is malformed (`temEMPTY_DID`). The second check is subtler: it rejects a transaction where all three fields *are* present but every one of them is an empty byte string. This matters because a client could send `URI=""`, `DIDDocument=""`, `Data=""` as a way to wipe an existing DID clean rather than using `DIDDelete`. The protocol blocks that by treating "all-present but all-empty" as equivalent to "no fields at all."

The length check uses a local `isTooLong` lambda that dereferences the optional field via `ctx.tx[~sField]` — the tilde operator returns `std::optional` — and only evaluates the length if the field is actually present. A single `temMALFORMED` covers any field that exceeds its per-field limit.

## Apply Logic in `doApply`

`doApply` is a clean upsert. It computes the canonical DID keylet for the submitting account via `keylet::did(account_)` — each account can hold at most one DID object, so the keylet is deterministic and needs no disambiguation. It then peeks at the ledger to check existence.

**Update path**: If the DID object already exists, a local `update` lambda is applied to each of the three fields. The lambda's behavior is intentionally asymmetric: if the transaction includes a field and it is non-empty, the SLE field is overwritten; if the transaction includes a field but it is *empty*, the field is actively removed from the SLE via `makeFieldAbsent`. This allows callers to surgically clear one field while leaving others intact. After all three updates, the code re-checks whether all fields are now absent — it is possible to arrive at an empty DID via update (e.g., clearing the last remaining field), and this is rejected with `tecEMPTY_DID`, which is a `tec`-class error that still claims the fee rather than being silently dropped.

**Create path**: If no DID object exists yet, the code constructs a fresh `SLE` of type `ltDID`, sets `sfAccount` to the submitting account, and calls the file-local `addSLE` helper. Only non-empty fields are populated in the new object — the `set` lambda skips absent or empty fields entirely. There is a guard here behind the `fixEmptyDID` amendment: without that fix enabled, the create path would allow an SLE where all three payload fields ended up absent (because the transaction passed `preflight`'s loose check but all provided values were empty strings). The amendment adds the same post-creation empty check that the update path already performs, returning `tecEMPTY_DID` before the object is ever inserted.

## The `addSLE` Helper

`addSLE` is a file-local static function that handles the bookkeeping required when inserting any new owner-tracked ledger object. It follows a standard three-step pattern: reserve check, object insertion, and directory linkage.

The reserve check reads the account's current `sfBalance` and computes the XRP reserve for `ownerCount + 1` objects. If the balance would fall below the new reserve threshold, it returns `tecINSUFFICIENT_RESERVE` before touching anything. This is the correct place for the check — it must happen *before* the object is inserted, not after.

After inserting the SLE into the ledger view, `addSLE` calls `dirInsert` to add the object's key into the account's owner directory, capturing the returned page index into `sfOwnerNode` on the SLE itself. That stored page index is essential for O(1) removal later — `DIDDelete` uses it to call `dirRemove` without scanning the entire directory. Finally, `adjustOwnerCount` increments `sfOwnerCount` on the account root and the updated account SLE is written back.

## Relationship to `DIDDelete`

`DIDDelete` is the symmetric counterpart. Its `deleteSLE` mirrors `addSLE` exactly in reverse: it calls `dirRemove` using the stored `sfOwnerNode`, decrements `sfOwnerCount`, and erases the SLE. The two files together form the complete DID lifecycle on the ledger; neither contains domain logic that belongs in the other.

## Error-Code Taxonomy

The file uses error codes at two different severity levels deliberately. `tem`-class codes from `preflight` cause the transaction to be dropped entirely (no fee claimed, not included in a ledger). `tec`-class codes from `doApply` — `tecEMPTY_DID`, `tecINSUFFICIENT_RESERVE`, `tecDIR_FULL` — still consume the transaction fee and are recorded in the ledger. The re-check for an empty DID inside `doApply` therefore correctly uses `tecEMPTY_DID` rather than `temEMPTY_DID`, because by that point the transaction has already passed preflight and a fee should be charged.
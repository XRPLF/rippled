# `DIDDelete.h` — DID Deletion Transactor

`DIDDelete` is the XRPL transactor responsible for processing `ttDID_DELETE` (type code 50) transactions, which remove a Decentralized Identifier (DID) ledger object previously created via `DIDSet`. It lives in the `did/` subdirectory alongside its write-side counterpart `DIDSet`, and is gated behind the `featureDID` amendment as declared in the auto-generated protocol header.

## Inheritance and Lifecycle

`DIDDelete` inherits `Transactor` and participates in the standard three-phase transactor lifecycle managed by `invokePreflight`:

1. **`preflight`** (static) — validates the raw transaction before any ledger state is touched.
2. **`doApply`** (virtual override) — applies the state mutation once the transaction is accepted.

The class declares `ConsequencesFactory{Normal}`, which tells the fee-queue machinery that this transaction has non-blocking consequences — it does not prevent other transactions from the same account from being processed behind it.

## Why `preflight` Is Trivial

`DIDDelete::preflight` returns `tesSUCCESS` unconditionally. This is intentional: a DID deletion carries no transaction-specific fields beyond the universal base fields (fee, sequence, signing key). All meaningful validation — account existence, sequence number, fee sufficiency, signature correctness, and amendment enablement — is handled by the base `invokePreflight` machinery and does not need to be repeated here. Attempting to do extra work in `preflight` for a no-field transaction would only add noise.

## The `deleteSLE` Overload Pair

The architectural heart of this header is the two static overloads of `deleteSLE`. This separation is a deliberate design for **reusability**.

The first overload — `deleteSLE(ApplyContext& ctx, Keylet sleKeylet, AccountID const owner)` — is the convenience wrapper used by `doApply`. It peeks the DID ledger object by keylet through the apply context and delegates to the second overload. Returning `tecNO_ENTRY` if the object doesn't exist is the idiomatic XRPL response for a fee-claiming failure: the transaction consumed a sequence number and fee, but found nothing to delete.

The second overload — `deleteSLE(ApplyView& view, std::shared_ptr<SLE> sle, AccountID const owner, beast::Journal j)` — operates purely at the `ApplyView` level. It does three things in sequence:
1. Removes the DID SLE from the owner's directory via `dirRemove`, decrementing the account's directory entry.
2. Fetches the owner account's SLE, calls `adjustOwnerCount` with `-1` to decrement the reserve-adjusted owner count, and updates the account object.
3. Erases the DID SLE from the ledger.

By accepting `ApplyView&` and a pre-resolved `std::shared_ptr<SLE>` rather than an `ApplyContext`, this overload is callable from any transactor that has access to a mutable view — most notably `AccountDelete`. When an account is deleted, any owned DID must be cleaned up as a prerequisite; `AccountDelete` can call `DIDDelete::deleteSLE` directly without constructing a `DIDDelete` transactor or synthesizing a transaction context.

## Error Handling

Three failure modes are handled: `tecNO_ENTRY` when the DID object is absent (first overload, fee-claiming), `tefBAD_LEDGER` when `dirRemove` fails (annotated `LCOV_EXCL` because it signals an internal ledger inconsistency that should never occur in practice), and `tecINTERNAL` when the owner account SLE cannot be found (similarly `LCOV_EXCL`). The distinction between `tec` and `tef` codes matters: `tef` failures abort and do not claim a fee, while `tec` failures claim the fee but do not apply the mutation.

## `doApply`

`doApply` is one line: `return deleteSLE(ctx_, keylet::did(account_), account_)`. The DID keylet is deterministically derived from the submitting account's `AccountID`, so no field lookup is required — a DIDDelete transaction is wholly identified by its sender, and each account can hold at most one DID object.
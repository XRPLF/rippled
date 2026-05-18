# `DIDSet.h` — DID Set Transactor Declaration

## Role in the System

`DIDSet.h` declares the `DIDSet` transactor, which handles `DIDSet` transactions on the XRP Ledger. These transactions create or update a Decentralized Identifier (DID) ledger object owned by an account, conforming to the W3C DID v1.0 specification. The file is one of two transactors in the `did/` subdirectory — the other being `DIDDelete` — and both follow the standard two-phase transactor pattern used throughout the XRPL codebase.

## Class Structure and Inheritance

`DIDSet` inherits from `Transactor`, the abstract base class for all transaction types in the ledger engine. This inheritance grants access to the apply pipeline infrastructure: `ApplyContext`, ledger views, account state, fee handling, and signature verification. The constructor simply forwards the `ApplyContext&` reference to the base class, consistent with every other transactor — the context carries everything needed for execution.

The `ConsequencesFactory` is set to `Normal`, meaning the transaction's consequences (for the purpose of local fee escalation and transaction queue management) are computed using the standard path. Transactors that block other transactions from the same account would use `Blocker` instead; `Normal` signals that this transaction neither invalidates nor dominates other pending transactions from the same sender.

## Two-Phase Execution Contract

Like all transactors, `DIDSet` exposes exactly two user-defined entry points:

**`preflight(PreflightContext const& ctx)`** — a static method invoked before the transaction touches ledger state. It runs on the raw transaction fields without access to a mutable view. The implementation in `DIDSet.cpp` enforces three rules: at least one of `sfURI`, `sfDIDDocument`, or `sfData` must be present; all three cannot be simultaneously present but empty (which would create a semantically vacuous DID); and no individual field can exceed its maximum byte length (`maxDIDURILength`, `maxDIDDocumentLength`, `maxDIDDataLength`). Violations return `temEMPTY_DID` or `temMALFORMED`, which are `NotTEC` codes that cause the transaction to be rejected without claiming a fee.

**`doApply()`** — the virtual method that performs the actual ledger mutation, called after fee deduction and sequence number consumption. It distinguishes two code paths: if a DID SLE already exists for the account, it performs a field-level update (an absent field in the transaction is a no-op; an empty field removes the attribute from the existing object; a non-empty field replaces it). If no DID SLE exists, it creates a fresh one and calls the file-local `addSLE()` helper, which checks owner reserve availability, inserts the object into the ledger, adds it to the account's owner directory, and increments the owner count.

## Design Decisions

The `preflight` / `doApply` split is a deliberate separation of concerns central to the XRPL transaction pipeline. `preflight` is cheap, stateless, and can be run on any node that sees the transaction before it enters a ledger. `doApply` is authoritative and runs only once per ledger close under consensus. Placing field-length and presence checks in `preflight` means malformed DID transactions are rejected early, before they consume ledger resources.

The class itself has no data members beyond what `Transactor` provides — all state for the DID operation flows through `ctx_` (the `ApplyContext`), keeping the transactor stateless between construction and the call to `operator()()`. This is consistent across all transactor types and avoids any ambiguity about object lifetime relative to ledger apply batches.

Compared to `DIDDelete`, which adds a static `deleteSLE()` helper used by cleanup paths elsewhere in the system, `DIDSet` exposes no additional static utilities. The upsert logic in `doApply()` is self-contained and not shared, which is appropriate given that creating or updating a DID object has no analogous reuse point in the codebase.
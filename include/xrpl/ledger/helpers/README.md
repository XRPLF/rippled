# Ledger Entry Helpers (`helpers/`)

## Overview

This folder contains helper classes and free functions for working with **Serialized Ledger Entries (SLEs)**. Its centerpiece is `SLEBase.h`, which defines the template class `SLEBase<ViewT>` — a type-safe, context-aware wrapper around the raw `std::shared_ptr<SLE>` used throughout the rest of the codebase.

## The Problem: Untyped SLE Access

Historically, ledger entries are passed around as bare `std::shared_ptr<SLE>` (or `std::shared_ptr<SLE const>`). This has several drawbacks:

1. **No compile-time entry-type safety.** Any code holding an `std::shared_ptr<SLE>` can read or write _any_ field on _any_ ledger entry type. Nothing prevents you from calling `sle->getFieldU32(sfOwnerCount)` on an Offer SLE, even though Offers don't have that field.

2. **No read/write distinction.** A function that only needs to _read_ an entry still receives a mutable `std::shared_ptr<SLE>`, making it easy to accidentally mutate state. Conversely, a function that _must_ write has no way to express that requirement in its signature.

3. **No association with the view.** The SLE and the `ReadView` / `ApplyView` it came from travel as separate arguments, so callers must manually keep them in sync and remember to call `view.update(sle)` after mutations.

## The Solution: `SLEBase.h`

`SLEBase.h` introduces a single template class `SLEBase<ViewT>` that pairs an SLE with its view context and enforces read/write semantics at compile time via `requires` clauses.

**`SLEBase<ReadView>`** (aliased as `ReadOnlySLE`) holds a `std::shared_ptr<SLE const>` and a `ReadView const&`. Write-only members are excluded at compile time.

**`SLEBase<ApplyView>`** (aliased as `WritableSLE`) holds a mutable `std::shared_ptr<SLE>`, an `ApplyView&`, and a `Keylet`. It exposes `insert()`, `update()`, `erase()`, and `newSLE()` to keep the SLE and its view in sync automatically.

A converting constructor allows implicit conversion from `SLEBase<ApplyView>` to `SLEBase<ReadView>`, so functions taking a read-only wrapper can accept a writable one without a cast.

### Template Pattern

Each entry type is a single template class parameterized on the view type:

```
AccountRoot<ReadView>   — read-only:  RAccountRoot
AccountRoot<ApplyView>  — writable:   WAccountRoot
```

Both specializations share all domain read methods. Write methods on `WAccountRoot` are gated with `requires is_writable` so they are unavailable on `RAccountRoot` at compile time. The `R`/`W` prefix aliases (`RAccountRoot`, `WAccountRoot`) are provided for convenience and backward compatibility.

## Files in This Directory

| File                   | Description                                                                                          |
| ---------------------- | ---------------------------------------------------------------------------------------------------- |
| `SLEBase.h`            | Template base class `SLEBase<ViewT>` and `ReadOnlySLE`/`WritableSLE` aliases                         |
| `AccountRootHelpers.h` | `AccountRoot<ViewT>` wrapper (`RAccountRoot`, `WAccountRoot`) and free functions for pseudo-accounts |
| `CredentialHelpers.h`  | Free functions for Credential ledger entries                                                         |
| `DirectoryHelpers.h`   | Free functions for directory traversal (`dirFirst`, `dirNext`, `forEachItem`, etc.)                  |
| `MPTokenHelpers.h`     | Free functions for MPToken and MPTokenIssuance ledger entries                                        |
| `OfferHelpers.h`       | Free function `offerDelete` for removing Offer entries                                               |
| `RippleStateHelpers.h` | Free functions for RippleState (trust line) entries: credit, freeze, issuance, authorization         |
| `TokenHelpers.h`       | Shared token helpers (freeze/auth checks used by both IOU and MPT paths)                             |
| `VaultHelpers.h`       | Free functions for Vault ledger entries                                                              |

## Migration Status

This migration is still in progress. New code should prefer the wrapper style where possible; existing free functions will be migrated incrementally.

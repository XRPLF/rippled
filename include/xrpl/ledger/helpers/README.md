# Ledger Entry Helpers (`entries/`)

## Overview

This folder contains helper classes and free functions for working with **Serialized Ledger Entries (SLEs)**. Its centerpiece is `SLEBase.h`, which defines two base classes — `ReadOnlySLE` and `WritableSLE` — that provide a type-safe, context-aware wrapper around the raw `std::shared_ptr<SLE>` used throughout the rest of the codebase.

## The Problem: Untyped SLE Access

Historically, ledger entries are passed around as bare `std::shared_ptr<SLE>` (or `std::shared_ptr<SLE const>`). This has several drawbacks:

1. **No compile-time entry-type safety.** Any code holding an `std::shared_ptr<SLE>` can read or write _any_ field on _any_ ledger entry type. Nothing prevents you from calling `sle->getFieldU32(sfOwnerCount)` on an Offer SLE, even though Offers don't have that field.

2. **No read/write distinction.** A function that only needs to _read_ an entry still receives a mutable `std::shared_ptr<SLE>`, making it easy to accidentally mutate state. Conversely, a function that _must_ write has no way to express that requirement in its signature.

3. **No association with the view.** The SLE and the `ReadView` / `ApplyView` it came from travel as separate arguments, so callers must manually keep them in sync and remember to call `view.update(sle)` after mutations.

## The Solution: `SLEBase.h`

`SLEBase.h` introduces two base classes that pair an SLE with its view context and enforce read/write semantics at compile time.

**`ReadOnlySLE`** bundles a `std::shared_ptr<SLE const>` with the `ReadView` it was read from. It provides existence checks and const-only access to the underlying entry. Derived classes add domain-specific read-only accessors (e.g. `AccountRoot::isGlobalFrozen()`).

**`WritableSLE`** bundles a mutable `std::shared_ptr<SLE>` with the `ApplyView` used for writes. It provides helpers to insert, update, and erase the entry in the view, keeping the SLE and its view in sync automatically.

### Dual-Inheritance Pattern

Concrete writable wrappers inherit from _both_ the read-only wrapper and `WritableSLE`:

```
WritableAccountRoot
  ├── AccountRoot   (extends ReadOnlySLE)  — read-only domain methods
  └── WritableSLE                          — write capabilities
```

This lets a writable wrapper reuse all read-only domain logic from its parent while gaining mutation and persistence operations from `WritableSLE`.

## Migration Status

This migration is still in progress and is still in the early stages. New code should prefer the wrapper style where possible; existing free functions will be migrated incrementally.

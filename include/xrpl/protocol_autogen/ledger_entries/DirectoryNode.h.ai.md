# `DirectoryNode.h` — Auto-Generated Type-Safe Wrapper for the DirectoryNode Ledger Entry

## Role and Purpose

This file is part of the `xrpl/protocol_autogen/ledger_entries` layer — a set of auto-generated, type-safe C++ wrappers that sit above the raw `SLE` (Serialized Ledger Entry) API. It defines `DirectoryNode` (the read-only wrapper) and `DirectoryNodeBuilder` (the fluent construction interface) for the `ltDIR_NODE` (0x0064) ledger entry type, known in RPC contexts as `"directory"`.

The `DirectoryNode` is one of the most structurally important ledger objects in the XRPL. It implements a paged, doubly-linked-list structure that organizes collections of ledger object keys: an account's owned objects (offers, escrows, trust lines, etc.), an order book's offers at a given quality level, or an NFToken's buy/sell offer listings. Because a single directory can grow beyond what fits in one ledger entry, the design breaks it into pages, each holding a batch of 256-bit entry keys in `sfIndexes` (a `VECTOR256`), chained together via `sfIndexNext` and `sfIndexPrevious` (both optional `uint64` page pointers). `sfRootIndex` is required on every page and always points back to the first page, giving O(1) navigation to the head of the chain regardless of which page is currently being read.

## The Two Classes

`DirectoryNode` extends `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` — the `const` qualifier is the key architectural choice that enforces immutability throughout the read path. Every field accessor is `[[nodiscard]] const` and returns either a direct value (for required fields like `sfIndexes` and `sfRootIndex`) or `protocol_autogen::Optional<T>` for optional ones.

`protocol_autogen::Optional<T>` (defined in `Utils.h`) is a thin type alias: for value types it resolves to `std::optional<T>`, and for reference types it wraps in `std::optional<std::reference_wrapper<...>>`. This handles both cases uniformly without forcing copies of reference fields.

`DirectoryNodeBuilder` inherits from `LedgerEntryBuilderBase<DirectoryNodeBuilder>` via CRTP. The base class initializes an internal `STObject object_` with `sfLedgerEntryType` and `sfFlags`, avoiding the `SOTemplate` initialization intentionally — the comment in `LedgerEntryBuilderBase` explains that calling `object_.set(soTemplate)` would create placeholder `STBase` objects for `soeDEFAULT` fields, causing `applyTemplate()` to throw "may not be explicitly set to default" when the SLE is constructed. All setter methods return `DirectoryNodeBuilder&` by calling `static_cast<Derived&>(*this)` in the base, enabling clean method chaining.

## Semantic Polymorphism in One Entry Type

The most architecturally notable aspect of `DirectoryNode` is that it serves multiple semantically distinct purposes distinguished only by which optional fields are present:

- **Owner directory**: `sfOwner` is set to the account ID; no exchange-rate fields. Used to track all ledger objects belonging to an account.
- **Order book directory**: `sfTakerPaysCurrency`, `sfTakerPaysIssuer` (or `sfTakerPaysMPT` for Multi-Purpose Token orders), `sfTakerGetsCurrency`, `sfTakerGetsIssuer` (or `sfTakerGetsMPT`), and `sfExchangeRate` are set. The `sfExchangeRate` encodes the quality tier and is physically embedded in the last 8 bytes of the ledger entry key via `keylet::quality()`, enabling lexicographic iteration across quality levels using simple key arithmetic.
- **NFToken offer directory**: `sfNFTokenID` identifies the token whose buy or sell offers this directory page lists.
- **Domain directory**: `sfDomainID` groups entries within a domain context.

The generated wrapper exposes a parallel `hasX()` / `getX()` pair for each optional field, giving callers a clean way to determine which semantic role a given `DirectoryNode` is playing without inspecting raw serialized bytes.

## Type Safety and Error Handling

The `DirectoryNode` constructor validates the SLE type immediately on construction, throwing `std::runtime_error` if `sle_->getType() != ltDIR_NODE`. The same check appears in the `DirectoryNodeBuilder(std::shared_ptr<SLE const>)` constructor, which allows an existing SLE to be loaded into a builder for modification. This dual-path construction — either from required primitives or from an existing SLE — handles both the "create new" and "modify existing" workflows while maintaining the type guarantee in both cases.

## Build Cycle

`DirectoryNodeBuilder::build(uint256 const& index)` finalizes construction by moving the accumulated `STObject` into a new `SLE` keyed at `index`, then wrapping that `SLE` in a `DirectoryNode`. The `SLE` constructor calls `applyTemplate()` internally, which reconciles the free `STObject` fields against the `ltDIR_NODE` format template — at this point any missing required fields or invalid field types would produce an error from the ledger formats layer, not from the builder itself.

Because the file header reads `// This file is auto-generated. Do not edit.`, the canonical source of the field list and requiredness annotations lives in the upstream code generator. Adding a new field to `DirectoryNode` requires updating the generator schema, not this file directly.
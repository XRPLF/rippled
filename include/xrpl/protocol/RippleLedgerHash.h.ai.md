# `RippleLedgerHash.h` — Ledger Hash Type Alias

This file occupies exactly nine lines and exists for a single purpose: establishing `LedgerHash` as a named alias for `uint256` within the `xrpl` namespace. Its brevity belies its importance — every layer of the ledger stack that refers to a ledger by its cryptographic identity uses this type.

## What It Does and Why

`LedgerHash` is defined as:

```cpp
using LedgerHash = uint256;
```

where `uint256` is itself `base_uint<256>`, a 256-bit big-endian integer declared in `<xrpl/basics/base_uint.h>`. In practice, a ledger hash is the SHA-512/256 digest computed over a serialized `LedgerHeader` (account-state hash, transaction-set hash, sequence number, close time, drop amounts, and the parent ledger hash). The resulting 32-byte value uniquely identifies a closed ledger in the XRP Ledger network.

## Design Rationale

The alias serves two purposes that bare `uint256` cannot:

**Semantic clarity.** Code that stores or passes a ledger hash communicates its intent through the type name rather than relying on comments or variable names. Interfaces like `CanonicalTXSet(LedgerHash const& saltHash)` become self-documenting at the call site.

**Insulation against future specialization.** `base_uint<Bits, Tag>` accepts an optional `Tag` template parameter precisely to allow distinct types with the same bit-width. By routing all ledger-hash usage through the `LedgerHash` alias, the codebase preserves the option to introduce a tagged variant (e.g., `base_uint<256, struct LedgerHashTag>`) that would be type-incompatible with `uint256` transaction hashes, account IDs, or node IDs — preventing accidental cross-domain substitution — without touching every call site. No such tag is applied today, but the indirection makes the migration zero-friction.

## Relationship to Surrounding Code

`LedgerHeader.h` — the struct that actually carries a ledger's metadata — stores its hashes as bare `uint256` members (`hash`, `txHash`, `accountHash`, `parentHash`) rather than through this alias. That inconsistency is a historical artifact; `calculateLedgerHash()` returns `uint256` for the same reason. Higher-level code, such as `CanonicalTXSet`, `LedgerHistory`, `InboundLedgers`, and the consensus layer (`RCLValidations`, `RCLConsensus`), consistently prefers `LedgerHash` when expressing API contracts, which is the intended usage pattern.

The single `#include <xrpl/basics/base_uint.h>` is intentional minimalism: including the full protocol stack in a header consumed across every ledger-facing subsystem would drive up compile times. `base_uint.h` itself is heavy (it pulls in hashing, endian conversion, and hex utilities), but it is already an unavoidable transitive dependency for virtually all protocol types.
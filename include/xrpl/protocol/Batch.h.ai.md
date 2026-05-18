# `include/xrpl/protocol/Batch.h`

## Role and Purpose

`Batch.h` defines the canonical wire-format serialization of a batch signing payload on the XRP Ledger. Its entire surface area is a single inline function, `serializeBatch()`, but that function is the authoritative definition of what a "batch" means at the cryptographic level: it is the byte sequence that every batch co-signer actually signs and that the validator verifies.

The Batch feature allows multiple independent transactions to be grouped into a single outer transaction with an execution policy (expressed as flags, e.g., `tfAllOrNothing`). Each participant who authorizes that group must produce a signature over a payload that unambiguously encodes both the execution policy and the exact set of inner transactions. `serializeBatch()` produces that payload.

## The Serialization Format

The function appends four elements to a `Serializer`:

1. **`HashPrefix::batch`** (`'B', 'C', 'H'` → `0x42434800`) — a 4-byte type discriminant that places batch hashes in their own hash-space, preventing any collision with signatures over raw transaction data, ledger nodes, or other protocol objects. This is a convention used uniformly across XRPL: every signable payload starts with its own `HashPrefix` constant so that the same binary content never accidentally validates as two different message types.

2. **`flags`** — the `uint32_t` flags field of the outer batch transaction. These flags encode the execution policy (e.g., "apply all or none"). Including flags in the signed data is deliberate: a signer is not just authorizing the set of transactions, they are authorizing those transactions under a specific execution semantics. Stripping flags from the signed payload would let a malicious actor modify the policy after all signers committed.

3. **`txids.size()`** — the inner-transaction count as a `uint32_t`. Serializing the count explicitly means that an adversary cannot extend or truncate the list without invalidating signatures; the signed byte string would have a different count prefix.

4. **Each `uint256` transaction ID** — added via `addBitString`, the raw 32-byte hash of each inner transaction. Signers are committing to the exact set of inner transactions by their hashes, not by any mutable representation.

## How It Is Used

In `STTx.cpp`, both `checkBatchSingleSign()` and `checkBatchMultiSign()` construct a `Serializer`, call `serializeBatch()` to fill it, and then pass the resulting byte slice to the signature-verification helpers (`singleSignHelper` / `multiSignHelper`). This means signature checking and signature creation share a single serialization path — there is no risk of divergence between what the signer encoded and what the validator checks.

The multi-sign path exploits an optimization: `serializeBatch()` is called once to produce `dataStart`, and then each per-signer verification appends only the per-signer account ID suffix via `finishMultiSigningData`. This avoids re-serializing the inner transaction list for every signer.

In the test harness (`src/test/jtx/impl/batch.cpp`), signing helpers for both single-sign and multi-sign batch scenarios call `serializeBatch()` to assemble the message before calling `xrpl::sign()`. This ensures test-generated signatures exercise exactly the same format as production verification.

## Why `inline` in a Header

The function is marked `inline` and lives entirely in a header rather than a `.cpp` file. This is a pragmatic choice: the function is short, has no state, and needs to be callable from multiple translation units — the core library, test helpers, and potentially external tooling — without requiring a separate compilation unit or additional link dependencies.

## Relationship to Dependencies

`HashPrefix.h` provides the `batch` enum value and the broader `HashPrefix` type. Changing `HashPrefix::batch` would be a **protocol-breaking change** — it would render all existing batch signatures invalid — which is why `HashPrefix.h` explicitly notes that these values are part of the protocol and cannot be changed arbitrarily.

`STVector256.h` is included as a convenience for callers. The function itself accepts a plain `std::vector<uint256>`, but in practice the inner transaction IDs come from `STTx::getBatchTransactionIDs()`, which returns data ultimately stored in an `STVector256` field. Including the header here saves callers from having to pull it in themselves.

`Serializer.h` provides the `Serializer` class that manages the growing byte buffer, owns the `add32()` and `addBitString()` methods used here, and exposes `slice()` so the completed payload can be passed directly to signature primitives.

## Invariants Enforced

The serialization order — prefix, then flags, then count, then IDs — is fixed by the code and must not be reordered without a corresponding protocol amendment. Any reordering would produce a different hash, breaking all previously issued signatures. Because the function is the only definition of this format in the codebase, any future change is naturally centralized here rather than scattered across signing and verification sites.
# `include/xrpl/tx/SignerEntries.h` — Multi-Signature Signer Entry Representation

## Purpose and Context

The XRPL multi-signature system lets an account authorize transactions through a quorum of weighted co-signers rather than a single private key. This header defines the minimal data model and deserialization logic for that co-signer roster. It exists as a shared building block used by at least three distinct sites: the `SignerListSet` transactor (which creates and destroys signer lists), the base `Transactor::checkMultiSign()` method (which authenticates incoming multi-sig transactions against the on-ledger list), and the `XChainBridge` transactor (which reads account signers during cross-chain operations). Centralizing this into one header prevents each transactor from re-implementing the same `STArray` iteration and field-extraction logic.

## `SignerEntry` — The Core Record

`SignerEntry` is a plain struct holding three fields: an `AccountID`, a `uint16_t` weight, and an `optional<uint256>` tag that maps to the `sfWalletLocator` field. The optional tag supports destination tagging for phantom accounts (signers that may not yet have on-ledger account roots).

The comparison operators are deliberately defined on `account` alone — `operator<` sorts by `AccountID`, and `operator==` tests equality purely by `AccountID`. This design is not accidental: `SignerListSet::determineOperation()` calls `std::sort()` on the deserialized vector immediately after `deserialize()` returns, and `Transactor::checkMultiSign()` then exploits that sorted order to perform a single O(n) linear merge between the sorted ledger signers and the sorted transaction signers. If the comparison included `weight` or `tag`, the duplicate-detection logic using `std::adjacent_find()` would silently miss two entries with the same account but differing weights, which is a category of malformed transaction that `validateQuorumAndSignerEntries()` explicitly rejects with `temBAD_SIGNER`.

## `SignerEntries` — A Non-Constructible Utility Namespace

The outer `SignerEntries` class has an `explicit`-deleted default constructor: it cannot be instantiated. It acts purely as a named scope for the inner `SignerEntry` type and the static `deserialize()` method. This is a deliberate design over a free function or plain namespace — keeping the type `SignerEntries::SignerEntry` and function `SignerEntries::deserialize()` co-located under the same identifier makes call sites self-documenting about what they are operating on.

The comment in the header makes the data model explicit: a `std::vector<SignerEntries::SignerEntry>` *is* the signer list representation; there is no richer container object wrapping it.

## `deserialize()` — Validation and Extraction

```cpp
static Expected<std::vector<SignerEntry>, NotTEC>
deserialize(STObject const& obj, beast::Journal journal, std::string_view annotation);
```

The function accepts any `STObject` — the same call works against both an `STTx` (a transaction being preflight-checked) and an `SLE` (a live ledger entry being read during apply). The `annotation` parameter is passed as a `string_view` rather than constructed inline, and it feeds directly into journal log messages: callers pass `"transaction"` or `"ledger"` so that trace logs pinpoint whether the malformed data came from a submitted transaction or from ledger state, aiding debugging of both client errors and potential ledger corruption.

The return type `Expected<std::vector<SignerEntry>, NotTEC>` is the XRPL variant of the `std::expected` pattern. `NotTEC` is a strong typedef over the transaction error code type restricted to non-`tec` error codes, meaning errors that should abort the transaction without charging a fee (e.g., `temMALFORMED`). Using `Expected` forces callers to explicitly handle the error path before accessing the value; both `SignerListSet::determineOperation()` and `Transactor::checkMultiSign()` immediately test `if (!signers)` and propagate `signers.error()` before dereferencing.

The implementation (`SignerEntries.cpp`) performs two layers of validation: it first checks that `sfSignerEntries` is present on the object at all, then iterates the `STArray` verifying that each element's field name is `sfSignerEntry`. It extracts three fields per entry — `sfAccount`, `sfSignerWeight`, and optionally `sfWalletLocator` — and appends them to a pre-reserved vector sized to `STTx::maxMultiSigners`. No business-logic validation (quorum reachability, duplicate detection, self-reference checking) happens here; that responsibility belongs to `SignerListSet::validateQuorumAndSignerEntries()`, which receives the already-deserialized vector. The separation keeps deserialization pure and composable.

## Relationship to the Broader Signing Flow

When a `SignerListSet` transaction arrives, `preflight` calls `determineOperation()` → `SignerEntries::deserialize(tx, j, "transaction")`, sorts the result, and passes it to `validateQuorumAndSignerEntries()`. At apply time, `preCompute()` repeats the deserialization to re-populate `signers_`. The read-then-re-deserialize pattern is intentional: preflight and apply run in different contexts and the apply phase needs the deserialized list available without storing it across the phase boundary.

When a multi-signed transaction of *any* type is submitted, `Transactor::checkMultiSign()` reads the on-ledger `ltSIGNER_LIST` object and calls `SignerEntries::deserialize(*sleAccountSigners, j, "ledger")`. It then walks both the ledger signer vector and the transaction's `sfSigners` array in parallel — O(n) because both are sorted by `AccountID` — accumulating weight from matched signers and returning `tefBAD_SIGNATURE` on any mismatch.

This header is thus the pivot between the protocol representation of signer lists (as `STArray` inside `STObject`) and the in-memory representation consumed by transaction processing logic throughout the codebase.
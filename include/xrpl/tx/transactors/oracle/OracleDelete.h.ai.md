# `OracleDelete.h` — Oracle Deletion Transactor

## Role in the System

`OracleDelete` is the XRPL transactor responsible for removing Price Oracle objects from the ledger. It belongs to the Price Oracle feature defined by the XLS-47d specification, which allows accounts to publish off-chain price data (such as asset prices) on-chain so decentralized applications can consume them. The `OracleDelete` class pairs with `OracleSet` — together they cover the full lifecycle of an oracle object.

Like all transactors, `OracleDelete` inherits from the `Transactor` base class and plugs into the three-phase transaction-processing pipeline: `preflight` (stateless sanity checks), `preclaim` (read-only ledger validation), and `doApply` (ledger mutation). The `ConsequencesFactory{Normal}` constant tells the framework to use standard fee and consequence computation for this transaction type.

## The Three-Phase Pipeline

**`preflight`** unconditionally returns `tesSUCCESS`. There is nothing to validate about a delete request without consulting ledger state — the transaction carries only an `sfOracleDocumentID` identifying which oracle to remove. All meaningful checks happen in `preclaim`.

**`preclaim`** performs two read-only ledger lookups. First, it confirms the submitting account exists (the comment marks this path as unreachable under normal conditions since account existence is enforced earlier). Second, it derives the oracle's ledger key from `keylet::oracle(account, documentId)` and verifies the object exists; if not, it returns `tecNO_ENTRY` with a debug log. It also confirms the `sfOwner` field on the oracle SLE matches the submitting account — though again, this case is marked `LCOV_EXCL` because the key derivation itself embeds the account, making ownership mismatches structurally impossible through normal transaction flow.

**`doApply`** is thin: it re-reads the oracle SLE via `ctx_.view().peek()` and delegates immediately to the static `deleteOracle` helper.

## The `deleteOracle` Static Method

The most architecturally notable feature of this header is the public static `deleteOracle(ApplyView&, SLE, AccountID, Journal)` method. Unlike `OracleSet`, which has no equivalent helper, `OracleDelete` exposes its core mutation logic as a free-standing static function. This matches the pattern of `Transactor::ticketDelete` in the base class — it exists so that other transactors (principally account deletion, which must clean up all objects owned by the account) can remove oracles without constructing a full `OracleDelete` transactor instance.

The implementation of `deleteOracle` encodes a subtle ledger-reserve rule: oracle objects that contain more than five `sfPriceDataSeries` entries are charged two owner reserve slots instead of one when created. The deletion therefore adjusts the owner count by `-2` for large oracles and `-1` for smaller ones via `adjustOwnerCount`. This mirrors the creation logic in `OracleSet` and must remain in sync with it; the threshold of five entries is a protocol-level constant baked into XLS-47d.

After adjusting the reserve count, `deleteOracle` removes the oracle from the account's owner directory with `view.dirRemove()` and then erases the SLE with `view.erase()`. A failure from `dirRemove` returns `tefBAD_LEDGER`, which signals an internal ledger consistency error — this path is also excluded from coverage because it indicates corruption rather than a user-facing error condition.

## Relationship to `OracleSet`

The two oracle transactors are mirror images in structure but differ in complexity. `OracleSet` handles both creation and update (idempotent upsert semantics), carries significant `preflight` validation (checking data types, sizes, and price data constraints), and has no reusable static helper. `OracleDelete` is intentionally minimal: deletion needs no field validation, and the static helper exists purely to support the account-deletion cleanup path that the broader transaction system requires.
# `AcceptedLedgerTx.cpp` — Transaction Envelope for Closed-Ledger Propagation

## Role in the System

`AcceptedLedgerTx` represents a single transaction that has been accepted into a closed (finalized) ledger. Its purpose is not transaction execution — that happens in the consensus and apply pipeline — but rather **post-acceptance packaging**: assembling all the information downstream consumers need to act on a confirmed transaction.

Two major consumers drive the design. First, `NetworkOPsImp::pubValidatedTransaction()` and `pubAccountTransaction()` use the pre-built `mJson` payload to push events over WebSocket subscriptions without reconstructing JSON on every subscriber delivery. Second, the relational database backend (`Node.cpp`) calls `getEscMeta()` to obtain SQL-safe binary metadata for persistence. Both access patterns favor construction-time serialization over lazy computation, which is exactly what the constructor does.

## Constructor Logic

The constructor takes three `shared_ptr` inputs — the closed ledger view, the serialized transaction (`STTx`), and the raw metadata `STObject` — and fully materializes the object in one pass.

The first thing it does is assert `!ledger->open()`. This is a hard invariant: `AcceptedLedgerTx` is only meaningful for finalized ledgers. An open ledger has no authoritative transaction ordering or result codes yet, so constructing this wrapper for an open ledger would produce incorrect metadata. The `XRPL_ASSERT` (rather than a thrown exception) reflects that callers in this path are trusted internal code where a violation indicates a programming error rather than bad input.

`TxMeta` is constructed inline from the transaction ID, ledger sequence, and dereferenced metadata `STObject`. This is the parsed, structured representation of metadata (affected nodes, result code, delivery amounts). In parallel, the raw bytes of `met` are also captured via a `Serializer` pass into `mRawMeta`. Keeping both forms avoids re-serializing later: the `STObject` form serves `getJson()`, while the binary blob serves `getEscMeta()`.

The JSON payload assembled in `mJson` is intentionally comprehensive. It embeds the transaction (`jss::transaction`), its parsed metadata (`jss::meta`), the hex-encoded raw metadata (`jss::raw_meta`), the human-readable result string (`jss::result`), and the set of affected accounts (`jss::affected`) in base58 form. This is the exact envelope that WebSocket subscription clients receive.

## The OfferCreate Owner Funds Special Case

The most interesting logic is the `ttOFFER_CREATE` branch. For offer creation transactions where the offer is not self-funded (i.e., the transaction's account is not the issuer of the asset being offered), the constructor queries `accountFunds()` against the closed ledger to compute the account's actual spendable balance of the asset at the time of acceptance.

This `owner_funds` field is injected directly into `mJson[jss::transaction]` and exists specifically to help clients and order book subscribers assess whether an offer is fully funded at the moment of its creation. It is not part of the ledger state itself — it is a read-time annotation added to the event. The `fhIGNORE_FREEZE` and `ahIGNORE_AUTH` flags passed to `accountFunds()` indicate that this balance query intentionally bypasses trust-line freeze checks and authorization requirements, reporting the raw economic balance rather than the effective spendable amount under compliance restrictions. The `beast::Journal::getNullSink()` suppresses diagnostic output, consistent with this being a non-critical annotation rather than a protocol-required computation.

The self-funded exclusion (`account != amount.getIssuer()`) avoids a redundant query: when an account creates an offer to sell its own issued currency, its "balance" of that asset is unbounded (it can issue freely), so the `owner_funds` annotation would be meaningless.

## `getEscMeta()`

`getEscMeta()` returns `mRawMeta` formatted as a SQL blob literal via `sqlBlobLiteral()`. It asserts that `mRawMeta` is non-empty before doing so — the metadata binary is populated unconditionally in the constructor, so a non-empty check failing would indicate object construction was somehow bypassed. The result is used verbatim in SQL `INSERT`/`REPLACE` statements for the transaction database (see `Node.cpp`).

## Design Tradeoffs

The choice to serialize everything at construction time (JSON, raw bytes, affected accounts) trades memory for CPU efficiency and simplicity: the object is immutable after construction, it can be shared freely across threads without locks, and every accessor is a trivial `const` reference return. The `CountedObject<AcceptedLedgerTx>` base class provides cheap live-instance telemetry without affecting behavior.

The `boost::container::flat_set<AccountID>` for `mAffected` is a deliberate space-time tradeoff: a sorted contiguous array is more cache-friendly than `std::set` for the small sets typical of XRPL transactions, and it supports efficient iteration for the subscription notification fan-out in `pubAccountTransaction()`.
# `AcceptedLedgerTx` — Closed-Ledger Transaction Wrapper

## Purpose and Role

`AcceptedLedgerTx` represents a single transaction that has been accepted into a closed (non-open) ledger, enriched with all the derived information the server needs to notify clients and persist state to storage. Its central job is to act as an immutable, eagerly-computed snapshot: raw transaction bytes, parsed metadata, affected accounts, a cached JSON representation, and a SQL-ready blob are all computed once at construction and then served read-only to multiple consumers.

The class sits at the intersection of two major subsystems: the subscription/notification layer (`NetworkOPsImp::pubValidatedTransaction`, `pubAccountTransaction`) and the relational database backend (`Node.cpp` SQL insert path). Both consumers need different projections of the same transaction — one needs JSON with affected account lists for WebSocket fan-out, the other needs binary-escaped metadata for SQL INSERTs. Rather than recomputing these projections on every delivery, `AcceptedLedgerTx` pre-builds all of them in its constructor.

## Construction and Data Flow

The constructor takes three inputs: a shared pointer to the closed `ReadView` (the ledger snapshot), the `STTx` transaction object, and a raw `STObject` representing the transaction's metadata blob as stored in the ledger.

From these, four derived data structures are built:

1. **`TxMeta mMeta`** — the `STObject` metadata is parsed into a structured `TxMeta` via `TxMeta(txID, ledger->seq(), *met)`, giving typed access to affected nodes, result codes, and the transaction's position index within the ledger.

2. **`mAffected`** — `mMeta.getAffectedAccounts()` returns a `boost::container::flat_set<AccountID>` of every account touched by this transaction. Using a `flat_set` (a sorted contiguous array rather than a tree) is a deliberate choice: the set is built once and only read afterwards, so cache-friendly linear traversal during fan-out outweighs the cost of sorted insertion at construction time. This set is what `InfoSub` uses to route notifications to account-specific subscribers.

3. **`mRawMeta`** — the `STObject` is separately serialized back to a raw byte blob via `Serializer::add()`. This binary form coexists with the parsed `TxMeta` because each serves a different consumer: the parsed form enables field-level access, while the raw bytes are needed verbatim for database persistence and are included in the JSON as `raw_meta` (hex-encoded via `strHex`).

4. **`mJson`** — the full JSON snapshot is assembled once, containing the transaction (`jss::transaction`), structured metadata (`jss::meta`), the hex-encoded raw metadata (`jss::raw_meta`), the human-readable result string (`jss::result`), and an array of affected account Base58 addresses (`jss::affected`). Computing JSON is relatively expensive; caching it here ensures the cost is paid once regardless of how many WebSocket subscribers receive this transaction.

The constructor enforces a hard invariant via `XRPL_ASSERT(!ledger->open())` — an `AcceptedLedgerTx` can only be created from a closed ledger. This assertion documents a domain rule (you cannot "accept" a transaction into an open ledger) and guards against programming errors where the wrong ledger state is passed in.

## The `owner_funds` Special Case

For `ttOFFER_CREATE` transactions where the offer is not self-funded (i.e., the issuer of the `TakerGets` amount is not the offer creator), the constructor queries `accountFunds()` against the ledger snapshot and injects an `owner_funds` field into the JSON. This is a special accommodation for order-book subscribers: clients watching an order book need the offer creator's actual spendable balance to determine whether the offer is executable, and this balance is only available while the closed ledger's state is still in hand. Injecting it here avoids a later round-trip through the ledger when delivering the notification.

## `getEscMeta()` and the Database Path

`getEscMeta()` returns `mRawMeta` formatted as an escaped SQL blob literal via `sqlBlobLiteral()`. In `Node.cpp`, this is used directly in an `STTx::getMetaSQL()` insert statement — the output is pasted verbatim into a SQL string. The `XRPL_ASSERT(!mRawMeta.empty())` guard at the top of `getEscMeta()` documents that empty metadata is structurally impossible for an accepted transaction; since `mRawMeta` is populated from the `STObject` passed at construction (which must exist for a valid ledger entry), an empty blob would indicate severe ledger corruption upstream.

## Relationship to `AcceptedLedger`

`AcceptedLedger` owns a `std::vector<std::unique_ptr<AcceptedLedgerTx>>` — one entry per transaction in the accepted ledger, in order. `AcceptedLedger`'s constructor iterates the ledger's transaction map and builds an `AcceptedLedgerTx` for each entry. The `AcceptedLedger` is itself held in a cache and handed to `NetworkOPsImp` when it processes a newly validated ledger, which then iterates the vector and calls both `pubValidatedTransaction` and `pubAccountTransaction` for each `AcceptedLedgerTx`.

## Instance Counting via `CountedObject`

`AcceptedLedgerTx` inherits `CountedObject<AcceptedLedgerTx>`, which uses a lock-free linked list of static counters to track live instance counts by type name. This is a server-wide diagnostic facility: operators can query how many `AcceptedLedgerTx` objects are alive at any moment, which is useful for detecting accumulation under load or slow subscriber drain that is holding ledger snapshots open longer than expected.
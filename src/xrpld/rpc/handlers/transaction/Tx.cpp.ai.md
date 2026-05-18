# `Tx.cpp` — The `tx` RPC Handler

## Role in the System

`Tx.cpp` implements `doTxJson`, the server-side handler for the XRPL `tx` RPC method. This is one of the most fundamental read APIs in the ledger — a client sends a transaction hash or Concise Transaction ID (CTID) and gets back the full transaction with its metadata and validation status. The file is deliberately self-contained: it houses the argument structures, the lookup logic, and the response-formatting code in roughly 320 lines, making the full lifecycle of the call easy to trace in one place.

## Three-Phase Architecture

The implementation separates concerns into three tightly scoped layers:

**`doTxJson`** owns input parsing. It validates that exactly one of `transaction` (a hash) or `ctid` is supplied — both at once is rejected as `rpcINVALID_PARAMS` because the intent would be ambiguous. It decodes CTID strings via `RPC::decodeCTID`, performs the critical network ID check (a CTID encodes which network it was issued on, so submitting it to the wrong node produces a human-readable error rather than a silent hash lookup on a different chain), and builds a `TxArgs` value object. The optional `min_ledger`/`max_ledger` pair is extracted here too; the `asUInt()` calls sit inside a try/catch specifically because `Json::Value::asUInt` throws on type mismatch, and the design is to convert that exception into `rpcINVALID_LGR_RANGE` rather than let it bubble.

**`doTxHelp`** owns lookup and validation. It checks the ledger range constraints (the range must be non-inverted and capped at 1000 ledgers — `MAX_RANGE` is a `constexpr` scoped to the function) before dispatching to `TransactionMaster::fetch`. If a CTID was supplied, the ledger sequence and transaction index are resolved to a hash via `LedgerMaster::txnIdFromIndex` before calling `fetch`. There are two `fetch` overloads: one that accepts a `ClosedInterval<uint32_t>` for range-bounded searches, and one that searches the full local history. Both return `std::variant<TxPair, TxSearched>` — if the variant holds a `TxSearched` sentinel, the transaction was not found and that sentinel is forwarded into `TxResult` for the response layer to use.

**`populateJsonResponse`** owns serialization. It must not touch the database or the ledger state — everything it needs is already in `TxResult`.

## The `TxResult` Structure

`TxResult` carries the raw search output across the phase boundary. Its `meta` field is typed as `std::variant<std::shared_ptr<TxMeta>, Blob>` — when binary mode is requested, `doTxHelp` calls `meta->getAsObject().getSerializer().getData()` up front and stores the raw bytes, avoiding any re-serialization in the response phase. The `validated` bool, `closeTime`, `ledgerHash`, and `ctid` are only populated when the transaction was found in a closed ledger and all the prerequisite data is available.

## Validation Status and `isValidated`

The file-local `isValidated` function is a three-guard check: the ledger must exist in the local store, its sequence must not exceed the current validated ledger's sequence, and its hash must match what the local store has for that sequence. This matters because a node can have a closed but not yet validated ledger in its store; returning `"validated": true` for such a transaction would be incorrect. The hash match is the final guard against rare cases where a ledger was reorganised.

## CTID Round-Trip

CTID support follows a decode-then-re-encode pattern. On input, `decodeCTID` parses a 16-hex-character string into `(ledgerSeq, txnIndex, networkID)` — the two-nibble `C` prefix in the high bits is a magic tag that distinguishes CTIDs from raw hashes. On output, `doTxHelp` attempts to re-encode a CTID from the found transaction's metadata fields. This re-encoding can fail if any component exceeds its bit budget (`txnIdx > 0xFFFF`, `netID >= 0xFFFF`, `lgrSeq >= 0x0FFF'FFFF`), in which case the `ctid` field is simply absent from the response rather than returning a truncated or invalid value. This is a deliberate bounds check spelled out inline rather than delegated to `encodeCTID`'s own guard.

## API Version Branching

`populateJsonResponse` has a clear branch on `context.apiVersion > 1`. In API v2+, the response structure is reorganised: the transaction goes under `tx_json` (or `tx_blob` for binary mode), `ledger_hash` and `hash` become top-level fields, and `close_time_iso` replaces the older date format. In API v1, the flat legacy layout from `Transaction::getJson` is used directly. The `JsonOptions::disable_API_prior_V2` flag signals to the serializer that deprecated legacy fields should be suppressed. This branching avoids a separate handler file for each API version while keeping the shape of each response correct.

## Synthetic Metadata Insertions

After the core metadata is serialised, three augmentation calls run on the `meta` JSON object: `insertDeliveredAmount` fills in `delivered_amount` for payment and check-cash transactions, `RPC::insertNFTSyntheticInJson` adds NFT-specific synthetic fields, and `RPC::insertMPTokenIssuanceID` injects the `mpt_issuance_id` for MPTokenIssuanceCreate transactions. These are post-processing concerns that do not belong in the generic serializer — placing them here keeps the protocol-specific enrichment co-located with the RPC handler that owns the response contract.

## The `searched_all` Field and Partial Histories

When a transaction is not found and a ledger range was specified, the response includes `"searched_all": true/false` derived from the `TxSearched` enum. `TxSearched::All` means every ledger in the requested range was present in the local database and the transaction is definitively absent. `TxSearched::Some` means the local store was incomplete — the client should not conclude the transaction never existed. `TxSearched::Unknown` (the default when no range was given or a deserialization error occurred) suppresses the field entirely, preserving backward compatibility with the pre-range-query response shape. This design lets clients distinguish genuine non-existence from a partial-history node without requiring a separate API call.
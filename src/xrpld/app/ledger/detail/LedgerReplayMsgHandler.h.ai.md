# `LedgerReplayMsgHandler`

`LedgerReplayMsgHandler` is the message-processing bridge between individual peer connections and the central `LedgerReplayer` subsystem. It handles exactly four protocol message types that drive the ledger replay feature: `TMProofPathRequest`, `TMProofPathResponse`, `TMReplayDeltaRequest`, and `TMReplayDeltaResponse`. One instance lives as a direct member of `PeerImp`, meaning every active peer connection owns its own handler that shares a reference to the single application-wide `LedgerReplayer`.

## Role in the Replay Pipeline

Ledger replay allows a node to reconstruct historical ledgers by fetching the minimum necessary data from peers: a cryptographic proof that a ledger's skip list entry is authentic (via a SHAMap proof path), and the set of transactions that were applied to produce the target ledger (the "delta"). `LedgerReplayMsgHandler` straddles two roles simultaneously — it acts as a server when this node already possesses a ledger a peer wants, and as a client-side verifier when this node receives data it requested from a peer.

## Request Handling (Server Side)

`processProofPathRequest()` and `processReplayDeltaRequest()` are called when a remote peer wants data from the local node. Both follow the same defensive pattern: validate the inbound protobuf fields first (checking field presence, `uint256`-sized byte strings, valid enum values), set `reBAD_REQUEST` on the response proto and return immediately if anything is off. If the requested ledger cannot be found via `LedgerMaster::getLedgerByHash()`, the response carries `reNO_LEDGER`. A further `reNO_NODE` error is possible if the requested SHAMap key doesn't exist in the ledger's state map.

For proof path requests, the handler dispatches on the `TMLedgerMapType` enum to call `getProofPath()` on either the account state map or the transaction map, then serializes the ledger header and the raw proof path bytes into the response. For replay delta requests, it iterates over all transaction leaf nodes via `txMap().visitLeaves()` and packs each raw item into the response's `transaction` repeated field.

The return type for both request handlers is the response protobuf by value. The caller in `PeerImp::onMessage` then inspects `has_error()` and decides whether to send the response or charge the requesting peer a resource fee. This design keeps `LedgerReplayMsgHandler` free of networking concerns — it only validates and serializes.

Notably, `PeerImp` offloads request handling to the job queue (`jtREPLAY_REQ`) rather than processing inline on the network strand. This prevents a slow ledger lookup from blocking message dispatch for the peer. Response handling, by contrast, is processed directly in the network message path.

## Response Handling (Client Side)

`processProofPathResponse()` and `processReplayDeltaResponse()` are called when a previously-requested response arrives. Both return `false` on any validation failure, which `PeerImp` translates into a `feeInvalidData` resource charge against the peer — a lightweight form of abuse prevention.

Every response goes through cryptographic verification before touching the `LedgerReplayer`. For proof path responses, the handler deserializes the ledger header, recomputes its hash via `calculateLedgerHash()`, and compares against the hash the peer echoed back. It then calls `SHAMap::verifyProofPath()` using `info.accountHash` as the root to confirm the Merkle path is internally consistent. At the time of writing, the response handler explicitly limits itself to `lmACCOUNT_STATE` and to the skip list key (`keylet::skip()`), with a comment indicating transaction-map proof support is deferred. Only after verification does it deserialize the leaf node and call `replayer_.gotSkipList()`.

For replay delta responses, the same header hash check applies. The handler then reconstructs a local `SHAMap` of type `TRANSACTION` by deserializing each transaction-metadata item from the response. Each encoded item is a VL-prefixed pair: the raw transaction bytes followed by the metadata. The handler extracts `sfTransactionIndex` from the metadata to insert transactions into an `orderedTxns` map by their execution order, and inserts the paired SHAMap item for hash verification. Only when the rebuilt `txMap.getHash()` matches `info.txHash` from the deserialized header — proving the peer sent a complete and unmodified transaction set — does the handler invoke `replayer_.gotReplayDelta()`. All deserialization is wrapped in a broad `std::exception` catch that returns `false` on any malformed input.

## Design Choices

The symmetric split between request and response handlers within one class is intentional: both sides of the same protocol message exchange belong together, avoiding scatter across unrelated files while keeping `PeerImp` free of SHAMap and replay internals. Returning the response by value (rather than passing a mutable reference or a callback) makes the request path straightforward to test and reason about, with the networking concern cleanly owned by the caller. The conservative cryptographic checking on every response — even though the data ultimately originates from a peer that the node chose to contact — reflects a defense-in-depth posture consistent throughout the XRPL codebase.
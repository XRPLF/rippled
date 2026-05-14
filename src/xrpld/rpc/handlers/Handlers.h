/** @file
 *  Central manifest of every old-style RPC handler function in the xrpld server.
 *
 *  Declares 67 free functions — one per public API method — all sharing the
 *  signature `Json::Value doXxx(RPC::JsonContext&)`. Including this header in
 *  a translation unit makes every handler symbol available; `Handler.cpp` uses
 *  it to populate the static dispatch table that routes incoming RPC requests.
 *
 *  **Old-style vs new-style handlers.** The functions here are "old-style":
 *  plain free functions that accept a `JsonContext&` and return a `Json::Value`.
 *  `Handler.cpp` wraps each one with `byRef()`, which converts the by-value
 *  return into the `Handler::Method<Json::Value>` type the dispatch table
 *  expects. The only "new-style" handlers — `LedgerHandler` and `VersionHandler`
 *  — use a class with static metadata and are registered separately via
 *  `addHandler<T>()`.
 *
 *  **Adding a handler.** Declare it here, implement it in its own translation
 *  unit under `src/xrpld/rpc/handlers/`, and add one entry to the
 *  `handlerArray` in `Handler.cpp`. Omitting any of these three steps renders
 *  the handler unreachable or produces a link error.
 *
 *  @note This file intentionally includes `handlers/ledger/Ledger.h` rather
 *      than `Context.h` directly, so every translation unit that includes
 *      `Handlers.h` also receives the full `JsonContext` type definition
 *      required to write a valid handler body or call site.
 */
#pragma once

#include <xrpld/rpc/handlers/ledger/Ledger.h>

namespace xrpl {

// --- Account queries ---

/** Return the currencies an account can send or receive.
 *
 *  Inspects the account's trust lines and returns two lists: assets the
 *  account can send and assets it can receive.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `send_currencies` and `receive_currencies` arrays.
 */
json::Value
doAccountCurrencies(RPC::JsonContext&);

/** Return information about a single account on the ledger.
 *
 *  Retrieves account root fields (sequence, balance, flags, signers) and
 *  optionally queue data and signer lists. API v2 promotes `account_data`
 *  fields to the top level.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object describing the account, or an error if not found.
 */
json::Value
doAccountInfo(RPC::JsonContext&);

/** Return trust lines associated with an account.
 *
 *  Supports paginated iteration via a marker. Results are filtered by the
 *  optional `peer`, `ledger_index`, and `limit` request fields.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `lines` array and an optional `marker`.
 */
json::Value
doAccountLines(RPC::JsonContext&);

/** Return payment channels where the given account is the channel source.
 *
 *  Walks the account's owner directory and reports all open channel objects.
 *  Supports pagination via a marker.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `channels` array and an optional `marker`.
 */
json::Value
doAccountChannels(RPC::JsonContext&);

/** Return the NFTs owned by an account.
 *
 *  Iterates the account's NFToken pages and returns token metadata.
 *  Supports pagination via a marker.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with an `account_nfts` array and an optional `marker`.
 */
json::Value
doAccountNFTs(RPC::JsonContext&);

/** Return the ledger objects owned by an account.
 *
 *  Walks the owner directory and returns SLE data for each object, with
 *  optional filtering by type. Supports pagination via a marker.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with an `account_objects` array and an optional `marker`.
 *  @note Resuming pagination with a marker that belongs to a different account
 *      is a security concern; callers should validate markers with
 *      `RPC::isRelatedToAccount` before use.
 */
json::Value
doAccountObjects(RPC::JsonContext&);

/** Return the offers placed by an account.
 *
 *  Lists all outstanding DEX Offer objects in the account's owner directory.
 *  Supports pagination via a marker.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with an `offers` array and an optional `marker`.
 */
json::Value
doAccountOffers(RPC::JsonContext&);

/** Return transactions that affected a given account.
 *
 *  Queries the transaction database within optional ledger index bounds.
 *  Supports pagination. API v2 enforces strict marker and boolean field typing
 *  and rejects mixing `ledger_index_min`/`max` with `ledger_index`/`ledger_hash`.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `transactions` array and an optional `marker`.
 */
json::Value
doAccountTx(RPC::JsonContext&);

// --- DEX and orderbook ---

/** Return information about an Automated Market Maker (AMM) instance.
 *
 *  Resolves the AMM either by `amm_account` (direct lookup) or by
 *  `asset`+`asset2` pair (derives the AMM account via `keylet::amm`).
 *  Both paths resolve to the same AMM SLE.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with AMM pool data, LP token supply, and trading fees.
 */
json::Value
doAMMInfo(RPC::JsonContext&);

/** Return the offers currently listed in an order book.
 *
 *  Iterates a DEX book directory for the given currency pair and returns
 *  up to `limit` offers. Applies an inline load-shedder: at warning or drop
 *  resource tiers the effective limit is halved before iteration.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with an `offers` array.
 */
json::Value
doBookOffers(RPC::JsonContext&);

/** Return OHLCV price data for all currency pairs that traded in a ledger.
 *
 *  Reads the transaction metadata of a closed ledger and produces one
 *  summary record per (base, quote) pair that had at least one fill.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `changes` array of per-pair OHLCV records.
 */
json::Value
doBookChanges(RPC::JsonContext&);

/** Return the weighted-average aggregate price for an oracle data feed.
 *
 *  Combines oracle price data from multiple Oracle ledger objects and returns
 *  a statistical summary (mean, trimmed mean, median) of the feed.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with aggregate price statistics.
 */
json::Value
doGetAggregatePrice(RPC::JsonContext&);

// --- Payment channels ---

/** Sign a payment channel claim for off-ledger use.
 *
 *  Creates a signature over a (channelID, amount) pair using the caller's
 *  key. The signature can be used by the channel destination to claim funds.
 *  Uses `HashPrefix::paymentChannelClaim` ('CLM') as the domain separator.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object containing the `signature` field.
 *  @note This handler has no `ADMIN` role requirement, but signing support
 *      must be enabled via config (`[signing_support]`); otherwise the node
 *      returns `rpcNOT_SUPPORTED`.
 */
json::Value
doChannelAuthorize(RPC::JsonContext&);

/** Verify a payment channel claim signature.
 *
 *  Checks that the provided signature is valid for the given channel, amount,
 *  and public key without touching the ledger state.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a boolean `signature_verified` field.
 */
json::Value
doChannelVerify(RPC::JsonContext&);

// --- NFTs ---

/** Return the open buy offers for a given NFToken.
 *
 *  Iterates the NFToken's buy-offer directory (`keylet::nft_buys`).
 *  Supports pagination via a marker.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with an `offers` array and an optional `marker`.
 */
json::Value
doNFTBuyOffers(RPC::JsonContext&);

/** Return the open sell offers for a given NFToken.
 *
 *  Iterates the NFToken's sell-offer directory (`keylet::nft_sells`).
 *  Supports pagination via a marker.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with an `offers` array and an optional `marker`.
 */
json::Value
doNFTSellOffers(RPC::JsonContext&);

// --- Ledger access ---

/** Return the hash and index of the most recently closed ledger.
 *
 *  Requires `NeedsClosedLedger` condition; returns `rpcNO_CLOSED` (API v1)
 *  or `rpcNOT_SYNCED` (API v2+) if no closed ledger is available.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `ledger_hash` and `ledger_index`.
 */
json::Value
doLedgerClosed(RPC::JsonContext&);

/** Return the index of the current open ledger.
 *
 *  Requires `NeedsCurrentLedger` condition.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `ledger_current_index`.
 */
json::Value
doLedgerCurrent(RPC::JsonContext&);

/** Return all ledger state entries, optionally paginated.
 *
 *  Iterates the SHAMap of a specified ledger. Binary mode returns raw hex;
 *  JSON mode returns parsed objects. Page sizes are controlled by
 *  `Tuning::binaryPageLength` (2048) and `Tuning::jsonPageLength` (256).
 *  Also supports `marker`+`end_marker` for range-parallel scans (gRPC path).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `state` array and an optional `marker`.
 */
json::Value
doLedgerData(RPC::JsonContext&);

/** Return a single ledger state entry by type and identifier.
 *
 *  Resolves the entry via a parser table driven by `ledger_entries.macro`.
 *  Returns `Expected<uint256, Json::Value>` internally; API v1 re-throws
 *  `Json::error` for compatibility. API v3 (beta) adds human-readable
 *  singleton aliases.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object containing the requested ledger entry.
 */
json::Value
doLedgerEntry(RPC::JsonContext&);

/** Return the header fields of a ledger by hash or index (API v1 only).
 *
 *  Restricted to API version 1; hidden from API v2+ clients. Superseded by
 *  `doLedger` (the new-style `LedgerHandler`) for v2+ clients.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with ledger header fields.
 */
json::Value
doLedgerHeader(RPC::JsonContext&);

/** Trigger acquisition of a specific ledger from the network (ADMIN).
 *
 *  Calls `InboundLedgers::acquire()` and returns the acquisition status.
 *  Intended for diagnostic use; does not block waiting for the ledger.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with acquisition progress information.
 */
json::Value
doLedgerRequest(RPC::JsonContext&);

/** Force the node to advance to the next ledger in standalone mode (ADMIN).
 *
 *  Takes the master mutex and calls `Consensus::simulate()`. Only valid in
 *  standalone mode; used in testing to drive ledger advancement without a
 *  validator network. Requires `NeedsCurrentLedger` condition.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the accepted ledger index.
 */
json::Value
doLedgerAccept(RPC::JsonContext&);

/** Configure the background ledger repair job (ADMIN).
 *
 *  Controls the `LedgerCleaner` subsystem that re-validates and re-acquires
 *  ledger state entries. Requires `NeedsNetworkConnection` condition.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the cleaner configuration.
 */
json::Value
doLedgerCleaner(RPC::JsonContext&);

// --- Transactions ---

/** Sign a transaction and return the signed blob (USER, signing must be enabled).
 *
 *  Applies the caller's key to the provided `tx_json`, filling fee and
 *  sequence if requested. Responds with a `deprecated` warning directing
 *  clients toward local or offline signing.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `tx_blob` and `tx_json` of the signed transaction.
 *  @note Requires `canSign()` config; returns `rpcNOT_SUPPORTED` otherwise.
 */
json::Value
doSign(RPC::JsonContext&);

/** Add one signature to a multi-signed transaction (USER, signing must be enabled).
 *
 *  Validates the signing key against the signer entry and appends the
 *  signature to the `Signers` array. Returns a `deprecated` warning.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the partially-signed `tx_blob` and `tx_json`.
 *  @note Requires `canSign()` config; returns `rpcNOT_SUPPORTED` otherwise.
 */
json::Value
doSignFor(RPC::JsonContext&);

/** Dry-run a transaction against the current open ledger without broadcasting.
 *
 *  Applies `tapDRY_RUN` to validate the transaction and produce metadata
 *  without submitting to the network. Rejects batch transactions.
 *  Requires `NeedsCurrentLedger` condition.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with simulated transaction result and metadata.
 */
json::Value
doSimulate(RPC::JsonContext&);

/** Submit a signed transaction to the network.
 *
 *  Accepts either `tx_blob` (pre-signed binary) or `tx_json` with embedded
 *  signing. Pre-seeds `HashRouter` with the transaction hash before forwarding
 *  so the node does not rebroadcast its own submission.
 *  Requires `NeedsCurrentLedger` condition.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the engine result and transaction hash.
 */
json::Value
doSubmit(RPC::JsonContext&);

/** Submit a transaction that has collected all required multi-signatures.
 *
 *  Validates all signatures against the account's signer list before
 *  submitting. Requires `NeedsCurrentLedger` condition.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the engine result and transaction hash.
 */
json::Value
doSubmitMultiSigned(RPC::JsonContext&);

/** Return a transaction by its hash, searching both the open and closed ledgers.
 *
 *  The `tx` method requires `NeedsNetworkConnection` condition. Enriches
 *  the response with `DeliveredAmount`, NFT synthetic fields, and
 *  `mpt_issuance_id` where applicable. API v2 promotes `hash`,
 *  `ledger_index`, and `ledger_hash` to the top level.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object describing the transaction and its metadata.
 */
json::Value
doTxJson(RPC::JsonContext&);

/** Return a transaction by its position within a specific closed ledger.
 *
 *  Unlike `doTxJson`, this pins the lookup to a ledger identified by hash or
 *  index rather than searching by transaction hash alone.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object describing the transaction and its metadata.
 */
json::Value
doTransactionEntry(RPC::JsonContext&);

/** Return a page of recent transactions in reverse-chronological order (API v1 only).
 *
 *  Restricted to API version 1; removed from v2+ to encourage use of
 *  `account_tx`. Gated on `useTxTables()`; deep-page requests beyond
 *  index 10000 are refused for non-admin callers.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `txs` array of recent transactions.
 */
json::Value
doTxHistory(RPC::JsonContext&);

/** Request that a transaction be relayed to fewer peers to reduce network load.
 *
 *  Used by the transaction reduce-relay feature to suppress redundant
 *  rebroadcasting of already-propagated transactions.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the relay suppression request.
 */
json::Value
doTxReduceRelay(RPC::JsonContext&);

// --- Path finding ---

/** Open or update a streaming path-find subscription (WebSocket only).
 *
 *  Starts an asynchronous path search that delivers results as a series of
 *  pushed `path_find` events over the WebSocket connection. Returns
 *  `rpcNO_EVENTS` for non-WebSocket transports. Requires `NeedsCurrentLedger`
 *  condition.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the initial path result (subsequent results are pushed).
 */
json::Value
doPathFind(RPC::JsonContext&);

/** Return the best payment paths between two accounts (one-shot).
 *
 *  Performs a synchronous path search via `LegacyPathFind`, which uses a
 *  lock-free CAS counter to limit concurrent executions. Admin callers bypass
 *  the concurrency cap.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `alternatives` array of ranked payment paths.
 */
json::Value
doRipplePathFind(RPC::JsonContext&);

// --- Server information ---

/** Return server status and configuration in a human-readable format.
 *
 *  Suitable for operators and interactive clients. Omits some raw counters
 *  that `doServerState` exposes for programmatic consumption.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with server status, peer count, ledger info, and load.
 */
json::Value
doServerInfo(RPC::JsonContext&);  // for humans

/** Return detailed server status and metrics for programmatic consumption.
 *
 *  Exposes raw counters and internal state suitable for monitoring systems.
 *  Complements `doServerInfo`, which formats the same data for human readers.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with raw server metrics.
 */
json::Value
doServerState(RPC::JsonContext&);  // for machines

/** Return the protocol and type definitions used by this node.
 *
 *  Returns a Meyers-singleton payload built once at startup. Includes a
 *  SHA-512-half hash clients can use for cache invalidation.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with field definitions, transaction types, and ledger entry types.
 */
json::Value
doServerDefinitions(RPC::JsonContext&);

/** Return the current transaction cost and fee schedule.
 *
 *  Requires `NeedsCurrentLedger` condition. Reports both the base fee and
 *  the open-ledger escalated fee from the TxQ.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with fee levels, base fee, and reference transaction cost.
 */
json::Value
doFee(RPC::JsonContext&);

/** Suspend transaction processing without stopping the node (ADMIN).
 *
 *  Declared but not yet registered in the handler table; effectively
 *  unreachable until a corresponding entry is added to `kHANDLER_ARRAY`.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the paused state.
 */
json::Value
doPause(RPC::JsonContext&);

/** Resume transaction processing after a `pause` (ADMIN).
 *
 *  Declared but not yet registered in the handler table; effectively
 *  unreachable until a corresponding entry is added to `kHANDLER_ARRAY`.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the resumed state.
 */
json::Value
doResume(RPC::JsonContext&);

/** Initiate a graceful server shutdown (ADMIN).
 *
 *  Signals the application to stop accepting new work and begin teardown.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `message` confirming the shutdown is in progress.
 */
json::Value
doStop(RPC::JsonContext&);

// --- Peer and network admin ---

/** Request that the node connect to a specific peer IP (ADMIN).
 *
 *  Attempts an outbound connection to the given host and optional port.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the connection attempt.
 */
json::Value
doConnect(RPC::JsonContext&);

/** Return the list of currently connected peers (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `peers` array of connection details.
 */
json::Value
doPeers(RPC::JsonContext&);

/** Add a static peer reservation slot (ADMIN).
 *
 *  Peer reservations guarantee connection slots for designated node keys,
 *  even when the peer limit is saturated.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the addition.
 */
json::Value
doPeerReservationsAdd(RPC::JsonContext&);

/** Remove a static peer reservation slot (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the deletion.
 */
json::Value
doPeerReservationsDel(RPC::JsonContext&);

/** Return all configured static peer reservation slots (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `reservations` array.
 */
json::Value
doPeerReservationsList(RPC::JsonContext&);

/** Return the admin blacklist of banned IP ranges (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the blacklist entries.
 */
json::Value
doBlackList(RPC::JsonContext&);

/** Return inbound ledger fetch diagnostics (ADMIN).
 *
 *  Useful for diagnosing why the node is not acquiring ledger history.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with fetch queue statistics.
 */
json::Value
doFetchInfo(RPC::JsonContext&);

/** Return internal consensus state for diagnostic purposes (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with consensus phase, timing, and proposal counts.
 */
json::Value
doConsensusInfo(RPC::JsonContext&);

// --- Validator infrastructure ---

/** Return the current validator list (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the active UNL entries.
 */
json::Value
doValidators(RPC::JsonContext&);

/** Return the configured validator list site URLs and fetch status (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with site URLs and last-fetch results.
 */
json::Value
doValidatorListSites(RPC::JsonContext&);

/** Return information about a specific validator by its public key (ADMIN).
 *
 *  If no manifest exists for the key, returns the master key unchanged,
 *  which is used to distinguish "is master key" from "ephemeral with no manifest".
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the validator's master and signing keys.
 */
json::Value
doValidatorInfo(RPC::JsonContext&);

/** Return the Unique Node List entries currently trusted (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the list of trusted UNL public keys.
 */
json::Value
doUnlList(RPC::JsonContext&);

/** Return the published manifest for a given validator key.
 *
 *  The manifest maps an ephemeral signing key back to its master key.
 *  If no manifest exists, the master key itself is returned.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the manifest fields for the requested key.
 */
json::Value
doManifest(RPC::JsonContext&);

// --- Utility and legacy ---

/** Echo a minimal response to confirm liveness; response varies by role.
 *
 *  Returns an empty object for USER callers and an `unlimited` field for
 *  ADMIN/unlimited callers, allowing clients to detect their privilege level.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object, optionally containing `{"unlimited": true}`.
 */
json::Value
doPing(RPC::JsonContext&);

/** Return a cryptographically random 256-bit value.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a `random` field containing a hex string.
 */
json::Value
doRandom(RPC::JsonContext&);

/** Return a summary of the objects owned by an account (legacy).
 *
 *  Older variant of account object inspection. Requires `NeedsCurrentLedger`
 *  condition. Prefer `doAccountObjects` for new integrations.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a summary of owned ledger objects.
 */
json::Value
doOwnerInfo(RPC::JsonContext&);

/** Return the aggregate balances of a gateway across all trust lines.
 *
 *  Totals obligations (liabilities) and assets for each currency the
 *  gateway issues, optionally including hot-wallet account exemptions.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `obligations` and `balances` maps.
 */
json::Value
doGatewayBalances(RPC::JsonContext&);

/** Identify accounts that need default ripple flag or other trust line adjustments.
 *
 *  Walks the account's trust lines and flags problematic configurations,
 *  such as lines with no-ripple set on one side but not the other.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `problems` array of account IDs.
 */
json::Value
doNoRippleCheck(RPC::JsonContext&);

/** Check whether an account is authorized to receive a given asset.
 *
 *  Evaluates deposit authorization flags and optionally verifies credential
 *  sets. Credential `(issuer, type)` pairs must be sorted canonically.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with a boolean `deposit_authorized` field.
 */
json::Value
doDepositAuthorized(RPC::JsonContext&);

/** Set the minimum ledger the online delete job may remove (ADMIN).
 *
 *  Configures the floor below which the online-delete background job
 *  will not prune ledger history.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the new can-delete threshold.
 */
json::Value
doCanDelete(RPC::JsonContext&);

/** Return or set the status of an amendment (USER for read, varies for write).
 *
 *  Lists all known amendments with their current voted/enabled status.
 *  Admin callers may vote for or against specific amendments.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with amendment feature entries.
 */
json::Value
doFeature(RPC::JsonContext&);

/** Return internal object and memory counts (ADMIN).
 *
 *  Also callable from non-RPC contexts (e.g. `OverlayImpl::getCountsJson`).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with subsystem object counts and memory usage.
 */
json::Value
doGetCounts(RPC::JsonContext&);

/** Dump internal object state to the log (ADMIN).
 *
 *  Triggers a diagnostic print of subsystem internals for offline analysis.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the print was triggered.
 */
json::Value
doPrint(RPC::JsonContext&);

/** Set the log verbosity level for one or all log partitions (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the new log level(s).
 */
json::Value
doLogLevel(RPC::JsonContext&);

/** Trigger a log file rotation (ADMIN).
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the rotation.
 */
json::Value
doLogRotate(RPC::JsonContext&);

/** Generate a wallet key pair and account address (ADMIN).
 *
 *  Entropy warning: fewer than 80 bits of entropy in the passphrase triggers
 *  a strong warning. A passphrase that directly encodes the seed (1751 word
 *  list, Base58, or hex) suppresses the warning.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `master_seed`, `master_key`, `account_id`, and
 *      `public_key` fields.
 */
json::Value
doWalletPropose(RPC::JsonContext&);

/** Generate a validator signing key pair (ADMIN).
 *
 *  Intended for bootstrapping validator key material. In production,
 *  prefer the `validator-keys` tool for air-gapped key generation.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with `validation_public_key` and `validation_seed`.
 */
json::Value
doValidationCreate(RPC::JsonContext&);

// --- Subscriptions ---

/** Register event subscriptions for a WebSocket connection.
 *
 *  Accepts stream names (`ledger`, `transactions`, `validations`, etc.) and
 *  account or book filters. Returns `rpcNO_EVENTS` for non-WebSocket callers
 *  since HTTP has no push channel.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with the initial subscription state.
 *  @note The `book_changes` stream cannot be unsubscribed once registered;
 *      there is no corresponding unsubscribe path for it.
 */
json::Value
doSubscribe(RPC::JsonContext&);

/** Remove previously registered event subscriptions.
 *
 *  Mirrors `doSubscribe`; accepts the same stream and filter arguments.
 *  Returns `rpcNO_EVENTS` for non-WebSocket callers.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object confirming the removed subscriptions.
 */
json::Value
doUnsubscribe(RPC::JsonContext&);

// --- Newer features ---

/** Return information about a Vault ledger object (XLS-66).
 *
 *  Resolves the vault by account and sequence number, then attaches
 *  the associated MPT issuance data.
 *
 *  @param context  RPC dispatch context for this call.
 *  @return JSON object with vault fields and MPT issuance details.
 */
json::Value
doVaultInfo(RPC::JsonContext&);

}  // namespace xrpl

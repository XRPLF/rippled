# RPC

JSON-RPC over HTTP/WebSocket and gRPC. Central handler table dispatches by method name + API version. Roles: ADMIN, USER, IDENTIFIED, PROXY, FORBID, GUEST.

## Key Invariants

- Handler table in `Handler.cpp`: each entry = `{name, function, role, condition, minApiVer, maxApiVer}` as `std::multimap<string, Handler>`. Same method name can have multiple entries with **non-overlapping** version ranges; overlap is a fatal `LogicError()` at startup.
- `conditionMet` checks amendment-blocked, UNL expired, operating mode ≥ SYNCING, validated ledger age < `Tuning::maxValidatedLedgerAge` (2 min), and validated/current gap ≤ 10 ledgers. Standalone mode bypasses age checks.
- API v1 vs v2 error code split: v1 emits `rpcNO_NETWORK`/`rpcNO_CURRENT`/`rpcNO_CLOSED`; v2+ collapses to `rpcNOT_SYNCED`.
- Sensitive fields (`passphrase`, `secret`, `seed`, `seed_hex`) are masked as `<masked>` in error response echoes.
- Batch requests: top-level `"method": "batch"` with `params` array; each sub-request processed independently and accumulated into JSON array.
- API v2 enforces strict JSON typing on previously-permissive boolean fields (e.g., `signer_lists`, `binary`, `forward`, `transactions`).

## Common Bug Patterns

- New handler without entry in `Handler.cpp` static array = handler silently unreachable.
- Wrong `role_` on handler: USER-level with admin data leaks; ADMIN handler accessible to users = security hole.
- `conditionMet` returning false: ensure new conditions are documented and version-coded errors are paired.
- Resource charging: each request gets a fee via `Resource::Consumer`; missing charge allows DoS.
- `maxRequestSize` (1 MB) rejected before JSON parsing; oversized requests get no error detail.
- Marker pagination: callers can forge markers pointing into other accounts' directories — always call `RPC::isRelatedToAccount` before resuming.
- `parse<T>()` returning `std::nullopt` is a programming-error sentinel for type system; user-facing errors go through `required<T>` / `Expected<T, Json::Value>`.
- `loadType` must be set early in handler — escalates to `feeExceptionRPC` automatically on exception if still `feeReferenceRPC`.

## Adding New RPC Handler

1. Declare in `Handlers.h`: `Json::Value doMyCommand(RPC::JsonContext&);`
2. Implement in new file under `src/xrpld/rpc/handlers/<category>/`.
3. Register in `Handler.cpp` `handlerArray` with role, condition, version range.
4. For class-based new-style handler (rare; only `LedgerHandler`, `VersionHandler`): expose static `name`, `role`, `condition`, `minApiVer`, `maxApiVer`; implement `check()` / `writeResult()`; register via `addHandler<T>()`.
5. For gRPC: define in `xrp_ledger.proto`, add `CallData` in `GRPCServerImpl::setupListeners()`, write `doXxxGrpc(RPC::GRPCContext<Request>&)` returning `std::pair<Response, grpc::Status>`.

## Handler Patterns

### Old-style registration (typical)
```cpp
// In Handler.cpp handlerArray[] — REQUIRED for every new handler:
{"my_command", byRef(&doMyCommand), Role::USER, NO_CONDITION},
// role: ADMIN for internal/sensitive, USER for public
// condition: NO_CONDITION, NEEDS_NETWORK_CONNECTION, NEEDS_CURRENT_LEDGER, NEEDS_CLOSED_LEDGER
// version range defaults to [apiMinimumSupportedVersion, apiMaximumValidVersion]
// To version-bound: `{"ledger_header", byRef(&doLedgerHeader), Role::USER, NO_CONDITION, 1, 1}`
```

### Version-Ranged Class Handler
```cpp
// Class with static metadata; registered in HandlerTable ctor via addHandler<T>()
template <> Handler handlerFrom<MyCommandHandler>() {
    return {MyCommandHandler::name, &handle<Json::Value, MyCommandHandler>,
            MyCommandHandler::role, MyCommandHandler::condition,
            MyCommandHandler::minApiVer, MyCommandHandler::maxApiVer};
}
```

### Ledger Resolution
- `RPC::lookupLedger(ledger, context)` for JSON path — handles `ledger_hash`/`ledger_index`/legacy `ledger`/shortcut strings.
- `RPC::ledgerFromRequest<T>(ledger, context)` for gRPC.
- `RPC::getOrAcquireLedger(context)` returns `Expected<shared_ptr<Ledger const>, Json::Value>` and triggers `InboundLedgers::acquire()` for missing ledgers (used only by `ledger_request` admin command).

### Pagination Idiom
- Marker format: `"<uint256_hex>,<uint64_pageHint>"` for owner-directory handlers; raw hex for NFT page chains.
- Request `limit + 1` from `forEachItemAfter`; if `count == limit + 1`, emit marker from limit-th item.
- Always validate marker SLE belongs to requesting account before resuming.
- Limits from `RPC::Tuning::<command>` clamped via `readLimitField()`; admin/unlimited roles bypass clamp.

## Key Files

### Top-level
- `src/xrpld/rpc/handlers/Handlers.h` — authoritative declarations of all old-style handler functions (~67 entries).
- `src/xrpld/rpc/detail/Handler.cpp` — handler table, `getHandler()`, `HandlerTable` singleton, version overlap enforcement.
- `src/xrpld/rpc/detail/Handler.h` — `Handler` struct, `Condition` enum, `conditionMet()` template.
- `src/xrpld/rpc/detail/RPCHandler.cpp` — `doCommand()` pipeline: load-shed, role check, condition check, perf-log instrumented dispatch.
- `src/xrpld/rpc/detail/ServerHandler.cpp` — HTTP/WS server entry; auth, batch handling, version-aware error formatting, secret masking, HTTP status from RPC error codes (ripplerpc ≥ 3.0).
- `src/xrpld/rpc/RPCHandler.h` — `doCommand`, `roleRequired` declarations.
- `src/xrpld/rpc/Context.h` — `Context`, `JsonContext`, `GRPCContext<T>` aggregate dispatch envelopes.
- `src/xrpld/rpc/Request.h` — simpler `Request` envelope (less used; lives alongside `Context`).
- `src/xrpld/rpc/Status.h` — unified error type bridging `TER`, `error_code_i`, and bare int with `inject()` to JSON.
- `src/xrpld/rpc/Role.h` — `Role` enum, `isUnlimited`, `requestRole`, `ipAllowed`, `forwardedFor`.
- `src/xrpld/rpc/detail/Role.cpp` — IP subnet matching, secure_gateway resolution, RFC 7239 / `X-Forwarded-For` parsing.
- `src/xrpld/rpc/detail/RPCHelpers.cpp` / `.h` — pagination, seed parsing, keypair derivation, ledger-entry type selection, MPT/IOU asset parsing.
- `src/xrpld/rpc/detail/RPCLedgerHelpers.cpp` / `.h` — `lookupLedger`, `getLedger`, `getOrAcquireLedger`; staleness checks; gRPC `ledgerFromSpecifier`.
- `src/xrpld/rpc/detail/Tuning.h` — all numeric tunables (limits, ranges, throttles).
- `include/xrpl/protocol/ErrorCodes.h` — `error_code_i`, `inject_error`, `ErrorInfo` table, HTTP status mapping.

### Subscriptions
- `src/xrpld/rpc/detail/WSInfoSub.h` — WebSocket `InfoSub` subclass; `Json::stream`-based zero-intermediate serialization into `multi_buffer`.
- `src/xrpld/rpc/RPCSub.h` / `detail/RPCSub.cpp` — outbound HTTP/HTTPS push subscription ("webhook"); producer/consumer with `jtCLIENT_SUBSCRIBE` jobs; carries `VFALCO TODO` markers (legacy).
- Streams: `server`, `ledger`, `book_changes`, `transactions`, `transactions_proposed` (`rt_transactions` deprecated alias), `validations`, `manifests`, `peer_status` (admin), `consensus`.
- `account_history_tx_stream` is experimental; gated on `useTxTables()`; supports `stop_history_tx_only` in unsubscribe.

### Pathfinding
- `src/xrpld/rpc/detail/Pathfinder.cpp` / `.h` — three-phase engine: `findPaths()` (template expansion via static `mPathTable`), `computePathRanks()` (RippleCalc simulation), `getBestPaths()` (selection with covering-path).
- `src/xrpld/rpc/detail/PathRequest.cpp` / `.h` — per-request state machine; two constructors for `path_find` (subscription) vs `ripple_path_find` (legacy callback); adaptive `iLevel`.
- `src/xrpld/rpc/detail/PathRequestManager.cpp` / `.h` — collection of `wptr<PathRequest>`; re-entrant `updateAll()` loop; shared `AssetCache` via `weak_ptr` (intentional, see `getAssetCache`).
- `src/xrpld/rpc/detail/AssetCache.cpp` / `.h` — per-ledger thread-safe trust line + MPT cache; direction-superset optimization for trust lines; `shared_ptr<vector<>>` null sentinels for empty accounts.
- `src/xrpld/rpc/detail/AccountAssets.cpp` / `.h` — `accountSourceAssets` / `accountDestAssets` for path source/dest currency enumeration.
- `src/xrpld/rpc/detail/TrustLine.cpp` / `.h` — `TrustLineBase` + `PathFindTrustLine` (memory-minimal) + `RPCTrustLine` (adds quality rates).
- `src/xrpld/rpc/detail/MPT.h` — `PathFindMPT` (MPTID + zeroBalance + maxedOut).
- `src/xrpld/rpc/detail/PathfinderUtils.h` — `largestAmount`, `convertAmount`, `convertAllCheck` for "convert all" sentinel handling.
- `src/xrpld/rpc/detail/LegacyPathFind.cpp` / `.h` — RAII concurrency guard for synchronous `ripple_path_find`; lock-free CAS on `inProgress` counter; admin bypass.
- `src/xrpld/rpc/detail/RippleLineCache.cpp` / `.h` — **empty stubs**; functionality replaced by `AssetCache`. Still `#include`d in two files for inert compatibility.

### Transaction Signing / Submission
- `src/xrpld/rpc/detail/TransactionSign.cpp` / `.h` — `transactionSign`, `transactionSubmit`, `transactionSignFor`, `transactionSubmitMultiSigned`; `SigningForParams` mode discriminator; round-trip "sterilization" via `transactionConstructImpl`.
- Fee pipeline: `checkFee` → `getCurrentNetworkFee` (max of load-scaled base fee and TxQ-escalated open ledger fee, capped by `fee_mult_max`/`fee_div_max`).
- `ProcessTransactionFn` dependency injection via `getProcessTxnFn(NetworkOPs&)` for testability.
- `acctMatchesPubKey` handles three account states: unactivated (master-only), master+regular both valid, master disabled (regular only).

### Utility / Enrichment
- `src/xrpld/rpc/BookChanges.h` — header-only template `computeBookChanges<L>(ledger)`; produces OHLCV per pair; reused by RPC handler and `book_changes` subscription stream.
- `src/xrpld/rpc/CTID.h` — Concise Transaction ID (XLS-15d): 16-hex `C` + 28-bit ledgerSeq + 16-bit txnIdx + 16-bit netID. Boost regex; `boost::regex` not `std::regex`.
- `src/xrpld/rpc/DeliveredAmount.h` / `detail/DeliveredAmount.cpp` — `insertDeliveredAmount`; three-tier resolution; threshold = ledger 4594095 (Jan 2014) or close time 446000000s; `"unavailable"` string sentinel for pre-threshold ledgers; lazy lambdas avoid `LedgerMaster` calls when meta has `sfDeliveredAmount`.
- `src/xrpld/rpc/MPTokenIssuanceID.h` / `detail/MPTokenIssuanceID.cpp` — `insertMPTokenIssuanceID`; mirrors `DeliveredAmount` three-function pattern (eligibility / extraction / injection).
- `src/xrpld/rpc/handlers/server_info/ServerDefinitions.cpp` — built once at startup via Meyers singleton; SHA-512-half hash for client cache invalidation; X-macro–driven from protocol headers.
- `src/xrpld/rpc/GRPCHandlers.h` — declarations for 4 gRPC handlers (`doLedgerGrpc`, `doLedgerEntryGrpc`, `doLedgerDataGrpc`, `doLedgerDiffGrpc`). Contract: non-OK `grpc::Status` discards the response object.
- `src/xrpld/rpc/Output.h` — `boost::utility/string_ref`-based output sink. Vestigial; not used by current codebase (canonical sink is `Json::Output`).
- `src/xrpld/rpc/json_body.h` — Boost.Beast `Body` type for JSON HTTP responses; both `reader` and `writer` implement BodyReader concept (eager, one-shot).

### Client-side
- `src/xrpld/rpc/RPCCall.h` / `detail/RPCCall.cpp` — `xrpld` CLI dispatch; `RPCParser` with static `Command[]` table mapping method → parse function; "trusted interface" — minimal validation by design.

## Resource Cost Tiers

Set `context.loadType` early. Tiers (from `Fees.h`):
- `feeReferenceRPC` — default; auto-escalates to `feeExceptionRPC` on uncaught exception.
- `feeMediumBurdenRPC` — directory walks, account_lines, account_offers, simulate, history paging, tx_reduce_relay-class ops.
- `feeHeavyBurdenRPC` — pathfinding, signing, gateway_balances, ledger_request, submit_multisigned.

## Tuning Constants (in `Tuning.h`)

- `maxRequestSize = 1_000_000` — rejected pre-parse in `ServerHandler`.
- `maxJobQueueClients = 500` — `RPCHandler::fillHandler` returns `rpcTOO_BUSY`; admin/unlimited bypass.
- `maxValidatedLedgerAge = 2 min`.
- `maxPathfindsInProgress = 2`, `maxPathfindJobCount = 50`, `max_src_cur = 18`, `max_auto_src_cur = 88`.
- `binaryPageLength = 2048`, `jsonPageLength = 256` — selected via `pageLength(isBinary)` in `ledger_data`.
- Per-command `LimitRange`: most account queries `{10, 200, 400}`; `bookOffers {0, 60, 100}`; `nftOffers {50, 250, 500}`; `noRippleCheck {10, 300, 400}`; `accountNFTokens {20, 100, 400}`.
- `defaultAutoFillFeeMultiplier = 10`, `defaultAutoFillFeeDivisor = 1`.

## Two-Tier Signing Access Gate

Sign-related handlers (`sign`, `sign_for`, `submit` with `tx_json`, `channel_authorize`) enforce:
```cpp
if (context.role != Role::ADMIN && !context.app.config().canSign())
    return rpcNOT_SUPPORTED;
```
`canSign()` reflects `[signing_support]` config; defaults false. Public nodes refuse to sign by default. All `sign`/`sign_for` responses include a `deprecated` warning steering clients to local/offline signing.

## API Version Behavioral Differences

- `apiCommandLineVersion` is used by the CLI; defaults differ from inbound.
- v2 promotes fields from inside transaction objects to top-level: `hash`, `ledger_index`, `ledger_hash`, `close_time_iso`.
- v2 renames metadata keys: `tx` → `tx_json`, `meta` → `meta_blob` for binary.
- v2 renames Payment `Amount` → `DeliverMax` (via `RPC::insertDeliverMax`).
- v2 strict boolean typing; v1 silently coerces.
- v2 rejects mixing `ledger_index_min`/`max` with `ledger_index`/`ledger_hash` in `account_tx`; v1 tolerates.
- v2 enforces precise marker objects in `account_tx` (`{ledger, seq}` integers).
- v3 (beta) adds human-readable singleton aliases in `ledger_entry` index lookup.

## Handler Subdirectory Map

- `handlers/account/` — `AccountInfo`, `AccountLines`, `AccountChannels`, `AccountCurrencies`, `AccountNFTs`, `AccountObjects`, `AccountOffers`, `AccountTx`, `GatewayBalances`, `NoRippleCheck`, `OwnerInfo` (legacy).
- `handlers/admin/` — `BlackList`, `UnlList`, plus subdirectories for `data/` (CanDelete, LedgerCleaner, LedgerRequest), `keygen/` (WalletPropose, ValidationCreate), `log/`, `peer/`, `server_control/` (Stop, LedgerAccept — standalone-only), `signing/` (ChannelAuthorize, Sign, SignFor), `status/` (ConsensusInfo, FetchInfo, GetCounts, Print, ValidatorInfo, Validators, ValidatorListSites).
- `handlers/ledger/` — `Ledger` (class-based), `LedgerClosed`, `LedgerCurrent`, `LedgerData`, `LedgerDiff` (gRPC-only), `LedgerEntry` (parser table from `ledger_entries.macro`), `LedgerHeader`.
- `handlers/orderbook/` — `AMMInfo`, `BookChanges`, `BookOffers`, `DepositAuthorized`, `GetAggregatePrice`, `NFTBuyOffers` / `NFTSellOffers` / `NFTOffersHelpers.h`, `PathFind` (subscription), `RipplePathFind` (one-shot).
- `handlers/server_info/` — `Fee`, `Feature`, `Manifest`, `ServerDefinitions`, `ServerInfo`, `ServerState`, `Version.h` (class-based).
- `handlers/subscribe/` — `Subscribe`, `Unsubscribe`.
- `handlers/transaction/` — `Simulate` (dry-run via `tapDRY_RUN`), `Submit`, `SubmitMultiSigned`, `Tx`, `TransactionEntry` (ledger-pinned), `TxHistory` (paginated, `useTxTables()`-gated, deep-page cap 10000 for non-admin), `TxReduceRelay`.
- `handlers/utility/` — `Ping` (role-conditional response), `Random`.
- Top-level `handlers/`: `ChannelVerify` (no admin restriction, stateless), `VaultInfo` (XLS-66, vault + MPT issuance lookup).

## gRPC Specifics

- Four handlers: `Ledger`, `LedgerEntry`, `LedgerData` (binary-only, fixed page=2048, supports `marker`+`end_marker` for range parallelism), `LedgerDiff` (SHAMap delta; downcast `ReadView`→`Ledger` is the validation gate).
- `doLedgerGrpc` adds `get_objects` (state diff via `SHAMap::compare`) and `get_object_neighbors` (DEX best-offer tracking via `keylet::quality`).
- Handlers return `std::pair<Response, grpc::Status>`. Non-OK status discards response.
- Error mapping: `rpcINVALID_PARAMS` → `INVALID_ARGUMENT`; ledger missing → `NOT_FOUND`; diff overflow → `RESOURCE_EXHAUSTED`.

## Key Gotchas

- `noEvents` (`rpcNO_EVENTS`) is returned by `path_find` and `subscribe`/`unsubscribe` for non-WebSocket transports — HTTP has no push channel.
- `LegacyPathFind` admit-failure means destructor must not decrement; uses `m_isOk` flag.
- `getMasterKey` returns the input key unchanged when no manifest exists — used in `doManifest`/`doValidatorInfo` to distinguish "is master key" from "ephemeral with no manifest".
- `ledger_accept` only works in standalone mode; takes master mutex; drives `Consensus::simulate`.
- `ChannelAuthorize` / `ChannelVerify` use `HashPrefix::paymentChannelClaim` ('CLM') as domain separator; canonical message = prefix + 32-byte channelID + 8-byte drops.
- `deposit_authorized` with credentials: must sort `(issuer, type)` pairs canonically via `credentials::makeSorted` before computing keylet; `lifeExtender` vector keeps SLEs alive so `Slice` views into `sfCredentialType` remain valid.
- `LedgerEntry` uses `Expected<uint256, Json::Value>` parser return type rather than exceptions; v1 still re-throws `Json::error` for compatibility.
- `nft_buy_offers` / `nft_sell_offers` differ only by `keylet::nft_buys` vs `keylet::nft_sells` — both delegate to `enumerateNFTOffers` in `NFTOffersHelpers.h`.
- `getCountsJson` (in `GetCounts.h`) is callable from non-RPC contexts (e.g., `OverlayImpl::getCountsJson`).
- `wallet_propose` entropy warning: <80 bits → strong warning; passphrase that already encodes the seed (1751/Base58/hex) suppresses warning.
- Account marker security: always verify `RPC::isRelatedToAccount(*ledger, sle, accountID)` on resumed pagination — prevents cross-account directory traversal.

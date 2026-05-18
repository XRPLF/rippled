# `src/xrpld/rpc/handlers/Handlers.h`

## Role in the System

This header is the central manifest of every "old-style" RPC handler function in the `rippled` server. It declares 67 free functions — one per public API method — all sharing the same signature: `Json::Value doXxx(RPC::JsonContext&)`. The file's sole purpose is to make all handler symbols available in a single include, which `Handler.cpp` then uses to populate the static dispatch table that routes incoming RPC requests to their implementations.

## Design Pattern: Old-Style vs New-Style Handlers

The XRPL RPC subsystem supports two handler registration patterns. The functions declared here represent the **old-style pattern**: plain free functions that accept a `JsonContext&` and return a `Json::Value`. The newer alternative, exemplified by `LedgerHandler` in the only header this file directly includes (`handlers/ledger/Ledger.h`), uses a class with static metadata members (`name`, `role`, `condition`, `minApiVer`, `maxApiVer`) and instance methods `check()` and `writeResult()`.

The `Handler.cpp` file bridges these two worlds. For the old-style functions it wraps each one with a `byRef()` lambda adapter that converts the return-by-value signature into the `Handler::Method<Json::Value>` type expected by the dispatch table. The adapter also enforces a defensive invariant: if a handler accidentally returns a non-object `Json::Value`, it logs the violation and wraps it. New-style handlers are registered directly via `addHandler<T>()`, which reads their static members at compile time and uses `static_assert` to catch version range errors before they reach runtime.

The deliberate structural comment in the header file on lines 110–112 — `doServerInfo` labelled "for humans" versus `doServerState` labelled "for machines" — illustrates how the old-style pattern encodes operational intent through naming and comments rather than through typed metadata.

## The Dispatch Table Relationship

The `HandlerTable` singleton in `Handler.cpp` is where the functions declared here gain their operational identity. Each handler entry pairs the function pointer with a string method name (e.g., `"account_info"`), a `Role` (`USER` or `ADMIN`), and a `Condition` bitmask. The `Condition` enum has three meaningful values — `NEEDS_NETWORK_CONNECTION`, `NEEDS_CURRENT_LEDGER`, and `NEEDS_CLOSED_LEDGER` — and the `conditionMet()` function in `Handler.h` gates every non-`NO_CONDITION` handler against the node's current sync state, amendment-blocked status, and UNL validity before the handler itself ever runs.

The table uses a `std::multimap<std::string, Handler>` rather than a simple `std::map`, because the same method name can be registered under different API version ranges. This supports the versioned API model where behavior changes between `apiMinimumSupportedVersion` and `apiMaximumSupportedVersion`. The `ledger_header` and `tx_history` handlers, for example, are restricted to API version 1 only (`minApiVer=1, maxApiVer=1`), meaning they were deprecated before API version 2 was introduced.

## The `RPC::JsonContext` Parameter

Every handler receives an `RPC::JsonContext&`, which inherits from `RPC::Context`. The base provides the full application environment: `Application& app`, `NetworkOPs& netOps`, `LedgerMaster& ledgerMaster`, `Resource::Consumer& consumer`, `Role role`, and an optional coroutine handle for async handlers. `JsonContext` adds `Json::Value params` (the parsed request body) and HTTP header forwarding fields for user identity and proxy chain tracking. This consolidated context design means handlers never need to reach for global state — everything needed for request execution and audit logging arrives in one argument.

## Handler Groupings and Feature Surface

The 67 declarations cover the full XRPL public and administrative API surface:

- **Account queries**: `doAccountInfo`, `doAccountLines`, `doAccountChannels`, `doAccountNFTs`, `doAccountObjects`, `doAccountOffers`, `doAccountTx`, `doAccountCurrencies` — the core account inspection methods.
- **DEX and orderbook**: `doBookOffers`, `doBookChanges`, `doAMMInfo`, `doGetAggregatePrice` — covering both traditional order-book and Automated Market Maker surfaces.
- **Payment channels**: `doChannelAuthorize`, `doChannelVerify` — off-ledger payment channel primitives.
- **NFTs**: `doNFTBuyOffers`, `doNFTSellOffers` — NFT marketplace queries.
- **Ledger access**: `doLedgerClosed`, `doLedgerCurrent`, `doLedgerData`, `doLedgerEntry`, `doLedgerHeader`, `doLedgerRequest`, `doLedgerAccept`, `doLedgerCleaner` — the last two are admin-only, with `doLedgerAccept` forcing ledger advancement in standalone mode.
- **Transactions**: `doSign`, `doSignFor`, `doSubmit`, `doSubmitMultiSigned`, `doSimulate`, `doTransactionEntry`, `doTxJson`, `doTxHistory`, `doTxReduceRelay` — covering the complete transaction lifecycle from signing to historical lookup. `doSimulate` is a dry-run execution path that validates without broadcasting.
- **Path finding**: `doPathFind`, `doRipplePathFind` — streaming (`doPathFind`) and one-shot (`doRipplePathFind`) path search.
- **Server operations**: `doServerInfo`, `doServerState`, `doServerDefinitions`, `doFee`, `doPause`, `doResume`, `doStop` — `doPause`/`doResume` allow operators to temporarily suspend transaction processing without shutting down the node.
- **Peer and network admin**: `doConnect`, `doPeers`, `doPeerReservationsAdd`, `doPeerReservationsDel`, `doPeerReservationsList`, `doBlackList`, `doFetchInfo`, `doConsensusInfo` — privileged network management methods.
- **Validator infrastructure**: `doValidators`, `doValidatorListSites`, `doValidatorInfo`, `doUnlList`, `doManifest` — UNL and validator set introspection.
- **Utility and legacy**: `doPing`, `doRandom`, `doOwnerInfo`, `doGatewayBalances`, `doNoRippleCheck`, `doDepositAuthorized`, `doCanDelete`, `doFeature`, `doGetCounts`, `doPrint`, `doLogLevel`, `doLogRotate`, `doWalletPropose`, `doValidationCreate` — operational diagnostics and some helpers retained for compatibility.
- **Subscriptions**: `doSubscribe`, `doUnsubscribe` — the WebSocket streaming subscription interface.
- **Newer features**: `doVaultInfo` — vault ledger object inspection, reflecting ongoing protocol development.

## Architectural Significance

The deliberate separation of this header from `Handler.cpp` is what makes the dispatch table editable without touching any handler implementation file. Adding a new method requires only declaring it here, implementing it in its own translation unit, and inserting one entry in the `handlerArray` in `Handler.cpp`. The `byRef()` adapter makes the registration mechanical and uniform. The single include of `handlers/ledger/Ledger.h` — rather than `Context.h` directly — is intentional: it ensures that every translation unit including `Handlers.h` receives not just the function declarations but also the full `JsonContext` type definition needed to write a valid call site or a new handler body.
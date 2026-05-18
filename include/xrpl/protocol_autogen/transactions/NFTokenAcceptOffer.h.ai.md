# `NFTokenAcceptOffer.h` — Auto-Generated NFT Offer Acceptance Transaction

## File Role

This header is part of the `protocol_autogen` layer, a family of auto-generated C++ wrappers sitting above the XRPL core serialization types (`STTx`, `STObject`). Every transaction type in `include/xrpl/protocol_autogen/transactions/` follows the same structural pattern; this file instantiates it for `ttNFTOKEN_ACCEPT_OFFER` (type code 29).

The file exists to solve a recurring problem in XRPL transaction handling: the underlying `STTx` / `STObject` infrastructure stores fields in a loosely-typed map keyed by `SField` descriptors. Without a wrapper layer, every call site must know which field names to query, check presence manually, and cast to the right type — a pattern that is both verbose and fragile. `NFTokenAcceptOffer` and `NFTokenAcceptOfferBuilder` eliminate that friction for one specific transaction type.

## The Transaction Semantics

`NFTokenAcceptOffer` covers three distinct modes of NFT trade settlement on the ledger:

- **Direct sell**: the account that received a sell offer accepts it by specifying `sfNFTokenSellOffer`.
- **Direct buy**: the original NFT owner accepts an incoming buy offer by specifying `sfNFTokenBuyOffer`.
- **Brokered**: a third-party broker submits the transaction specifying *both* a buy and a sell offer, optionally extracting `sfNFTokenBrokerFee` from the transaction proceeds. The broker need not own the NFT or be either original counterparty.

All three fields are declared `soeOPTIONAL` in the ledger schema, which is why the wrapper returns `protocol_autogen::Optional<…>` (a thin alias for `std::optional`) from every getter rather than a value directly. The caller is forced to handle absent fields at compile time through the optional API, rather than relying on runtime checks or exception-based field access.

## `NFTokenAcceptOffer` — Immutable Read Wrapper

`NFTokenAcceptOffer` inherits `TransactionBase`, which holds a single `std::shared_ptr<STTx const> tx_` and exposes type-safe getters for every universal transaction field (`getAccount()`, `getFee()`, `getSequence()`, `getMemos()`, etc.). The derived class adds only the three transaction-specific getters.

Construction accepts a `std::shared_ptr<STTx const>` and immediately validates the transaction type against the class-level constant `txType = ttNFTOKEN_ACCEPT_OFFER`. Passing a mismatched `STTx` throws `std::runtime_error`. This check is intentionally eager — it catches integration errors at the earliest possible moment rather than allowing a silently wrong wrapper to propagate through business logic.

Each optional field follows a strict two-method pattern:

```cpp
getNFTokenBuyOffer()   // returns Optional<uint256>, checks presence first
hasNFTokenBuyOffer()   // cheap boolean field-presence query
```

The getter delegates presence checking to `hasNFTokenBuyOffer()` before accessing `tx_->at(sfNFTokenBuyOffer)`. This avoids the exception that `STObject::at` would throw on a missing field, translating it into an `std::nullopt` return instead. All getters carry `[[nodiscard]]` to prevent callers from silently discarding the returned optional.

The wrapper is intentionally immutable — there are no setters. The separation between mutable construction and immutable reading is a deliberate design choice: once a transaction is wrapped in `NFTokenAcceptOffer`, its field contents are frozen, and only `NFTokenAcceptOfferBuilder` can produce new instances.

## `NFTokenAcceptOfferBuilder` — Fluent Construction

`NFTokenAcceptOfferBuilder` inherits `TransactionBuilderBase<NFTokenAcceptOfferBuilder>`, which uses CRTP so that every common setter in the base (`setFee()`, `setSequence()`, `setLastLedgerSequence()`, etc.) returns a `NFTokenAcceptOfferBuilder&` rather than a `TransactionBuilderBase&`. This preserves the concrete type across the entire method chain without virtual dispatch.

The base class maintains a mutable `STObject object_{sfTransaction}` rather than an `STTx`. This distinction matters: `STTx` enforces the transaction schema template (`soTemplate`) at construction, which would reject missing or default-valued fields. By keeping state as a free `STObject` during the build phase, the builder can accumulate fields incrementally without triggering those constraints. Only at `build()` time, when the `STObject` is moved into `STTx`, does schema validation fire.

The builder offers two construction paths:

1. **From scratch**: `NFTokenAcceptOfferBuilder(account, sequence, fee)` — initializes `sfTransactionType`, `sfAccount`, and optionally `sfSequence` and `sfFee`. Sequence and fee are optional parameters with `std::nullopt` defaults, accommodating workflows where the fee is auto-filled by a server or the sequence comes from an account info query.

2. **From an existing `STTx`**: the second constructor copies a validated `STTx` back into `object_` via `object_ = *tx`. This enables round-trip editing: deserialize a transaction from the wire, wrap it in the builder to modify fields, then call `build()` to produce a new signed transaction. The type check mirrors the one in `NFTokenAcceptOffer` — mismatched type throws immediately.

The transaction-specific setters use `std::decay_t<typename SF_UINT256::type::value_type>` as the parameter type. The `std::decay_t` strips references and cv-qualifiers from whatever the `SField` type system resolves, ensuring clean pass-by-const-reference semantics regardless of how the underlying type alias is defined.

`build()` calls `sign()` (inherited from `TransactionBuilderBase`), which serializes the `STObject` without signing fields, prepends the `HashPrefix::txSign` tag, signs the resulting bytes with the provided key pair, and stores both `sfSigningPubKey` and `sfTxnSignature` back into `object_`. The `STObject` is then moved into a freshly constructed `STTx` and wrapped in the immutable `NFTokenAcceptOffer`. The move means the builder's internal state is consumed — it should not be reused after calling `build()`.

## Relationship to the Broader `protocol_autogen` Layer

This file is one of roughly 70 identically structured transaction headers in the `transactions/` directory, all generated from a common schema. The design contracts established by `TransactionBase` and `TransactionBuilderBase` are the same across all of them: immutable read-type on one side, CRTP builder on the other. The pattern makes transaction-type-specific code trivially thin — here the entire transaction-specific surface is three optional `uint256`/`STAmount` fields — while ensuring that common infrastructure such as schema validation (`validateSTObject`), local ledger checks (`passesLocalChecks`), and signing lives in the shared base classes rather than being duplicated.
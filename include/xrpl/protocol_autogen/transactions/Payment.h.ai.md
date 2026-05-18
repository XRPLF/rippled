# `Payment.h` — Auto-Generated Payment Transaction Wrapper

## Role and Context

`Payment.h` is part of the `protocol_autogen` layer, a code-generated subsystem under `include/xrpl/protocol_autogen/transactions/`. Over seventy transaction types follow the same structural pattern in this directory; `Payment` represents `ttPAYMENT` (ordinal 0), the most fundamental XRPL operation. The file header carries a strict `// This file is auto-generated. Do not edit.` guard — the source of truth is the generator, not this file.

The file lives in `namespace xrpl::transactions` and defines two cooperating classes: the immutable `Payment` read-wrapper and the mutable `PaymentBuilder`, each inheriting from separate base templates in `TransactionBase.h` and `TransactionBuilderBase.h`.

## Two-Class Design: Immutable Reader + Fluent Builder

This pattern separates concerns cleanly. Once a `Payment` object exists, it cannot be mutated — it wraps a `std::shared_ptr<STTx const>`, inheriting this guarantee from `TransactionBase`. Callers read fields through typed getters that delegate to `STTx::at()` or `STTx::isFieldPresent()`. There is no way to accidentally modify a transaction that has already been signed and submitted.

Construction goes through `PaymentBuilder`, which extends `TransactionBuilderBase<PaymentBuilder>` using the Curiously Recurring Template Pattern (CRTP). The base class defines setters for all fields common to every XRPL transaction (`setAccount`, `setFee`, `setSequence`, `setMemos`, `setLastLedgerSequence`, etc.) and makes them return `Derived&` instead of the base type. This means every setter in both the base and the derived builder participates in the same fluent chain without virtual dispatch and without slicing.

The `PaymentBuilder` constructor requires the two fields that the XRPL protocol marks `soeREQUIRED` for Payment: `account` and `destination` as `SF_ACCOUNT::type::value_type`, and `amount` as `SF_AMOUNT::type::value_type`. Optional `sequence` and `fee` parameters allow immediate configuration at construction time. The base class deliberately avoids calling `object_.set(soTemplate)` on the internal `STObject` — a subtle but important detail explained in `TransactionBuilderBase.h`: calling it would create placeholder entries for `soeDEFAULT` fields (like `sfPaths`), causing `applyTemplate()` inside the `STTx` constructor to throw "may not be explicitly set to default". Deferring template application to `STTx` construction sidesteps the issue entirely.

## Payment-Specific Fields

The field set exposes the complete XRPL Payment schema:

**Required fields** accessed directly (no `Optional` wrapper):

- `getDestination()` — returns the recipient `AccountID` via `sfDestination`.
- `getAmount()` — returns `SF_AMOUNT::type::value_type`, which covers XRP drops, issued currency amounts, and MPT (Multi-Purpose Token) amounts. MPT support is called out explicitly in the comment because `sfAmount` in Payment is one of the few fields that allows the newer MPT amount type alongside legacy XRP/IOU amounts.

**Optional fields** use a paired `hasX()` / `getX()` accessor idiom, where `getX()` returns `protocol_autogen::Optional<T>` (`std::nullopt` when absent):

- `getSendMax()` / `hasSendMax()` — an MPT-aware maximum spend amount used for cross-currency payments. When present alongside `sfPaths`, it instructs the pathfinding engine to find a route that delivers `sfAmount` while spending no more than `sfSendMax`.
- `getDeliverMin()` / `hasDeliverMin()` — the minimum the destination must receive for a partial-payment transaction to succeed. Also MPT-capable.
- `getDestinationTag()` / `hasDestinationTag()` — a 32-bit integer routing hint allowing the destination to multiplex incoming payments (e.g., per-user identifiers on an exchange).
- `getInvoiceID()` / `hasInvoiceID()` — a free-form `uint256` that can reference an invoice or order, enabling off-ledger reconciliation.
- `getCredentialIDs()` / `hasCredentialIDs()` — a `SF_VECTOR256` list of Credential object IDs. When the destination account requires Deposit Authorization, the sender can present credentials authorizing the payment without a pre-existing `DepositPreauth` ledger entry.
- `getDomainID()` / `hasDomainID()` — a `uint256` reference to a Permissioned Domain. Payments destined for accounts under a permissioned domain must supply the matching domain ID.

**The `sfPaths` special case**: `getPaths()` diverges from the `protocol_autogen::Optional` pattern and returns `std::optional<std::reference_wrapper<STPathSet const>>`. This is because `sfPaths` is a structurally complex nested type (`STPathSet`) that doesn't map to the scalar `SF_*` field descriptor system used by the rest of the getters. The builder's `setPaths()` correspondingly calls `object_.setFieldPathSet(sfPaths, value)` rather than the generic `object_[sfPaths] = value` assignment. The field is annotated `soeDEFAULT`, meaning it is absent in the serialized form when empty rather than serialized as a zero-length container.

## Type Safety and Error Handling

Both `Payment(std::shared_ptr<STTx const>)` and `PaymentBuilder(std::shared_ptr<STTx const>)` validate the transaction type at construction, throwing `std::runtime_error` if `getTxnType() != ttPAYMENT`. This prevents a caller from accidentally wrapping, say, an `OfferCreate` in a `Payment` accessor. The `static constexpr txType = ttPAYMENT` constant ties the class permanently to that transaction code.

Validation of full schema conformance lives in `TransactionBase::validate()`, which delegates to `protocol_autogen::validateSTObject` against the `TxFormats` SO template and then calls `passesLocalChecks`. This is not called at construction — it is offered as an explicit opt-in, consistent with how the broader `rippled` codebase treats local pre-submission checks.

## Build and Sign Flow

`PaymentBuilder::build(publicKey, secretKey)` finalizes the transaction in two steps. First it calls `sign()` from `TransactionBuilderBase`, which serializes the in-progress `STObject` with `HashPrefix::txSign` prepended (omitting signing fields), signs the resulting byte slice, and writes `sfSigningPubKey` and `sfTxnSignature` back into `object_`. Then it constructs a new `STTx` by moving `object_` into the `STTx` constructor — at which point `applyTemplate()` validates that all required fields are present — and wraps it in a `Payment`. The returned `Payment` is fully immutable and ready to serialize for network submission.

## Delegation and Amendment Context

The class comment records `Delegation::delegable`, meaning a `Payment` may include `sfDelegate` (inherited from `TransactionBase`) to exercise a delegated authority grant. `Amendment: uint256{}` (the zero hash) signals that this transaction type requires no amendment gate — it is a core protocol feature available on all ledger versions. The `createAcct | mayCreateMPT` privilege flags are metadata consumed by the code generator and the protocol's privilege-checking machinery, documenting that a Payment may implicitly create the destination account if funded above the reserve, and may create MPT-related state during payment processing.
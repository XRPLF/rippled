# `AMMWithdraw.h` — Auto-generated AMM Withdrawal Transaction Wrapper

## Role and Context

This header is part of the `xrpl/protocol_autogen/transactions` layer — a set of auto-generated, type-safe C++ wrappers for every XRPL transaction type. It defines the `AMMWithdraw` class (transaction type `ttAMM_WITHDRAW`, numeric ID 37) and its companion `AMMWithdrawBuilder`, both living in the `xrpl::transactions` namespace. The file implements the AMM (Automated Market Maker) withdrawal operation introduced by the `featureAMM` amendment, which allows liquidity providers to redeem LP tokens for the underlying pool assets.

The header opens with `// This file is auto-generated. Do not edit.`, which is the decisive architectural choice here: rather than hand-maintaining one large transaction registry, the codebase generates one focused header per transaction type. This keeps each file minimal, makes diffs readable, and allows the generator to enforce uniform structure across all transaction kinds.

## The Two-Class Pattern

Every generated transaction header in this directory follows an identical structural pattern: an immutable **read wrapper** (`AMMWithdraw`) paired with a mutable **builder** (`AMMWithdrawBuilder`). This separation enforces a clear lifecycle — once a transaction has been signed and finalized into an `STTx`, it must be consumed only through the read-only wrapper. Mutation is only possible via the builder before signing.

## `AMMWithdraw` — Immutable Read Wrapper

`AMMWithdraw` inherits from `TransactionBase`, which provides all common-field accessors (`getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, `getSigners()`, etc.) backed by a `std::shared_ptr<STTx const>`. The derived class adds withdrawal-specific field access.

The constructor takes a `std::shared_ptr<STTx const>` and immediately guards against misuse with a type check: if the wrapped `STTx` is not of type `ttAMM_WITHDRAW`, a `std::runtime_error` is thrown. This is an important defensive invariant — the `shared_ptr<const>` guarantees immutability at the type level, but the runtime check guarantees identity. Callers that dispatch from a generic `STTx` can safely construct this wrapper and rely on the exception rather than silent misinterpretation.

The class exposes six field accessors divided into two groups:

**Required fields** — always present and returned by value:
- `getAsset()` / `getAsset2()` — return `SF_ISSUE::type::value_type`, identifying the two asset sides of the target AMM pool. These fields use `sfAsset` and `sfAsset2` typed as `STIssue`, which can represent either a classic IOU token (currency + issuer) or a Multi-Purpose Token (MPT). The comment `@note This field supports MPT amounts` signals that the AMM implementation has been extended to support the `featureMPT` amendment's token type.

**Optional fields** — guarded by a `hasXxx()` check and returned as `protocol_autogen::Optional<T>`:
- `getAmount()` / `hasAmount()` — specifies the exact amount of asset 1 to withdraw (single-asset withdrawal mode).
- `getAmount2()` / `hasAmount2()` — specifies the exact amount of asset 2 to withdraw (single-asset or two-asset withdrawal mode).
- `getEPrice()` / `hasEPrice()` — the effective price, used in single-asset withdrawal to constrain the LP token burn rate.
- `getLPTokenIn()` / `hasLPTokenIn()` — the number of LP tokens the account is redeeming. This is the inverse of `sfLPTokenOut` found in `AMMDeposit`: withdrawal *takes in* LP tokens and *produces* pool assets; deposit *takes in* pool assets and *produces* LP tokens.

The `protocol_autogen::Optional<T>` alias wraps `std::optional<T>`. Each optional getter checks field presence via `tx_->isFieldPresent(sf...)` before calling `tx_->at(...)` — this matters because calling `at()` on a missing field would throw, making the presence-check not just a convenience but a correctness requirement.

## `AMMWithdrawBuilder` — Fluent Transaction Constructor

`AMMWithdrawBuilder` inherits from `TransactionBuilderBase<AMMWithdrawBuilder>` using the Curiously Recurring Template Pattern (CRTP). This gives the builder all common field setters (`setFee()`, `setFlags()`, `setLastLedgerSequence()`, `setDelegate()`, etc.) while making each setter return `Derived&` — i.e., `AMMWithdrawBuilder&` — so callers can chain calls without casting.

The primary constructor enforces that `sfAsset` and `sfAsset2` are always set, since the AMM pool is identified by this pair. Sequence and fee are optional at construction time and may be set later via the inherited setters — common when the caller obtains sequence or fee asynchronously from the network.

A secondary constructor accepts a `std::shared_ptr<STTx const>` and copies the underlying `STObject` into `object_`. This exists for round-trip editing: a received or decoded transaction can be loaded into a builder, modified (for example to bump the fee), and re-signed. The type guard (`ttAMM_WITHDRAW`) prevents accidentally loading the wrong transaction into this builder.

For the required `sfAsset` and `sfAsset2` setters, values are explicitly wrapped in `STIssue(sfAsset, value)` before assignment. This differs from the optional amount setters, which assign `STAmount` values directly. The `STIssue` construction is necessary because the `STObject` operator[] for issue fields requires the correct `STIssue` subtype rather than a raw `Issue` value.

The `build()` method finalizes construction: it calls the protected `sign()` from `TransactionBuilderBase`, which serializes the `STObject` with `HashPrefix::txSign` prepended, computes the signature, sets `sfSigningPubKey` and `sfTxnSignature` on the object, and then constructs an `STTx` from the moved `STObject`. The result is immediately wrapped in a new `AMMWithdraw` instance and returned. After `build()` is called, `object_` has been moved-from and the builder should not be reused.

## AMM Withdrawal Modes and the Optional Field Design

The XRPL AMM supports several withdrawal modes, each determined by which combination of optional fields is present:

- **LP token redemption** (`sfLPTokenIn` only): redeem a fixed number of LP tokens for a proportional share of both pool assets.
- **Single-asset withdrawal** (`sfAmount` + optionally `sfEPrice`): withdraw a specific amount of one asset, burning however many LP tokens the pool's math requires. `sfEPrice` constrains the maximum effective price to protect against slippage.
- **Two-asset withdrawal** (`sfAmount` + `sfAmount2`): withdraw specific amounts of both assets simultaneously.

The presence-check API (`hasLPTokenIn()`, `hasAmount()`, etc.) reflects this protocol-level optionality directly in the C++ type system. The caller is responsible for supplying a valid combination; field-level validation against the AMM's transaction template is performed by `TransactionBase::validate()`, which delegates to `validateSTObject()` against the registered `SOTemplate`.

## Privileges

The transaction metadata notes `Privileges: mayDeleteAcct | mayAuthorizeMPT`, meaning `AMMWithdraw` is permitted in contexts where account deletion and MPT authorization are allowed. This contrasts with `AMMDeposit`, which carries `noPriv`. The elevated privilege for withdrawal is consistent with the operational expectation that exiting liquidity positions should be permitted even during administrative states where new deposits are restricted.
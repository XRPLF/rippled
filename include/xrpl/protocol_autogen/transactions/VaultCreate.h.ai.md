# `VaultCreate.h` — Auto-Generated Vault Creation Transaction Wrapper

## Role in the System

This file is part of the `protocol_autogen` code-generation layer inside `xrpl::transactions`. It implements the `VaultCreate` transaction (type code `ttVAULT_CREATE = 65`), which creates a single-asset vault on the XRPL ledger. The vault primitive is gated behind the `featureSingleAssetVault` amendment and serves as the foundation for on-ledger asset pooling — holders deposit one asset class and receive a proportional share token (backed by an MPT issuance) in return.

The file is marked `// This file is auto-generated. Do not edit.` and follows the same template as every other transaction in the `include/xrpl/protocol_autogen/transactions/` directory. The pattern is consistent across ~70 transaction types: a read-only wrapper class and a corresponding fluent builder, both living in the `xrpl::transactions` namespace.

## Class Hierarchy and Design

`VaultCreate` inherits from `TransactionBase`, which wraps a `std::shared_ptr<STTx const>` and exposes read-only accessors for all common transaction fields (`sfAccount`, `sfSequence`, `sfFee`, `sfFlags`, `sfSigners`, etc.). `VaultCreate` itself adds vault-specific field accessors on top of that shared foundation.

`VaultCreateBuilder` inherits from the CRTP template `TransactionBuilderBase<VaultCreateBuilder>`. The CRTP is load-bearing here: every setter in the base class calls `static_cast<Derived&>(*this)` before returning, ensuring that method chaining on a `VaultCreateBuilder&` keeps the concrete derived type in scope rather than collapsing back to `TransactionBuilderBase&`. Without this, a call like `builder.setLastLedgerSequence(100).setAsset(issue)` would fail to compile because `setAsset` does not exist on the base. The builder accumulates field assignments into an `STObject object_{sfTransaction}` member and defers constructing the final `STTx` until `build()` is called.

## Immutability Split

There is a deliberate separation between reading and writing. `VaultCreate` is constructed only from an existing, validated `STTx const` pointer — it never mutates its contents. `VaultCreateBuilder` is the only construction path for new transactions: it accepts the required `account` and `asset` fields in its primary constructor, sets optional fields through chained setters, calls `sign()` inside `build()`, and then constructs a `VaultCreate` from the resulting signed `STTx`.

Both classes validate the transaction type at construction. If the incoming `STTx` has a type other than `ttVAULT_CREATE`, both constructors throw `std::runtime_error`. This prevents the wrapper and builder from being misused as generic `STTx` containers.

## Transaction Fields

`sfAsset` is the only required field. It is typed as `SF_ISSUE::type::value_type` — an issue specifier that identifies the asset class the vault accepts. The `setAsset()` setter wraps the value in an `STIssue(sfAsset, value)` rather than assigning it directly; this is the only field setter in the class that wraps its argument, reflecting the ledger's treatment of `sfAsset` as a structured `STIssue` rather than a raw scalar.

The optional fields cover the full configuration surface of a vault:

- `sfAssetsMaximum` (`SF_NUMBER`) — a cap on the total assets the vault can hold, represented as a 64-bit number.
- `sfMPTokenMetadata` (`SF_VL`) — a variable-length blob that becomes the metadata attached to the MPT issuance created for the vault's share token. This ties directly to the `createMPTIssuance` privilege listed in the class comment.
- `sfDomainID` (`SF_UINT256`) — a 256-bit identifier linking the vault to a permissioned domain, controlling who is allowed to deposit.
- `sfWithdrawalPolicy` (`SF_UINT8`) — an 8-bit enum encoding the vault's withdrawal rules (e.g., first-come-first-served vs. proportional).
- `sfData` (`SF_VL`) — an arbitrary application-level blob, useful for off-chain integrations.
- `sfScale` (`SF_UINT8`) — the decimal precision for the vault share token's MPT issuance, giving the issuer control over fractional representation.

## The `Optional<T>` Alias

Each optional field getter returns `protocol_autogen::Optional<T>` rather than bare `std::optional<T>`. The alias in `Utils.h` expands to `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` when `T` is a reference type, or plain `std::optional<T>` otherwise. This matters because some XRPL field accessors return references into the underlying `STObject` — wrapping a raw reference in `std::optional` would be undefined behaviour if the value were absent. The alias handles both cases uniformly, so the generated code works correctly regardless of whether the underlying field accessor returns by value or by reference.

## Privilege Model

The class comment records the privileges `createPseudoAcct | createMPTIssuance | mustModifyVault`. These are not enforced in this header — enforcement lives in the transaction-processing layer — but the annotations serve as a contract: `VaultCreate` is the sole transaction that may create the pseudo-account and MPT issuance associated with a vault. The `notDelegable` flag means this transaction cannot be submitted by a delegate account on behalf of another account, which is consistent with the privileged nature of vault creation.

## Relationship to Sibling Vault Transactions

The `VaultCreate` transaction represents only the creation step. The vault lifecycle continues through `VaultSet` (mutate configuration, type `ttVAULT_SET = 66`), `VaultDeposit`, `VaultWithdraw`, `VaultClawback`, and `VaultDelete`, each following the identical code-generation pattern in adjacent files. All share the same base classes and `Optional<T>` alias. `VaultSet` notably requires `sfVaultID` (the 256-bit identifier assigned at creation) as its required field, illustrating how the two-phase create/mutate model distributes fields across transaction types.
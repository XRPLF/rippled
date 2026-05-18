# `CredentialAccept.h` — Auto-generated Transaction Wrapper and Builder

## Role and Context

This file is part of the `protocol_autogen` subsystem — a set of auto-generated, strongly-typed C++ wrappers for every XRPL transaction type. It must not be edited by hand; its structure is identical across all transaction headers in `include/xrpl/protocol_autogen/transactions/`.

`CredentialAccept` represents transaction type `ttCREDENTIAL_ACCEPT` (ordinal 59), one of three transactions belonging to the `featureCredentials` amendment. The credential lifecycle on the ledger follows a two-step issuance model:

1. **`CredentialCreate` (type 58)** — An issuer account creates a credential record targeting a subject account, optionally including an expiration time and a URI.
2. **`CredentialAccept` (type 59)** — The subject account countersigns by submitting this transaction, acknowledging and activating the credential.
3. **`CredentialDelete` (type 60)** — Either party can remove the credential from the ledger.

`CredentialAccept` is therefore the subject's half of the handshake. The submitting account (`sfAccount`) is implicitly the subject; the transaction identifies the credential by naming its `sfIssuer` and `sfCredentialType`.

## Two-Class Design: Wrapper and Builder

The file defines two classes in `xrpl::transactions`:

**`CredentialAccept`** is an immutable, read-only view over a `std::shared_ptr<STTx const>`. It inherits the full complement of common field accessors from `TransactionBase` (`getAccount()`, `getFee()`, `getSequence()`, `getFlags()`, `getMemos()`, and others). On top of that base, it exposes two transaction-specific getters:

- `getIssuer()` — returns an `SF_ACCOUNT::type::value_type` (an `AccountID`) for the `sfIssuer` field, which is `soeREQUIRED`.
- `getCredentialType()` — returns an `SF_VL::type::value_type` (a variable-length `Blob`) for the `sfCredentialType` field, also `soeREQUIRED`.

Because both fields are required by the protocol schema, neither getter needs an optional return type or a presence check; accessing a missing required field through `STTx::at()` would indicate a malformed transaction that should never have passed validation in the first place.

The class stores a `static constexpr xrpl::TxType txType = ttCREDENTIAL_ACCEPT` for compile-time type identity, while the constructor enforces the same invariant at runtime: it calls `tx_->getTxnType()` and throws `std::runtime_error` if there is a mismatch. This guards against misuse of the wrapper — for instance, accidentally constructing a `CredentialAccept` from a `CredentialCreate` transaction object.

**`CredentialAcceptBuilder`** is the mutable counterpart, responsible for assembling a transaction before it is signed and finalized. It inherits from `TransactionBuilderBase<CredentialAcceptBuilder>`, which uses CRTP to ensure every inherited setter (e.g., `setFee()`, `setFlags()`, `setLastLedgerSequence()`) returns a `CredentialAcceptBuilder&` for clean method chaining without ugly casts in user code.

## Builder Construction Paths

`CredentialAcceptBuilder` offers two construction paths:

1. **From scratch**: The primary constructor accepts `account`, `issuer`, and `credentialType` as required arguments, with `sequence` and `fee` as `std::optional` parameters. It delegates immediately to `TransactionBuilderBase`'s constructor (which sets `sfTransactionType` and `sfAccount`), then calls `setIssuer()` and `setCredentialType()` to populate the credential-specific fields. Keeping `sequence` and `fee` optional is deliberate — test harnesses often want to auto-fill these values rather than specifying them ahead of time.

2. **From an existing `STTx`**: The second constructor takes a `std::shared_ptr<STTx const>`, validates the transaction type, and copies the entire `STTx` into `object_` via `object_ = *tx`. This path intentionally bypasses the `TransactionBuilderBase` constructor — it uses direct assignment rather than field-by-field setup — because the STTx already contains a fully formed set of fields and copying them individually would risk double-setting or missing subtleties in the internal `STObject` state.

## Signing and Finalization

`build(PublicKey const& publicKey, SecretKey const& secretKey)` is the terminal builder method. It delegates to `TransactionBuilderBase::sign()`, which serializes the mutable `STObject object_` with `HashPrefix::txSign`, computes the signature, and writes both `sfSigningPubKey` and `sfTxnSignature` back into the object. It then move-constructs an `STTx` from `object_` and wraps it in a `shared_ptr<STTx const>` before handing it to the `CredentialAccept` constructor. After `build()` is called, the resulting `CredentialAccept` is frozen and immutable.

## Setter Parameter Types

Both `setIssuer()` and `setCredentialType()` accept parameters typed as `std::decay_t<typename SF_ACCOUNT::type::value_type> const&` and `std::decay_t<typename SF_VL::type::value_type> const&` respectively. The `std::decay_t` strips any reference or cv-qualifier from the trait's value type. This is necessary because the getter return types (`SF_VL::type::value_type`) may themselves be references internally, and forming a `const&` to a reference type would collapse incorrectly without decay. Using decayed types keeps the setters safe regardless of how the underlying field type trait resolves.

## Relationship to Sibling Files

All files in `include/xrpl/protocol_autogen/transactions/` follow the same structural template. `CredentialCreate.h` and `CredentialDelete.h` are the closest siblings. Comparing them reveals the minimal surface area of `CredentialAccept`: it has no optional fields (unlike `CredentialCreate`, which offers `sfExpiration` and `sfURI`), and its required fields (`sfIssuer`, `sfCredentialType`) are the minimal identifiers needed to locate the specific credential being accepted on the ledger. The transaction is also marked delegable, meaning a `sfDelegate` account can submit it on behalf of the subject using XRPL's delegation mechanism inherited through `TransactionBase`.
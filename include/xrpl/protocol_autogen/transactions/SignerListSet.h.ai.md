# `SignerListSet.h` — Auto-Generated Transaction Wrapper for `ttSIGNER_LIST_SET`

## Role in the System

This file is part of the `protocol_autogen` subsystem — a layer of generated C++ headers that give every XRPL transaction type a strongly-typed, compile-time-checked API. It defines two classes: `SignerListSet`, an immutable read-only wrapper over an `STTx` carrying transaction type 12 (`ttSIGNER_LIST_SET`), and `SignerListSetBuilder`, the fluent construction counterpart.

At the protocol level, the `SignerListSet` transaction is how an XRPL account establishes or removes its multi-signature signer list. Sending this transaction with a populated `sfSignerEntries` array installs a set of cosigners; sending it with `sfSignerQuorum` set to zero and `sfSignerEntries` absent tears the list down. The two-field design (a quorum threshold and the list of signers) mirrors the ledger object structure of the `SignerList` ledger entry.

## The Two-Class Pattern

The split between `SignerListSet` (wrapper) and `SignerListSetBuilder` (builder) reflects a deliberate immutability contract. `SignerListSet` wraps a `std::shared_ptr<STTx const>` — the `const` is baked in at the pointer type — so no mutation can happen through the accessor layer. Callers that want to construct a new transaction use `SignerListSetBuilder`, which holds a mutable `STObject` internally and only promotes it to an immutable `STTx` at the moment `build()` is called.

Both classes enforce transaction type correctness at construction time. The `SignerListSet` constructor calls `tx_->getTxnType()` and throws `std::runtime_error` if the result is not `ttSIGNER_LIST_SET`. `SignerListSetBuilder`'s second constructor (from an existing `STTx`) performs the same check. This makes type mismatches fail loudly at the wrapper boundary rather than silently producing incorrect field reads later.

## Field Accessors

`SignerListSet` exposes exactly the fields defined for this transaction type beyond the universal fields provided by `TransactionBase`:

- `getSignerQuorum()` returns the raw `uint32_t` value of `sfSignerQuorum` directly — no `std::optional` wrapping because `sfSignerQuorum` is `soeREQUIRED`. If the underlying `STTx` is well-formed, this call never fails.

- `getSignerEntries()` returns `std::optional<std::reference_wrapper<STArray const>>`. The `std::reference_wrapper` avoids copying the potentially large array while still returning it through an optional. The companion predicate `hasSignerEntries()` lets callers avoid the optional dereference when they only need a presence check.

All getters are annotated `[[nodiscard]]`, which ensures callers cannot accidentally ignore a return value that might be `std::nullopt` — a subtle but important correctness nudge for optional fields.

## Builder and CRTP Chain

`SignerListSetBuilder` inherits from `TransactionBuilderBase<SignerListSetBuilder>`. The CRTP template parameter makes every common setter in `TransactionBuilderBase` (for fields like `sfFee`, `sfSequence`, `sfFlags`, `sfLastLedgerSequence`, etc.) return `SignerListSetBuilder&` rather than the base class reference, so method chains remain fluent throughout without any casting on the caller side.

The constructor enforces that `sfSignerQuorum` must be supplied at build time — it is a required field and is wired up immediately via `setSignerQuorum()` in the constructor body. Optional `sequence` and `fee` arguments are forwarded to the base class, which applies them only if they have values, avoiding unintentional injection of unset fields into the `STObject`.

A notable design note in `TransactionBuilderBase` is the decision *not* to call `object_.set(soTemplate)`. The comment explains that setting a template would insert `soeDEFAULT` placeholder fields, which would then make the `STTx` constructor's `applyTemplate()` call throw "may not be explicitly set to default." Keeping `object_` as a free, untemplatized `STObject` sidesteps that constraint entirely.

## The `build()` Method and Signing

`build(PublicKey, SecretKey)` finalises the transaction in two steps. First it calls `sign()` (inherited from `TransactionBuilderBase`), which serialises the object without signing fields, prepends the `HashPrefix::txSign` prefix, and computes the signature. Then it constructs an `STTx` from the moved `STObject` and wraps it in a `SignerListSet` value. The move into `STTx` is intentional: after `build()` the builder's internal `object_` is in a valid-but-unspecified state, making the builder non-reusable — a design that prevents double-signing mistakes.

The second builder constructor (taking `std::shared_ptr<STTx const>`) allows an already-signed transaction to be re-opened into a builder. This supports scenarios like re-signing with a different key or applying additional fields, after which `build()` produces a fresh `STTx` with an updated signature.
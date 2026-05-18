# `SetRegularKey.cpp` — Regular Key Assignment Transactor

`SetRegularKey` implements the XRPL transaction type that allows an account owner to assign an alternative signing key, called the "regular key," to their account. Once set, the regular key's private half can sign any transaction on behalf of the account in lieu of the master key — a critical building block for key rotation, cold-wallet architectures, and custodial setups. This file contains the three override points the transactor framework calls in sequence: `calculateBaseFee`, `preflight`, and `doApply`.

## Class Design and Inheritance

`SetRegularKey` extends `Transactor` and declares `ConsequencesFactory{Blocker}`. That tag tells the transaction queue that any queued `SetRegularKey` blocks all subsequent transactions from the same account from being queued behind it. This is correct: because the regular key controls which credentials are valid for future transactions, allowing later transactions to queue before the key change resolves would risk committing those transactions under the wrong authority context.

The `preflight` and `calculateBaseFee` methods are `static`, not virtual — the `Transactor` framework exploits name hiding via the `invokePreflight<T>` template to achieve compile-time polymorphism without the overhead or ceremony of vtable dispatch. Only `doApply` is a proper virtual override, called after the framework has already verified signatures and deducted fees.

## Fee Waiver: The `lsfPasswordSpent` Mechanism

`calculateBaseFee` contains the most nuanced logic in the file. An account that has never used this facility may set its regular key for free — the fee is waived to zero — under two conditions: the transaction must be signed with the account's master key (verified by deriving an `AccountID` from the signing public key and comparing it to `sfAccount`), and the `lsfPasswordSpent` flag must be clear on the account's ledger entry.

The name "PasswordSpent" is a historical artifact from an early XRPL concept in which accounts were provisioned with a one-time password granting free regular-key setup. The flag persists in the protocol as the mechanism that tracks whether this free opportunity has already been consumed. `doApply` sets `lsfPasswordSpent` whenever the actual fee paid was less than the minimum required fee — i.e., when the free-use path was taken — so subsequent attempts are charged normally.

The validation in `calculateBaseFee` is defensive: `publicKeyType(makeSlice(spk))` is checked first to confirm the signing key is a recognized key type before `calcAccountID` is called on it, avoiding a potential crash on malformed or empty signing keys that might appear in multi-sig or inner-batch contexts.

## Preflight: Preventing Self-Referential Keys

`preflight` has a single, targeted check: it rejects the transaction with `temBAD_REGKEY` if `sfRegularKey` is present and equals `sfAccount`. Setting the regular key to the account's own address would be semantically circular and could interfere with key-removal logic that distinguishes between "no regular key" and "regular key is the account itself." The absence of `sfRegularKey` in the transaction is not an error at this stage — it signals intent to remove an existing regular key, handled downstream in `doApply`.

No amendment gating or flag checks are needed in `preflight` for this transaction type; the base `invokePreflight<T>` template handles `preflight1`, signature verification, and `preflight2` around this call.

## `doApply`: Set or Remove with Safety Guard

`doApply` branches on whether `sfRegularKey` is present in the transaction:

**Set path**: The field value is written directly to the account's `AccountRoot` ledger entry via `setAccountID(sfRegularKey, ...)`. No additional validation of the target address is required here — the address is just an opaque 160-bit identifier that the signing subsystem will use during future signature checks.

**Remove path**: Before calling `makeFieldAbsent(sfRegularKey)` to strip the field from the ledger entry, the code enforces a critical account-access invariant: if the master key is disabled (`lsfDisableMaster` flag set) and no multi-sig signer list exists (`view().peek(keylet::signers(account_))` returns null), then removing the regular key would leave the account with no valid signing method at all — permanently locked out. This is rejected with `tecNO_ALTERNATIVE_KEY`. The check requires *both* conditions because either a live master key or a signer list is sufficient to retain access.

This guard prevents the ledger from containing accounts that are permanently inaccessible. Since `tecNO_ALTERNATIVE_KEY` is a `tec`-class result, the transaction still claims a fee (the signer paid to learn this) but makes no state changes beyond fee deduction.

The final `ctx_.view().update(sle)` persists all mutations to the mutable view, which will be committed to the ledger if the transaction fully succeeds.

## Error Taxonomy

| Error | Phase | Condition |
|---|---|---|
| `temBAD_REGKEY` | `preflight` | `sfRegularKey` == `sfAccount` (self-assignment) |
| `tefINTERNAL` | `doApply` | Account root SLE missing (should never occur in production; marked `LCOV_EXCL_LINE`) |
| `tecNO_ALTERNATIVE_KEY` | `doApply` | Removing regular key when master is disabled and no signer list exists |
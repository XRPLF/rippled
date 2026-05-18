# `DepositPreauth.h` — Deposit Pre-Authorization Transactor

## Role in the System

`DepositPreauth` implements the transaction handler for the XRPL `DepositPreauth` transaction type. This transaction allows an account that has enabled the `lsfDepositAuth` flag (deposit authorization mode) to explicitly whitelist — or remove from the whitelist — other accounts or credential-bearing parties that may send it payments. Without such a preauthorization, a payment to a deposit-auth-enabled account would fail. This header declares the class interface; the full implementation lives in `DepositPreauth.cpp`.

## Class Structure and Inheritance

`DepositPreauth` inherits from `Transactor`, the base class for all XRPL transaction processors. Every transactor follows a three-phase pipeline: stateless validation (`preflight`), ledger-state checking (`preclaim`), and mutation (`doApply`). These phases are wired together by the framework via `invokePreflight<T>` and `invoke_preclaim<T>`, which use compile-time template dispatch (name hiding, not virtual dispatch) to call the correct per-class static methods without vtable overhead.

```cpp
static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};
```

Setting `ConsequencesFactory` to `Normal` tells the framework that this transaction does not block other transactions in a fee queue (as opposed to `Blocker`, used by account-mutating transactions).

## The Four Operations

Despite being a single transaction type, `DepositPreauth` supports four distinct operations controlled by which of four mutually exclusive transaction fields is present:

- **`sfAuthorize`** — preauthorizes a specific account by `AccountID`.
- **`sfUnauthorize`** — revokes a specific account's preauthorization.
- **`sfAuthorizeCredentials`** — preauthorizes any holder of a specific set of verifiable credentials (gated on the `featureCredentials` amendment).
- **`sfUnauthorizeCredentials`** — revokes a credential-set preauthorization.

## `checkExtraFeatures`

This optional override gates the credential-based operations on the `featureCredentials` amendment. If either `sfAuthorizeCredentials` or `sfUnauthorizeCredentials` is present in the transaction but the amendment is not active on the ledger, the method returns `false`, which causes `invokePreflight` to emit `temDISABLED`. This is the correct place for amendment checks — not inside `preflight` itself — as the base `Transactor` contract dictates.

## `preflight` — Stateless Validation

`preflight` enforces that exactly one of the four operation fields is present. The check is explicit: it counts how many of the four fields appear and returns `temMALFORMED` if the count is not exactly one. This guards against malformed transactions that omit all fields or combine incompatible ones.

For account-based operations, it validates that the target `AccountID` is non-zero and that an account is not attempting to preauthorize itself (`temCANNOT_PREAUTH_SELF`). For credential-based operations, it delegates to `credentials::checkArray`, which validates the credential array size (bounded by `maxCredentialsArraySize`) and internal structure.

## `preclaim` — Ledger-State Checks

`preclaim` operates on a read-only ledger view. It verifies:

- For `sfAuthorize`: the target account exists (`tecNO_TARGET`) and no duplicate `DepositPreauth` ledger entry already exists for this pair (`tecDUPLICATE`).
- For `sfUnauthorize`: the preauth entry being removed actually exists (`tecNO_ENTRY`).
- For `sfAuthorizeCredentials`: each credential issuer exists as a live account (`tecNO_ISSUER`), and no duplicate entry exists. The credentials are sorted canonically before computing the ledger key.
- For `sfUnauthorizeCredentials`: the canonical credential-set entry exists.

The credential duplicate check is interesting: `preclaim` builds a sorted `std::set<std::pair<AccountID, Slice>>` from the incoming credential array before calling `keylet::depositPreauth`. This normalization is critical — the ledger always stores credentials in a canonical order, so the key computation must use sorted input regardless of submission order.

## `doApply` — Ledger Mutation

`doApply` mirrors the four-way branch logic of `preclaim`, but mutates ledger state.

For authorization (both account-based and credential-based), `doApply` first checks the owner reserve. The check uses `preFeeBalance_` (the account's XRP balance *before* fees were deducted), deliberately allowing accounts to dip into their base reserve to cover the transaction fee, but not to fund a new owned object beyond their current balance. This is consistent with how owner reserves work across all ledger object types.

On success, the method creates a `SLE` (Serialized Ledger Entry) for the preauth, inserts it into the ledger, adds it to the account's owner directory via `dirInsert`, stores the resulting directory page number in `sfOwnerNode`, and increments the owner count with `adjustOwnerCount`. This reserve bookkeeping is why the object has a storage cost.

For credential-based entries, the credentials are re-sorted before being stored in the `SLE`, ensuring the canonical representation on the ledger matches the key regardless of the order they were specified in the transaction.

For de-authorization, the method delegates to the static `removeFromLedger`.

## `removeFromLedger` — Shared Cleanup Interface

```cpp
static TER
removeFromLedger(ApplyView& view, uint256 const& delIndex, beast::Journal j);
```

This static method accepts an `ApplyView` directly rather than using `this`, allowing it to be called from outside the transactor without instantiating a `DepositPreauth` object. As the comment in the header explicitly notes, this interface is "used by `AccountDelete`". When an account is deleted from the ledger, all owned objects — including preauth entries — must be cleaned up, and `AccountDelete` calls this method for each one.

The method locates the `SLE`, extracts the owner `AccountID` and directory page from the object itself, removes it from the owner directory via `dirRemove`, decrements the owner count, and erases the entry. The `tefBAD_LEDGER` return for a failed directory removal is annotated `LCOV_EXCL_START`, reflecting that this case should be unreachable given correct ledger invariants — a defensive guard against internal corruption rather than expected user-triggered failures.

## Relationship to the Credentials Amendment

The credential-authorization paths (`sfAuthorizeCredentials` / `sfUnauthorizeCredentials`) represent a newer extension to the `DepositPreauth` mechanism. Rather than whitelisting a specific sender account, they whitelist any party that can present a verified credential from a given set of issuers. This enables more flexible access control (e.g., "allow anyone with a KYC credential from issuer X") without requiring the authorizing account to know all potential senders in advance. The amendment gating in `checkExtraFeatures` ensures that this feature activates atomically across all validators when the amendment passes.
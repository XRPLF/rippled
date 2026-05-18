Now I have enough context to write the documentation. Let me produce it:

---

## `src/libxrpl/tx/Transactor.cpp`

### Role in the System

`Transactor` is the abstract base class for every transaction type in the XRP Ledger. Every `Payment`, `OfferCreate`, `AccountSet`, `NFTokenMint`, and so on ultimately inherits from it. This file implements the **shared infrastructure** that all transactions use: layered preflight validation, fee calculation and payment, sequence/ticket consumption, signature verification, and the final apply-or-reset decision loop. The concrete transaction type only has to supply `doApply()` and a few static hooks; everything else flows through the machinery here.

---

### The Three-Phase Transaction Pipeline

Transaction processing is split across three phases, each with its own context struct:

1. **Preflight** (`PreflightContext`) — stateless, read-only, runs against the raw `STTx`. The ledger view is not consulted. Errors here produce `tem*` or `tel*` codes.  
2. **Preclaim** (`PreclaimContext`) — read-only against a ledger view snapshot. Checks whether the account exists, the fee is sufficient, and the signature is valid. Errors here produce `tef*`/`ter*` codes.  
3. **Apply** (`ApplyContext`) — mutable. The transaction actually changes ledger state.

The gateway into phase 3 is `Transactor::operator()()`, which receives the preclaim result stored in `ctx_.preclaimResult` and proceeds to call `apply()` only when that result is `tesSUCCESS`.

---

### Layered Preflight: `preflight0` → `preflight1` → `T::preflight()` → `preflight2`

The template function `invokePreflight<T>()` in the header orchestrates all preflight checks for a concrete transactor type `T`:

```cpp
preflight1(ctx, T::getFlagsMask(ctx))  →  T::preflight(ctx)  →  preflight2(ctx)  →  T::preflightSigValidated(ctx)
```

**`preflight0`** (free function, called from `preflight1`) is the most primitive gate. It enforces three invariants that apply to every transaction regardless of type:
- Pseudo-transactions cannot carry the `tfInnerBatchTxn` flag.
- The `sfNetworkID` field must be absent on legacy networks (ID ≤ 1024) and present and matching on newer ones. This prevents replay across networks.
- The transaction ID cannot be the zero hash.
- No bits outside the transaction's allowed flag mask may be set.

**`preflight1`** adds account-level sanity: the `sfAccount` field must be a non-zero ID, the `sfFee` must be a non-negative native (XRP) amount, and the signing public key format must be valid. It also enforces that tickets and `sfAccountTxnID` are mutually exclusive — an important ordering-constraint invariant documented inline.

**`preflight2`** handles simulation mode (`tapDRY_RUN`) and the cryptographic signature validity check via the hash router's cached result (`checkValidity`). For `tfInnerBatchTxn` transactions the signature check is skipped entirely here because the outer batch transaction already provides authorization.

The design ensures a derived class's `preflight()` runs *between* the framework's own `preflight1` and `preflight2` checks, never bypassing them. The header comment is explicit: "Do not try to call preflight1 or preflight2 directly."

---

### Fee Calculation and the `reset()` Safety Net

`calculateBaseFee()` returns `baseFee * (1 + multiSigCount)` — each additional signer costs one extra base fee. `minimumFee()` then scales that by the current server load via `scaleFeeLoad`.

`checkFee()` does a nuanced balance check: it uses a static `ReadView` snapshot, so multiple in-flight transactions from the same account may each independently pass the balance check. The code documents this optimism explicitly:

> *"Because preclaim evaluates against a static readview, it does not reflect fee deductions from other transactions paid by the same account within the current ledger… The fee shortfall will be handled by the Transactor::reset mechanism."*

`reset()` is the correction path. It discards all ledger mutations via `ctx_.discard()`, then re-deducts only the fee, clamping it to the actual remaining balance if necessary:

```cpp
if (fee > balance)
    fee = balance;
```

This ensures that a failing transaction can still claim its fee even when the account is over-committed — a core ledger invariant.

---

### Sequence and Ticket Consumption

`checkSeqProxy()` handles both classic sequence numbers and the newer Ticket mechanism via `SeqProxy`. For sequence-based transactions it enforces strict monotonic ordering. For ticket-based transactions it checks that the ticket's numeric value is below the current account sequence (to rule out tickets that haven't been created yet), and that the ticket SLE actually exists in the ledger.

`consumeSeqProxy()` advances the account sequence for normal transactions, or calls `ticketDelete()` for ticket transactions. `ticketDelete()` performs three coordinated ledger mutations: removes the ticket SLE, removes it from the owner directory, decrements `sfTicketCount` (removing the field entirely when it reaches zero), and adjusts the owner reserve count.

---

### Signature Verification

`checkSign()` handles four distinct authorization paths:

1. **Batch inner transactions**: asserts that no key, signature, or signer list is present — authorization was already checked on the outer batch.
2. **Simulation (`tapDRY_RUN`)**: if no signing key and no signers are present, validation is skipped entirely.
3. **Multi-signature** (`sfSigners` present): delegates to `checkMultiSign()`.
4. **Single signature**: derives the signing account from the public key and calls `checkSingleSign()`.

`checkSingleSign()` applies three precedence rules: regular key first, then enabled master key, then `tefMASTER_DISABLED` for a disabled master key.

`checkMultiSign()` performs a linear merge of the sorted `sfSigners` array against the account's sorted `SignerEntry` list, validating each entry against the phantom / master key / regular key rules documented inline. It terminates with `tefBAD_QUORUM` if the weight sum falls short of `sfSignerQuorum`.

`checkBatchSign()` extends multi-sign to the outer batch transaction's `sfBatchSigners` array, permitting unsigned accounts to appear when their master key is the signer — allowing a batch to fund an account creation as part of the same batch.

---

### The `operator()()` Apply Loop

The entry point for phase 3 is `operator()()`. It:

1. Installs RAII guards (`NumberSO`, `CurrentTransactionRulesGuard`) that adapt numeric arithmetic rules based on enabled amendments.
2. In debug builds, serializes and re-parses the transaction to detect serdes mismatches.
3. Calls `apply()` if preclaim succeeded; `apply()` runs `preCompute()`, captures `preFeeBalance_`, calls `consumeSeqProxy()`, calls `payFee()`, updates `sfAccountTxnID` if present, then calls the virtual `doApply()`.
4. After the call, enforces `tecOVERSIZE` if the metadata delta exceeds `oversizeMetaDataCap`.
5. For `tapFAIL_HARD` with a `tec` result, the context is discarded and no state changes occur.
6. For special `tec` codes (`tecOVERSIZE`, `tecKILLED`, `tecINCOMPLETE`, `tecEXPIRED`), it visits the context diff to collect deleted objects, calls `reset()` to discard the mutation, then replays targeted cleanup helpers: `removeUnfundedOffers`, `removeExpiredNFTokenOffers`, `removeDeletedTrustLines`, `removeDeletedMPTs`, `removeExpiredCredentials`. This mechanism lets certain failure codes still produce useful side-effects (offer cleanup) without applying the failed transaction's primary effects.
7. Passes applied results through `ctx_.checkInvariants()`. If invariant checking itself fails (`tecINVARIANT_FAILED`), the context is reset again and the invariants re-checked on the fee-only claim. If invariants still fail, the transaction is not applied at all.
8. For dry-run mode, sets `applied = false` unconditionally after all the above — the state changes are computed but never committed.
9. Returns `{result, applied, metadata}` as an `ApplyResult`.

---

### Permission Delegation

`checkPermission()` validates the optional `sfDelegate` field against a `DelegateObject` ledger entry (`keylet::delegate`). If the object is absent, `terNO_DELEGATE_PERMISSION` is returned. If present, `checkTxPermission()` from `DelegateHelpers` evaluates whether the delegate has the right to submit this specific transaction type.

---

### Pseudo-Account Guard

When the `featureLendingProtocol` amendment is enabled, `checkSign()` rejects any signing attempt from a pseudo-account with `tefBAD_AUTH`. Pseudo-accounts are protocol-internal constructs that must never be able to sign user-facing transactions; this guard is defensive against ledger corruption or future code paths that might inadvertently authorize them.

---

### Debug and Diagnostics

`trapTransaction()` exists solely to provide a named breakpoint location for replaying specific transactions during debugging. It logs the hash and returns, doing nothing else. The `LCOV_EXCL_LINE` and `LCOV_EXCL_START/STOP` markers scattered through the file indicate branches that are structurally unreachable in normal ledger operation — defensive guards against ledger corruption — and are excluded from coverage requirements accordingly.
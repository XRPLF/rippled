# `SignerListSet.h` — Multi-Signature Signer List Transactor

`SignerListSet` implements the XRPL transaction type that manages an account's multi-signature signer list. It allows an account holder to create or replace a signer list (establishing who can co-sign future transactions and with what voting weight), or to destroy the list entirely, reverting the account to single-key signing. The class lives in the account-management transactor group alongside `AccountDelete`, `AccountSet`, and `SetRegularKey`.

## Inheritance and Role in the Transactor Framework

`SignerListSet` extends `Transactor`, the abstract base for all XRPL transaction processors. The base class handles fee payment, sequence number consumption, and signature verification; derived classes fill in `preflight()` (stateless validation), `preCompute()` (state extraction before applying), and `doApply()` (ledger mutation). `SignerListSet` follows this three-phase contract faithfully.

`ConsequencesFactory` is set to `Blocker`, meaning a queued `SignerListSet` transaction blocks any later transaction from the same account from being applied until this one is either applied or dropped. This is conservative but correct — changing who can sign an account has security implications that justify serializing processing.

## Cached Computation via `preCompute()`

Rather than re-parsing the transaction twice, the class caches three values as private members: `do_` (an `Operation` enum), `quorum_`, and `signers_`. These are populated by `preCompute()` before `doApply()` runs. The `Operation` enum has three values: `unknown`, `set`, and `destroy`. The choice of which operation to perform is determined entirely by the transaction's `sfSignerQuorum` and `sfSignerEntries` fields:

- **Non-zero quorum + `sfSignerEntries` present** → `set` (create or replace)
- **Zero quorum + no `sfSignerEntries`** → `destroy`
- Any other combination → `unknown`, which maps to `temMALFORMED`

This dispatch logic lives in the private static `determineOperation()`, which is called from both `preflight()` and `preCompute()`. Calling it in `preflight()` allows stateless validation to reject malformed transactions before any ledger access occurs; calling it again in `preCompute()` restores the parsed data in instance state for use in `doApply()`. The assertion in `preCompute()` confirms that by this stage the operation must be well-formed — validation has already passed.

## Validation in `preflight()`

The public static `preflight()` delegates to `determineOperation()` and, for `set` operations, to `validateQuorumAndSignerEntries()`. This second validator enforces several invariants:

1. The signer count must fall within `[STTx::minMultiSigners, STTx::maxMultiSigners]` (currently 1–32).
2. No duplicate signers. Since `determineOperation()` sorts the deserialized list, duplicates are found in O(n) with `std::adjacent_find`.
3. No signer may reference the signing account itself (`temBAD_SIGNER`), preventing trivial bypass of the quorum mechanism.
4. Every signer must have a positive weight; a zero-weight signer contributes nothing and is disallowed (`temBAD_WEIGHT`).
5. The sum of all signer weights must be at least equal to the quorum (`temBAD_QUORUM`). This is a reachability check — a quorum that no combination of signers can reach would permanently lock the account.

The validator deliberately does **not** verify that signer accounts exist in the ledger, because XRPL permits "phantom accounts" as signers. This is a documented design choice noted in the implementation.

## Applying the Operation

`doApply()` simply switches on the cached `do_` field and delegates to `replaceSignerList()` or `destroySignerList()`.

**`replaceSignerList()`** handles both creation and replacement with a single code path: it first removes any existing signer list (via the internal `removeSignersFromLedger()` helper), then checks the updated reserve, and finally inserts a fresh `ltSIGNER_LIST` ledger entry. Removing the old list before checking the new reserve is intentional — the deletion may reduce the owner count, potentially making the reserve check pass when it otherwise would not. The new list always sets `lsfOneOwnerCount`, indicating that the `MultiSignReserve` amendment applies and the list occupies exactly one owner-count unit regardless of how many signers it contains.

**`destroySignerList()`** has a critical safety check before removal: if the account has the master key disabled (`lsfDisableMaster`) and has no `sfRegularKey` set, destroying the signer list would leave the account with no signing mechanism, effectively bricking it. The operation is rejected with `tecNO_ALTERNATIVE_KEY` in that case.

## Owner Count and the `MultiSignReserve` Amendment

Owner-count accounting for signer lists has an amendment-aware dual mode. Before `MultiSignReserve`, the reserve cost was `2 + N` (two base units plus one per signer). After the amendment, new lists always pay exactly 1 unit (`lsfOneOwnerCount`). The removal path must handle both, because old ledger objects may pre-date the amendment. The `signerCountBasedOwnerCountDelta()` free function computes the legacy adjustment; the `removeSignersFromLedger()` helper inspects the `lsfOneOwnerCount` flag on the existing SLE to choose the correct calculation.

## Cross-Transactor Interface: `removeFromLedger()`

The static public `removeFromLedger()` method exposes signer list cleanup as a well-defined interface for `AccountDelete`. When an account is deleted, all its owned ledger objects must be removed first, and the `AccountDelete` transactor calls into `SignerListSet` rather than duplicating the removal logic. The signature takes an `ApplyView&` and `ServiceRegistry&` directly rather than going through an instance, keeping it usable from a different transactor's `doApply()`.

## `writeSignersToSLE()`

This helper serializes the in-memory signer list into the ledger SLE. It conditionally sets `sfOwner` only when the `fixIncludeKeyletFields` amendment is active, and writes `sfWalletLocator` (the signer tag) only when a tag is actually present. A comment explicitly calls this out as defensive: the optional write ensures no spurious tag field is ever serialized into the ledger even if the `tag` member is default-initialized.

## Flags Mask Handling

`getFlagsMask()` returns `tfUniversalMask` if the `fixInvalidTxFlags` amendment is enabled, otherwise it returns `0` (which in the framework means "allow any flags"). This backward-compatible choice lets the validator reject unknown flags on updated networks while preserving legacy behavior on older rulesets.
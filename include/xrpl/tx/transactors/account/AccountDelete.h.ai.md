# `AccountDelete.h` — AccountDelete Transaction Handler

## Role in the System

`AccountDelete.h` declares the `AccountDelete` transactor, the class responsible for processing the XRPL `AccountDelete` transaction type. It fits into the broader transaction processing framework by extending the `Transactor` base class and overriding the standard preflight/preclaim/`doApply` pipeline. AccountDelete is one of the most structurally complex transactors in the ledger because it must safely dismantle an account — wiping out all its owned ledger objects, transferring its remaining XRP balance, and then erasing the account root entry itself.

## Class Declaration

```cpp
class AccountDelete : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};
    ...
};
```

The `ConsequencesFactory{Blocker}` declaration is significant. The `Blocker` type tells the transaction queuing system that if an `AccountDelete` is pending for an account, no other transaction from that same account should be queued behind it. This is a safety invariant: because account deletion clears sequence numbers and owner objects, allowing subsequent transactions to queue up behind a pending delete would create undefined or dangerous state.

## Processing Pipeline

### `checkExtraFeatures`

Called by `invokePreflight<T>` before the field-level checks, this method gates the entire transaction on a single condition: if the transaction includes the `sfCredentialIDs` field, the `featureCredentials` amendment must be enabled. Returning `false` causes `invokePreflight` to return `temDISABLED`. This is the preferred pattern for amendment gating in the new framework — implemented here instead of inside `preflight()` proper.

### `preflight`

The `preflight` implementation performs stateless early rejection. It rejects the trivial self-send case (`sfAccount == sfDestination` → `temDST_IS_SRC`) and delegates credential field format validation to `credentials::checkFields`. No ledger state is consulted here — `preflight` only has access to the raw transaction fields and active rule set.

### `calculateBaseFee`

```cpp
static XRPAmount calculateBaseFee(ReadView const& view, STTx const& tx);
```

AccountDelete deliberately overrides the base class fee calculation. Instead of the standard reference fee, it calls `calculateOwnerReserveFee()`, which returns one owner reserve unit. This effectively prices account deletion at the cost of one reserve increment, making the operation economically meaningful and discouraging spam. The higher fee is also a design signal: account deletion is intentional and consequential, not routine.

### `preclaim`

`preclaim` is the heavyweight guard stage. It performs all stateful checks against a read-only view of the ledger:

1. **Destination validation** — the destination account must exist (`tecNO_DST`), and if it requires a destination tag, the transaction must supply one.
2. **DepositAuth check** — if the destination has `lsfDepositAuth` set and no credentials are provided, the sender must have a pre-authorized deposit entry. If credentials are provided, this check is intentionally deferred to `doApply` so that expired credentials are caught at apply-time rather than claim-time.
3. **NFToken obligations** — two separate checks prevent deletion if the account has outstanding NFTs. First, if the number of minted NFTokens doesn't match burned tokens (the account is still an active issuer with live NFTs), it returns `tecHAS_OBLIGATIONS`. Second, the code checks for any owned NFToken page entries.
4. **Sequence delta guard** — the account's own sequence number must be at least 256 below the current ledger sequence. This prevents replay attacks: if an account is deleted and then recreated with the same address, any old signed transactions (with sequence numbers close to the deletion point) would otherwise become valid again. The 256-ledger window provides a safe buffer since transactions expire within that range.
5. **NFToken sequence guard** — a more subtle variant of the same anti-replay concern: `FirstNFTokenSequence + MintedNFTokens + 255` must not exceed the current ledger. Without this, an authorized minter could have minted NFTs on behalf of the issuer without advancing the issuer's own sequence, allowing duplicate NFTokenIDs after account recreation.
6. **Owner directory sweep** — the method enumerates every entry in the account's owner directory and calls `nonObligationDeleter()` on each type. If any entry has no registered deleter (e.g., a trust line with a non-zero balance, or an escrow), the entire transaction fails with `tecHAS_OBLIGATIONS`. If the directory has more than `maxDeletableDirEntries` entries, it returns `tefTOO_BIG` — avoiding unbounded work in a single transaction.

The `nonObligationDeleter()` helper (file-local) is a dispatch table implemented as a `switch` over `LedgerEntryType`, returning a `DeleterFuncPtr` for each cleanable type: offers, signer lists, tickets, deposit preauth entries, NFToken offers, DIDs, oracles, credentials, and delegate objects. Any unrecognized type returns `nullptr`, which signals an obligation that blocks deletion.

### `doApply`

`doApply` is the mutation stage. It assumes `preclaim` has already validated the ledger state and proceeds to:

1. **Credential-based DepositAuth** — if `sfCredentialIDs` are present, it now calls `verifyDepositPreauth` to check both authorization and credential expiry. This is the deferred check skipped in `preclaim`.
2. **Owner directory cleanup** — calls `cleanupOnAccountDelete()` with a lambda that invokes the appropriate `nonObligationDeleter` for each remaining directory entry. The lambda returns `SkipEntry::No` uniformly, meaning no entries are skipped during iteration.
3. **XRP transfer** — after all owned objects are removed, the remaining XRP balance is transferred to the destination account directly by adjusting both SLE balances and calling `ctx_.deliver()`.
4. **Directory deletion** — the (now presumably empty) owner directory root node is erased. A non-empty directory at this point is treated as a ledger integrity error.
5. **Password flag reset** — if XRP was transferred and the destination had spent its free password-change credit (`lsfPasswordSpent`), the flag is cleared. This is a minor courtesy side-effect of receiving XRP.
6. **Account erasure** — `view().erase(src)` removes the account root SLE from the ledger.

## Design Relationships

`AccountDelete.h` is tightly coupled to the broader transactor ecosystem. The `Transactor` base (defined in `Transactor.h`) provides the `invokePreflight<T>` template that orchestrates `checkExtraFeatures` → `preflight1` → `preflight` → `preflight2`, as well as the `ticketDelete()` helper that `AccountDelete` delegates to for ticket removal. The implementation file pulls in helpers from `SignerListSet`, `DIDDelete`, `OracleDelete`, `DepositPreauth`, and credential utilities — each contributing a type-specific deletion routine that fits the `DeleterFuncPtr` signature. This delegation model keeps `AccountDelete` free of per-object implementation details while remaining the single authoritative orchestrator for account teardown.
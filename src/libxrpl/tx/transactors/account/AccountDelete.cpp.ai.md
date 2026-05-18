# AccountDelete.cpp

`AccountDelete.cpp` implements the XRPL `AccountDelete` transaction, the only mechanism by which an account can be permanently erased from the ledger. Executing this transaction removes the source account's SLE, cleans up every deletable object it owns, and sweeps its remaining XRP balance to a specified destination address. The implementation follows the standard four-phase transactor lifecycle: `checkExtraFeatures` → `preflight` → `preclaim` → `doApply`.

## Fee Design

`calculateBaseFee()` departs from the standard base-fee formula by charging one full owner reserve increment instead. This is intentional: account deletion is a privileged, irreversible operation that must be economically significant enough to prevent spam while remaining cheaper than simply holding reserves indefinitely. The reserve-sized fee is computed by the shared `calculateOwnerReserveFee()` helper inherited from `Transactor`.

## Preflight and Feature Gating

`checkExtraFeatures()` enforces that `sfCredentialIDs` may only appear in the transaction when the `featureCredentials` amendment is active. This is the correct place for feature-flag gating because it happens before ledger state is consulted.

`preflight()` performs two fast, stateless checks: it rejects `temDST_IS_SRC` when source and destination are the same account (since there is nobody left to receive the XRP), and delegates credential field format validation to `credentials::checkFields`.

## Preclaim: Blocking Conditions

`preclaim()` is the most consequential method. It must return `tesSUCCESS` only when every condition required for a safe deletion is met, because an account that fails here causes no ledger mutation and no fee burn.

**Destination checks.** The destination account must exist (`tecNO_DST`). If it has `lsfRequireDestTag` set, a destination tag must be present. If it has `lsfDepositAuth` set and no `sfCredentialIDs` is supplied, the source must have a `DepositPreauth` object authorising it. The deposit-auth check is intentionally skipped here when credentials are present — expired-credential removal requires a mutable `ApplyView`, which is only available in `doApply`.

**NFToken obligations.** XRPL treats minted NFTs as ledger obligations even after transfer. If `sfMintedNFTokens != sfBurnedNFTokens`, the account is still an issuer of outstanding tokens and cannot be deleted (`tecHAS_OBLIGATIONS`). Separately, if the account holds any NFTokens in its NFToken pages, deletion is also blocked. A second NFT-related check guards against duplicate token IDs after account resurrection: `FirstNFTokenSequence + MintedNFTokens + 255` must not exceed the current ledger sequence, because authorized minting can create NFTs without advancing the issuer's account sequence.

**Sequence freshness check.** An account's sequence number must be at least 256 below the current ledger index (`tecTOO_SOON`). Without this guard, a resurrected account could replay transactions that were valid before deletion — a critical security property given XRPL's replay-prevention model.

**Owner directory scan.** The code iterates the account's owner directory and calls `nonObligationDeleter()` on each entry's type. If any entry returns `nullptr` from that function — meaning it is a genuine obligation (trust lines with balances, escrows, checks, payment channels) — `preclaim` fails with `tecHAS_OBLIGATIONS`. If the directory contains more than `maxDeletableDirEntries` (1000) deletable items, it returns `tefTOO_BIG` to prevent a single transaction from consuming excessive execution time.

## The Deleter Dispatch Table

The anonymous namespace defines a uniform function pointer type `DeleterFuncPtr` and a set of thin adapter functions, one per deletable ledger entry type. The adapter signatures all match `DeleterFuncPtr` exactly, even though the underlying deletion functions have varied signatures — the adapters simply drop unused parameters:

```cpp
using DeleterFuncPtr = TER (*)(
    ServiceRegistry&, ApplyView&, AccountID const&,
    uint256 const&, std::shared_ptr<SLE> const&, beast::Journal);
```

`nonObligationDeleter()` is a `switch` over `LedgerEntryType` that maps each deletable type to its adapter, and returns `nullptr` for any type that represents a blocking obligation. This design is used identically in both `preclaim` (to detect blockers) and `doApply` (to actually invoke deletions), ensuring the two phases stay in sync without duplicating the type classification logic.

The supported deletable types are: `ltOFFER`, `ltSIGNER_LIST`, `ltTICKET`, `ltDEPOSIT_PREAUTH`, `ltNFTOKEN_OFFER`, `ltDID`, `ltORACLE`, `ltCREDENTIAL`, and `ltDELEGATE`. Each delegates to its own transactor's static deletion helper (e.g., `DIDDelete::deleteSLE`, `OracleDelete::deleteOracle`), keeping cleanup logic co-located with the feature that created the object.

## doApply: Execution

`doApply()` first re-checks deposit authorisation using `verifyDepositPreauth()` when `sfCredentialIDs` is present. Unlike the `preclaim` call to `credentials::valid()`, this call operates on the mutable `ApplyView` so it can remove any expired credentials it discovers — a deliberate two-phase split to handle credential expiry correctly.

The account's owner directory is then walked via `cleanupOnAccountDelete()`, which invokes the deleter callback for each entry. The `nonObligationDeleter` dispatch is called again here; if it somehow returns `nullptr` (which `preclaim` should have prevented), the code hits an `UNREACHABLE` macro and logs an error — a defensive invariant assertion rather than silent data corruption.

After the directory is empty, the remaining XRP balance is transferred atomically: it is added to the destination's `sfBalance` and subtracted from the source's, with `ctx_.deliver()` recording the amount for the metadata. An `XRPL_ASSERT` then verifies the source balance is exactly zero before erasure. Finally, the source account SLE is removed from the ledger view with `view().erase(src)`. As a minor housekeeping detail, if the destination account had `lsfPasswordSpent` set and the incoming XRP is nonzero, that flag is cleared, re-arming the free password-change allowance.

## Invariants and Failure Modes

The sequence-number and NFT-sequence guards are the primary replay-prevention mechanism for deleted accounts. The `tefTOO_BIG` limit prevents a DoS attack where a user accumulates thousands of deletable objects to force a very expensive transaction. The fatal log in `preclaim` and the `UNREACHABLE` guard in `doApply` together ensure that any corruption of the owner directory is surfaced loudly rather than silently propagated.
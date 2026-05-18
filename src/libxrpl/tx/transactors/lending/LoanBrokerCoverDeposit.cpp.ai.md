# `LoanBrokerCoverDeposit.cpp`

## Role in the System

This file implements the `LoanBrokerCoverDeposit` transaction, one of three symmetrical cover-management operations in the XRPL lending protocol (alongside `LoanBrokerCoverWithdraw` and `LoanBrokerCoverClawback`). Its specific job is to allow the **owner** of a `LoanBroker` ledger object to deposit collateral into the broker's pseudo-account, increasing the broker's `sfCoverAvailable` balance. Cover serves as a loss-absorption buffer: the lending protocol requires a broker to maintain a minimum ratio of cover relative to its outstanding loan debt (`sfDebtTotal`), preventing it from taking on more loan exposure than its capitalization can support.

The file is compact — 126 lines — but encodes a layered validation strategy characteristic of all XRPL transactors. It sits under `src/libxrpl/tx/transactors/lending/` alongside the other lending transaction implementations, and depends on `LendingHelpers` for shared protocol utilities.

## Transaction Lifecycle

`LoanBrokerCoverDeposit` inherits from `Transactor` and exposes the four standard static/virtual hooks the framework calls in sequence.

**`checkExtraFeatures`** simply delegates to `checkLendingProtocolDependencies`, which verifies that both `featureSingleAssetVault` and the vault feature's own dependency chain are enabled in the current ledger rules. This gate prevents the transaction from even reaching preflight on ledgers that do not yet support the lending protocol.

**`preflight`** performs cheap, stateless field validation before any ledger reads occur. It rejects a zero `sfLoanBrokerID` (not a valid object reference), a non-positive `sfAmount`, and any amount that fails `isLegalNet` (which catches malformed IOU representations with illegal precision). These checks are intentionally minimal — anything that requires ledger state belongs in `preclaim`.

**`preclaim`** is the substantive validation phase. Its checks fall into a clear hierarchy:

1. *Object existence*: the broker keyed by `sfLoanBrokerID` must exist in the ledger; if missing, `tecNO_ENTRY` is returned.
2. *Ownership*: `sfAccount` on the transaction must equal `sfOwner` on the broker. Only the broker's owner may inject cover; returns `tecNO_PERMISSION` otherwise.
3. *Vault integrity*: the broker's associated vault is looked up via `sfVaultID`. A missing vault is treated as a fatal ledger inconsistency (`tefBAD_LEDGER`, guarded by `LCOV_EXCL_START`), reflecting that this state can only occur through data corruption — the broker creation path guarantees the vault exists.
4. *Asset match*: the deposited amount's asset must equal the vault's `sfAsset`. Returns `tecWRONG_ASSET` if they differ, preventing mixed-asset cover pools.
5. *Transfer compliance* (four sequential checks): `canTransfer` ensures the asset is transferable between the two accounts; `checkFrozen` ensures the asset is not frozen on the depositor's side; `checkDeepFrozen` ensures the broker's pseudo-account can receive (important because deep-freeze prevents receiving even if the sender is unaffected); `requireAuth` with `AuthType::StrongAuth` ensures the depositor has an established trust line or MPToken for the asset. This strong-auth requirement is notable — it means a depositor cannot accidentally fund a broker with an asset they are not explicitly authorized to hold.
6. *Balance*: `accountHolds` with `SpendableHandling::shFULL_BALANCE` verifies the depositor holds enough of the asset, using `fhZERO_IF_FROZEN` and `ahZERO_IF_UNAUTHORIZED` so frozen or unauthorized balances cannot be spent.

**`doApply`** performs the mutation once all preclaim checks pass. It re-reads the broker and vault (failures here are `tecINTERNAL`, again `LCOV_EXCL_LINE` — the ledger is immutable between preclaim and apply within the same transaction batch). The three operations are:

1. `accountSend(view(), account_, brokerPseudoID, amount, j_, WaiveTransferFee::Yes)` — moves funds from the depositor to the broker's pseudo-account. Transfer fees are explicitly waived because this is a protocol-internal capital movement, not a user-to-user IOU transfer; charging a fee here would erode cover capitalization in a way the protocol spec does not intend.
2. `broker->at(sfCoverAvailable) += amount` followed by `view().update(broker)` — increments the tracked cover balance. The `sfCoverAvailable` field is the source of truth the protocol uses for minimum-cover ratio checks during loan issuance and cover withdrawal; it must stay in sync with the pseudo-account's actual token balance.
3. `associateAsset(*broker, vaultAsset)` — a bookkeeping call that updates asset-tracking metadata on the broker SLE. This pattern appears uniformly across all lending transactors (deposit, withdraw, clawback, loan lifecycle events), ensuring protocol-level asset enumeration and cleanup routines can correctly identify which asset each broker object is associated with.

## Design Decisions Worth Noting

The ownership check in `preclaim` uses `sfOwner` from the broker object itself rather than trusting any owner field in the transaction, which prevents an attacker from crafting a transaction that claims ownership via a manipulated field. The broker object in the ledger is the authoritative source.

Compared to `LoanBrokerCoverWithdraw`, which must additionally enforce minimum cover ratios (the withdrawal reduces cover and could violate the required buffer), the deposit path has no equivalent floor check — deposits can only increase cover, so there is no minimum to enforce from the depositor's perspective.

The `LCOV_EXCL_LINE` annotations on the `doApply` null-checks signal that test coverage tooling should not penalize these unreachable guards. They exist as paranoia, not as functional error paths: `preclaim` already verified both the broker and vault exist, and the XRPL apply-phase runs against the same ledger snapshot.
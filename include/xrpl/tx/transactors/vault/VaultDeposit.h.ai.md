# `VaultDeposit.h` — Vault Deposit Transactor Declaration

## Role in the System

`VaultDeposit.h` declares the `VaultDeposit` transactor, which handles the `VaultDeposit` transaction type on the XRP Ledger. This transaction allows an account to deposit assets into a vault ledger object and receive newly minted vault-share MPTokens in return — the essential liquidity-provision operation that makes vaults function as on-ledger asset pools or yield-bearing containers.

The header itself is intentionally minimal: a single class declaration with three static/virtual method stubs and a constructor. All logic lives in `VaultDeposit.cpp`.

## Transactor Lifecycle Pattern

`VaultDeposit` inherits from `Transactor`, the XRPL base class for all transaction processors. The framework dispatches every transaction through three ordered phases, each called with a progressively more expensive context:

- **`preflight(PreflightContext const&)`** — stateless, no ledger access. This is where format-level validation happens before any state is examined. For `VaultDeposit`, it rejects a zero `sfVaultID` and any non-positive `sfAmount`. Because it runs without the ledger, it can be safely parallelised across transactions in a batch.

- **`preclaim(PreclaimContext const&)`** — read-only ledger access. This phase performs every check that requires inspecting current ledger state but does not need write access. The implementation validates that the vault exists, that the deposited asset matches the vault's configured asset type (`sfAsset`), that the vault's MPT issuance for shares is present and unlocked, that neither the asset nor the shares are frozen or locked for the depositor, and — for private vaults — that the depositor is either the vault owner or holds valid domain credentials. It also verifies the depositor holds sufficient spendable balance.

- **`doApply()`** — state-mutating, called on the writable `ApplyView`. This phase commits the exchange: it creates or authorizes an MPToken account for the depositor to hold shares, calculates the share amount using `assetsToSharesDeposit()`, back-verifies the implied asset cost via `sharesToAssetsDeposit()`, updates the vault's asset totals, enforces the vault's maximum asset cap, and issues two atomic `accountSend` calls — assets from depositor to vault pseudo-account, and shares from vault pseudo-account to depositor.

## Key Design Decisions

**`ConsequencesFactory{Normal}`** marks this transaction as non-blocking. In the transaction queue, a `Normal` transaction from an account does not prevent later transactions from the same account from being queued or applied. This is the correct classification because a failed deposit does not invalidate subsequent operations.

**No `checkExtraFeatures` or `getFlagsMask` override.** The base class defaults are used — the vault amendment check is handled centrally via `Permission::getInstance().getTxFeature()` in `Transactor::invokePreflight`, and standard flags apply. `VaultCreate.h` overrides both of these, but `VaultDeposit` has no additional amendment gating or custom flag bits beyond the universal set.

**Two-step exchange calculation in `doApply`.** Rather than directly using the asset amount the depositor offered, `doApply` first converts assets to shares (truncated, because MPT shares are integral), then converts those shares back to the exact asset cost. This reverse-check guarantees the invariant that the vault never extracts more than the depositor offered (`assetsDeposited <= amount`). Any sub-share-unit remainder of the offered amount is effectively returned by not being taken. If shares round to zero, `tecPRECISION_LOSS` is returned, preventing dust deposits that would dilute share accounting.

**Overflow maps to `tecPATH_DRY`.** The `assetsToSharesDeposit` and `sharesToAssetsDeposit` helpers can throw `std::overflow_error` when the vault's scale factor combined with large balances exceeds numeric limits. The implementation catches this and returns `tecPATH_DRY` — a semantically adjacent "exchange failed" code — while logging at `debug` level to avoid log spam from adversarial inputs.

**Private vault authorization uses domain credentials, not MPT issuer authorization.** The vault's shares are issued by a pseudo-account derived from the vault itself, not a human-controlled account. This pseudo-account cannot proactively authorize holders via normal MPT mechanics. Instead, private vault access is governed by a `DomainID` on the MPT issuance, and `credentials::validDomain` is called in `preclaim` to check credential membership. The `tecEXPIRED` error is suppressed in `preclaim` (allowing the transaction to proceed) so that `doApply` can delete the expired credentials as a side effect.

**Transfer fees waived on both legs.** Both the asset transfer (depositor → vault) and the share transfer (vault → depositor) are issued with `WaiveTransferFee::Yes`. This is intentional: the vault itself is the economic actor managing exchange, not an intermediary collecting fees. Allowing transfer fees on vault operations would break the exchange rate accounting and create systemic inaccuracies in `sfAssetsTotal`.

## Relationship to Sibling Files

The vault directory contains six transactors — `VaultCreate`, `VaultSet`, `VaultDelete`, `VaultDeposit`, `VaultWithdraw`, and `VaultClawback` — all following this same three-phase structure. `VaultDeposit` and `VaultWithdraw` are the mirror pair that manage liquidity: deposit mints shares and increases `sfAssetsTotal`/`sfAssetsAvailable`; withdraw burns shares and decreases them. The exchange-rate math in both directions is factored into `VaultHelpers.h`, keeping the conversion logic auditable in one place.
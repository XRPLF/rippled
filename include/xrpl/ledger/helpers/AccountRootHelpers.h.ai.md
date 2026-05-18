# `include/xrpl/ledger/helpers/AccountRootHelpers.h`

This header is the canonical source of free functions for querying and mutating `ltACCOUNT_ROOT` ledger entries (`SLE`s). It sits in the `ledger/helpers` module alongside analogous headers for AMMs, trust lines, NFTs, and other ledger-object types, providing a shared utility layer that transaction processors, payment engines, and invariant checkers all pull from.

## Token Economics Helpers

`isGlobalFrozen()` checks whether an IOU issuer has activated the global freeze flag (`lsfGlobalFreeze`). It short-circuits immediately for XRP — XRP is never frozen. This guard appears throughout the payment paths to prevent token movement when an issuer is in a frozen state.

`transferRate()` returns the IOU transfer fee for an issuer as a `Rate` value — a fraction expressed in units of one billion (so 1% = 1,010,000,000). If the issuer account doesn't exist or hasn't set `sfTransferRate`, it falls back to `parityRate` (no fee), ensuring callers never have to handle a null case.

## Owner Count and Reserve Accounting

`adjustOwnerCount()` increments or decrements `sfOwnerCount` on an account SLE and notifies the view via `adjustOwnerCountHook()`, which lets sandboxed views (like `PaymentSandbox`) track in-flight count changes. Internally it delegates to the file-static `confineOwnerCount()`, which prevents both unsigned underflow and overflow with clamping to 0 and `UINT32_MAX` respectively, logging at `fatal` severity when clamping occurs on a real account. That defensive clamping is deliberate — the field is `uint32_t`, so arithmetic wraps silently without it.

`xrpLiquid()` computes how much XRP an account can freely spend: balance minus reserve. The `ownerCountAdj` parameter is the key design decision here — callers often need to know available balance *before* or *after* they add/remove ledger entries that would change the reserve, so they pass a signed delta rather than relying on a state that isn't yet committed to the view. The function also calls `view.ownerCountHook()` and `view.balanceHookIOU()`, which are virtual hooks allowing the `PaymentSandbox` to intercept and adjust values reflecting uncommitted in-flight changes during multi-step payment processing. Crucially, pseudo-accounts bypass reserve requirements entirely (`XRPAmount{0}`), since they cannot submit transactions and should never be blocked by a reserve check.

## Pseudo-Account Subsystem

The most architecturally novel feature in this file is the pseudo-account mechanism — a way for first-class ledger objects (AMMs, Vaults, LoanBrokers) to own an `ltACCOUNT_ROOT` SLE that has no private key and is not controlled by any user.

**Field discovery via `getPseudoAccountFields()`** returns a singleton `const std::vector<SField const*>`. The list is built once at startup by iterating the `ltACCOUNT_ROOT` format's `SOTemplate` and selecting any field that has the `SField::sMD_PseudoAccount` metadata bit set (bit 0x40). Currently that set is `sfAMMID`, `sfVaultID`, and `sfLoanBrokerID`, each defined with `SField::sMD_PseudoAccount | SField::sMD_Default` in `sfields.macro`. This design means adding a new pseudo-account type requires nothing more than tagging its key field with `sMD_PseudoAccount` — the discovery logic is fully data-driven, with no manual registration.

**`isPseudoAccount()`** has two overloads. The primary overload takes an SLE directly and checks three things defensively: the pointer is non-null, the SLE's type is `ltACCOUNT_ROOT`, and at least one pseudo-account field is present. The optional `pseudoFieldFilter` set narrows the check to specific field(s), allowing callers to distinguish AMM pseudo-accounts from Vault pseudo-accounts. The inline convenience overload reads the account from a `ReadView` via `keylet::account()` and delegates.

**`pseudoAccountAddress()`** derives a new `AccountID` using a loop of up to 256 tries. Each attempt hashes a counter, the parent ledger's hash, and the owner key through SHA-512 half-digest followed by a `ripesha_hasher` (RIPEMD-160(SHA-256(...))). The loop skips any address that already exists in the view. The 256-attempt cap is consensus-critical — the comment explicitly notes it cannot change without an amendment. In practice, collisions are astronomically unlikely; the loop is purely defensive.

**`createPseudoAccount()`** returns `Expected<std::shared_ptr<SLE>, TER>`, the XRPL error-or-value pattern. It performs a debug assertion (`XRPL_ASSERT`) that the provided `ownerField` is registered as a pseudo-account field, providing a loud diagnostic instead of silent corruption. The created SLE gets `sfSequence = 0` (when `featureSingleAssetVault` or `featureLendingProtocol` is active), making it visually distinct from normal accounts and providing an extra barrier against accidental transaction submission. Flags are set to `lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth`: the master key is disabled so no private key can sign, default rippling is on so the pseudo-account can hold and transfer IOUs through trust lines, and deposit authorization blocks direct payments from external accounts. The comment in `createPseudoAccount` makes an important point: **amendment checks are the caller's responsibility** — this function is amendment-neutral by design, and callers like `VaultCreate` and `LoanBrokerSet` perform the relevant `view.rules().enabled(...)` checks before invoking it.

## Destination Validation

`checkDestinationAndTag()` is a small but widely reused guard: it returns `tecNO_DST` for a null SLE and `tecDST_TAG_NEEDED` if the destination account requires a tag (`lsfRequireDestTag`) but none was provided. Centralizing this check avoids duplicated logic across the many transaction types that send to an account destination.

## Relationships

The implementation file (`AccountRootHelpers.cpp`) is a direct dependency of AMM, Vault, and LoanBroker transactors. The `xrpLiquid()` and `adjustOwnerCount()` functions are called across nearly all transaction processors. The hook-based design for balance and owner-count queries (`balanceHookIOU`, `ownerCountHook`, `adjustOwnerCountHook`) allows `PaymentSandbox` to overlay uncommitted changes during complex multi-step payment flows without the helpers needing to know about sandboxing internals.
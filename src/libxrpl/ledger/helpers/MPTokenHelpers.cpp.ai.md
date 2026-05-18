# `src/libxrpl/ledger/helpers/MPTokenHelpers.cpp`

This file implements all MPT-specific (Multi-Purpose Token) business logic that operates directly on ledger state — freeze checking, authorization, holding lifecycle, escrow accounting, transfer permission, and MPT supply overflow safety. It is the MPT counterpart to `RippleStateHelpers.cpp`, which handles the equivalent operations for IOU trust lines. Together both files feed into the asset-agnostic dispatch layer in `TokenHelpers.cpp`, which calls them via `std::visit` on the `Asset` variant type.

## Freeze Checking

The freeze model has three independent tiers, checked cheapest first. `isGlobalFrozen` reads the `MPTIssuance` SLE and checks `lsfMPTLocked`; if that flag is absent, the entire issuance is unfrozen regardless of individual holdings. `isIndividualFrozen` reads the per-account `MPToken` SLE and checks the same flag at that level. Both return `false` if the SLE is absent — a missing issuance or missing token entry is treated as unfrozen, not as an error. `isFrozen` combines these two plus a third call to `isVaultPseudoAccountFrozen` (defined in `VaultHelpers`), which recursively checks whether the issuer is a vault pseudo-account and whether the vault's underlying asset is frozen. The `depth` parameter passed through the call chain protects against theoretical infinite recursion in nested vault configurations; the header comment explicitly notes this is purely defensive because the ledger does not currently permit such nesting.

`isAnyFrozen` takes an `initializer_list<AccountID>` and applies the same three-tier check, but deliberately separates the two loops: global freeze is checked once (short-circuiting immediately if true), then all accounts are checked for individual freeze, and only then are all accounts checked for vault pseudo-account freeze. This ordering avoids the most expensive recursive vault check unless none of the cheaper checks triggered.

## Transfer Rate

`transferRate` reads the `sfTransferFee` field (a `uint16` in the range 0–50,000 representing 0–50%) and converts it to the XRPL `Rate` representation: `1,000,000,000 + (10,000 × fee)`. A 50% fee becomes `1,500,000,000`. When no `sfTransferFee` field is present, the function returns `parityRate` (exactly `1,000,000,000`, meaning no fee). This encoding aligns MPT fees with the scale used for IOU transfer rates.

## Holding Lifecycle

`canAddHolding` is a read-only pre-check that validates two preconditions: the `MPTIssuance` must exist, and it must have `lsfMPTCanTransfer` set. This flag means the token allows third-party holders at all; tokens without it can only be transferred directly between the issuer and its counterparties, so adding an independent holding makes no sense.

`addEmptyHolding` delegates to `authorizeMPToken` after verifying the issuance exists, is not globally locked, and no duplicate `MPToken` entry already exists. If the account is the issuer itself, the function returns `tesSUCCESS` immediately — the issuer never holds a `MPToken` SLE for their own issuance. The reserve check follows the same rule as trust lines: reserves are only enforced when the account already owns more than two objects; new accounts get the first two items free.

`removeEmptyHolding` is the deletion mirror. It defensively checks for the issuer case (where a token SLE should not exist at all), verifies that both `sfMPTAmount` and `sfLockedAmount` are zero, then delegates to `authorizeMPToken` with the `tfMPTUnauthorize` flag rather than duplicating the SLE removal logic.

`createMPToken` is a lower-level primitive (called by `checkCreateMPT`) that directly inserts an `MPToken` SLE and links it into the owner directory without the reserve or issuance validity checks that `addEmptyHolding`/`authorizeMPToken` perform. `checkCreateMPT` wraps it with an idempotent "create if not exists" pattern and adjusts the owner count, making it suitable for apply-phase callers that need to auto-create a holding.

## Authorization: The Two-Phase Split

Authorization is split across `requireAuth` (called in preclaim — read-only) and `enforceMPTokenAuthorization` (called in apply — mutating).

`authorizeMPToken` does the actual ledger work. The `holderID` optional parameter distinguishes which side of the authorization relationship submitted the transaction: when `nullopt`, the submitter is the holder (who is either creating or deleting their own MPToken SLE); when set, the submitter is the issuer toggling `lsfMPTAuthorized` on the holder's existing `MPToken`. This dual role in a single function avoids duplicating the SLE lookup and directory management code.

`requireAuth` handles the `lsfMPTRequireAuth` check in preclaim. Issuers are always treated as authorized (they have no `MPToken` SLE for their own issuance). Vault pseudo-accounts and `LoanBroker` pseudo-accounts are implicitly authorized without needing an explicit `MPToken`. If the issuance carries a `sfDomainID`, authorization by credential is checked first via `credentials::validDomain`; if that passes, the function succeeds even without an `MPToken` SLE. If the domain check passes but an `MPToken` exists, the token's `lsfMPTAuthorized` flag is ignored — domain-based authorization supersedes it.

`enforceMPTokenAuthorization` is its apply-phase counterpart. The critical difference: preclaim cannot mutate the ledger, so if a domain-authorized account lacks an `MPToken` SLE, preclaim cannot create one. The apply phase calls `enforceMPTokenAuthorization`, which handles exactly this case by calling `authorizeMPToken` to materialize the `MPToken` entry on the fly. The function is structured as a complete case analysis over the four combinations of `(authorizedByDomain, sleToken != nullptr)`, with `XRPL_ASSERT` guards on each branch to document the expected invariant. An `UNREACHABLE` sentinel with `tefINTERNAL` guards the final branch, which the case analysis proves can never be reached.

## Transfer and Trade Permissions

`canTransfer` enforces the `lsfMPTCanTransfer` flag with a key carve-out: when the flag is absent, transfers that directly involve the issuer (either as sender or receiver) are still permitted. This mirrors how IOU trust lines allow issuer-direct payments regardless of transfer restrictions. `canTrade` wraps `lsfMPTCanTrade` with a type-safe `asset.visit` dispatch, always returning success for plain XRP/IOU assets.

`checkMPTAllowed` (static, internal) implements a layered permission check for DEX and payment operations: issuance must exist, must not be globally locked, must have `lsfMPTCanTrade` set, and non-issuers must additionally have `lsfMPTCanTransfer` and an unlocked individual `MPToken`. It deliberately tolerates a missing `MPToken` (returning success) because some transaction types auto-create one during apply. `checkMPTTxAllowed` is the public wrapper that asserts the transaction is not a payment (payments go through a separate path) before delegating.

## Escrow Accounting

`lockEscrowMPT` and `unlockEscrowMPT` manage the `sfLockedAmount` field at both the `MPToken` (holder) level and the `MPTIssuance` level. When an MPT is placed in escrow, its balance is moved from `sfMPTAmount` to `sfLockedAmount` in the holder's `MPToken` SLE, and the issuance's `sfLockedAmount` is incremented — critically, `sfOutstandingAmount` is left unchanged because the tokens are still in circulation. Every arithmetic operation is guarded by a `canSubtract` or `canAdd` check to detect underflow and overflow before mutation; these guards are marked `LCOV_EXCL_LINE` because they represent invariant violations that should be unreachable in a correct implementation.

`unlockEscrowMPT` handles two distinct redemption paths: if the receiver is the issuer, it decrements `sfOutstandingAmount` (the tokens are retiring back to the issuer); if the receiver is a third party, it increases that party's `sfMPTAmount`. The `grossAmount`/`netAmount` split (enabled by `fixTokenEscrowV1`) represents the transfer fee taken at escrow release — the difference is removed from `sfOutstandingAmount` because those tokens effectively returned to the issuer as fee income.

## Supply Overflow Safety

`isMPTOverflow` centralizes the two-threshold overflow design described in the header: direct send operations check against `MaximumAmount` strictly, while payment engine paths use `UINT64_MAX` as a temporary ceiling to allow transient in-flight values. `availableMPTAmount` computes the headroom as `MaximumAmount - OutstandingAmount`, using `value_or(maxMPTokenAmount)` for the common case where no cap was specified. The view-taking overload throws `std::runtime_error` if the issuance SLE is missing — since this is called mid-computation in the payment engine, a missing issuance at that point indicates a ledger consistency failure rather than a user error.
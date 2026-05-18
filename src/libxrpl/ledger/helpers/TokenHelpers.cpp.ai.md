# `TokenHelpers.cpp` — Unified Token Dispatch Layer

`TokenHelpers.cpp` serves as the central dispatch layer between the XRPL ledger's two non-XRP token systems — trust-line-based IOUs and the newer Multi-Party Token (MPT) standard. The XRPL protocol internally represents both as an `Asset` variant (`std::variant<Issue, MPTIssue>`), and this file provides the glue: every public function accepts an `Asset` (or both concrete types via overloading), decomposes it using `std::visit` or `Asset::visit`, and routes to the type-specific implementation in either `RippleStateHelpers` (IOU) or `MPTokenHelpers` (MPT).

The file is organized into four sections: freeze checking, balance queries, holding lifecycle, and actual money transfers. Higher-level transaction code — payments, offers, AMM operations, vault operations — calls into this file so it never has to branch on token type itself.

## Freeze Checking

The freeze model differs meaningfully between IOUs and MPTs, and this file bridges the difference.

For IOUs, `isFrozen` and `isDeepFrozen` are distinct. A frozen IOU trust line prevents the holder from sending but still allows redemption back to the issuer; "deep frozen" locks the line entirely. For MPTs, `isDeepFrozen` is explicitly identical to `isFrozen` (see the comment at line 104): an MPT lock prevents both sending and receiving, so there is no meaningful additional deep-freeze concept.

`checkFrozen` returns different `TER` codes depending on asset type: `tecFROZEN` for IOUs, `tecLOCKED` for MPTs. This semantic difference surfaces in transaction error handling at the protocol level.

The `isAnyFrozen` family is designed for multi-party checks — offer processing and AMM liquidity steps frequently need to verify that neither the buyer nor the seller side is frozen. The `std::initializer_list<AccountID>` parameter avoids an allocation and keeps the call sites terse.

The `depth` parameter on `isFrozen` and `isDeepFrozen` exists for future-proofing vault recursion: MPT shares that represent vault positions may themselves be backed by assets that are frozen. The header comments note this is purely defensive — such vaults cannot currently be created — but the parameter is threaded through consistently so the architecture doesn't need to change if that changes.

## Balance Queries

`accountHolds` has four overloads unified around the `SpendableHandling` flag. The `shSIMPLE_BALANCE` path returns the raw balance; `shFULL_BALANCE` adds the opposite party's credit limit, representing what the account could spend if the counterparty were willing to absorb debt. For issuers, the semantics diverge: IOU issuers get `STAmount::cMaxValue` (infinite), while MPT issuers get the remaining issuance capacity (`MaximumAmount - OutstandingAmount`) from the issuance SLE.

The `AuthHandling` parameter for MPT balances controls whether `accountHolds` zeroes the result when the MPT requires authorization and the holder lacks it. The `featureSingleAssetVault` amendment changes which path this takes: with the amendment enabled, the check is delegated to `requireAuth()`, which handles the vault's recursive authorization model; without it, the flag check is done inline.

The private helper `getLineIfUsable` encapsulates the freeze-aware trust line lookup. A notable non-obvious piece is the `fixFrozenLPTokenTransfer` amendment path: if the trust line's issuer is an AMM account (detected via `sfAMMID` on the issuer SLE), the function calls `isLPTokenFrozen` to verify neither of the AMM's underlying pool assets is frozen. This check is retrofitted — earlier ledger versions could transfer LP tokens even when the underlying pool was frozen.

## Money Transfers

The transfer subsystem has two public entry points (`accountSend` and `accountSendMulti`) and one semi-public entry for fee-free sends (`directSendNoFee`). Each dispatches to a static IOU or MPT implementation.

**IOU transfer pipeline.** The core IOU path is `directSendNoFeeIOU`, which modifies trust line balances directly. When a trust line doesn't exist, it calls `trustCreate` to create one on behalf of the receiver — this implicit trust line creation is unique to direct (issuer-involved) sends. When a send zeros out a sender's balance and the trust line meets a specific set of conditions (zero trust limit, zero quality flags, balance crossing zero, no freeze), the function releases the sender's ledger reserve and potentially deletes the trust line via `trustDelete`. The comment in the code correctly notes this logic "NEEDS to be cleaned up and simplified."

Third-party IOU sends (sender, receiver, and issuer are all distinct) go through `directSendNoLimitIOU`, which applies a transfer fee by multiplying the amount by `transferRate(view, issuer)` and then executes two sequential `directSendNoFeeIOU` calls: first, issuer→receiver for the delivery amount; second, sender→issuer for the gross amount including the fee.

**MPT transfer pipeline.** The equivalent `directSendNoFeeMPT` directly manipulates the `sfMPTAmount` field on `MPToken` SLEs and the `sfOutstandingAmount` field on the `MPTokenIssuance` SLE. When the sender is the issuer, the outstanding amount increases; when the receiver is the issuer, it decreases. Third-party MPT sends route through the issuer's outstanding balance the same way IOU sends route through trust lines, applying transfer fees identically.

**Multi-destination sends.** `accountSendMulti` supports batched payments where one sender distributes a single asset to multiple recipients atomically. The IOU multi-path collects transfer fees into a running `takeFromSender` total per recipient, then issues a single debit from sender→issuer at the end. The MPT multi-path does the same but with a critical security fix via `fixSecurity3_1_3`: pre-fix code read `sfOutstandingAmount` from a stale `view.read()` snapshot on each loop iteration, meaning issuer-as-sender multi-sends could silently exceed `MaximumAmount`. The fix accumulates a `totalSendAmount` in exact `uint64_t` arithmetic (not `STAmount`/`Number`, which would lose precision near the 19-digit `maxMPTokenAmount` boundary) and performs an aggregate check per iteration.

**Transfer fees and `WaiveTransferFee`.** Both IOU and MPT paths check `WaiveTransferFee::Yes` before applying fees. This waiver is used by vault and AMM operations that need to move tokens without incurring transfer charges.

## Enumerations and Policy

The header defines six policy enums that control behavior without adding Boolean parameters: `FreezeHandling`, `AuthHandling`, `SpendableHandling`, `WaiveTransferFee`, `AllowMPTOverflow`, and `AuthType`. These make call sites self-documenting and make it possible to extend policy without breaking existing callers.

`AllowMPTOverflow` deserves particular attention: MPT vaults need to allow the `sfOutstandingAmount` to exceed `sfMaximumAmount` transiently during vault rebalancing. This enum gates the `isMPTOverflow` check inside `directSendNoLimitMPT`, and it is itself gated behind `featureMPTokensV2` — the overflow allowance only applies when the newer MPT ruleset is active.
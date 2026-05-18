# `AMMCreate.h` — AMM Instance Creation Transactor

## Role in the System

`AMMCreate.h` declares the `AMMCreate` transactor, one of the core DEX primitives in the XRPL. It handles the `AMMCreate` transaction type, which bootstraps a new Automatic Market Maker pool from scratch. Within the DEX module (alongside `AMMDeposit`, `AMMWithdraw`, `AMMVote`, `AMMBid`, and `AMMDelete`), `AMMCreate` is the entry point — no other AMM transaction can operate until this one successfully creates the pool's ledger objects and distributes initial LP tokens to the creator.

The header is deliberately minimal: a `#pragma once`, a single `#include` of `Transactor.h`, and the class declaration. All implementation lives in `AMMCreate.cpp`.

## The Transactor Lifecycle

`AMMCreate` inherits from `Transactor` and participates in the four-phase transaction processing pipeline that the framework drives through compile-time polymorphism (name hiding, not virtual dispatch). The `invokePreflight<T>` template in `Transactor.h` sequences the static methods automatically:

**`checkExtraFeatures`** gates the entire transaction on feature flags before any validation occurs. The AMM subsystem itself (`ammEnabled`) must be active on the current rules set, and if either asset is an MPT issue, `featureMPTokensV2` must also be enabled. This separation from `preflight` keeps amendment checks centralized and avoids redundant ledger reads.

**`preflight`** performs stateless structural checks on the transaction fields. It rejects duplicate assets (`sfAmount.asset() == sfAmount2.asset()`), validates both amounts via `invalidAMMAmount`, and enforces the `TRADING_FEE_THRESHOLD` ceiling on `sfTradingFee`. Because `preflight` is stateless — it has no `ReadView` access — these checks are cheap and deterministic.

**`calculateBaseFee`** departs from the standard pattern: rather than returning the network's base fee in drops, it returns `calculateOwnerReserveFee`, which equals one owner reserve increment. This reflects the economic reality that `AMMCreate` permanently allocates a ledger object; charging only the base fee would undercharge for a scarce resource.

**`preclaim`** performs the expensive ledger reads. It checks for a pre-existing AMM at the canonical keylet (`keylet::amm(asset1, asset2)`), verifies authorization and freeze status for both assets, checks that IOU issuers have `lsfDefaultRipple` set (required so balance can flow freely through the pool), confirms the creator has enough XRP for the LP token trustline reserve plus initial asset deposits, and rejects attempts to seed a pool with existing LP tokens (preventing AMM-of-AMM bootstrapping). It also enforces the clawback policy: before `featureAMMClawback` was enabled, any asset with clawback capability (`lsfAllowTrustLineClawback` or `lsfMPTCanClawback`) was forbidden entirely.

**`doApply`** wraps execution in a `Sandbox` — a copy-on-write overlay over the live `ApplyView` — calling the file-local `applyCreate()` function. Only when that function succeeds does `sb.apply(ctx_.rawView())` flush the changes atomically. This is the standard XRPL rollback pattern: if anything inside `applyCreate` returns a failure `TER`, the sandbox is discarded and the ledger is untouched.

## What `doApply` Actually Creates

`applyCreate()` in the `.cpp` builds four classes of ledger objects:

1. **A pseudo-account** (`AccountRoot`) created by `createPseudoAccount()` with a key derived from the AMM keylet. The account has no master key and is marked with `sfAMMID`, making it a non-user account that exists solely to hold XRP balances and issue LP tokens.

2. **An `ltAMM` ledger object** keyed by `hash{asset1.currency, asset1.issuer, asset2.currency, asset2.issuer}` with the assets stored in canonical order (`std::minmax`). This object records `sfAccount` (the pseudo-account ID), `sfLPTokenBalance`, both assets, and the initial fee/auction/vote state via `initializeFeeAuctionVote`. The deterministic key means any transaction can look up the AMM without any stored pointer — the lookup is pure computation.

3. **LP tokens** computed as `sqrt(A * B)` via `ammLPTokens()` and issued from the AMM pseudo-account to the creator via `accountSend`. The initial creator simultaneously receives the auction slot and voting slot.

4. **Trustlines / MPToken entries** for each asset. For IOU assets, the trustline is flagged `lsfAMMNode` to signal it is pool-owned (credit limit stays at 0, deliberately preventing unsolicited LPToken sends). For MPT assets, `createMPToken` is called with `lsfMPTAMM` and, if needed, `lsfMPTAuthorized`. Transfer fees are waived for the initial asset sends (`WaiveTransferFee::Yes`).

After successful object creation, both directions of the trading pair are registered in the `OrderBookDB`, enabling payment path finding and offer crossing to route through the new pool.

## Design Decisions Worth Noting

The `ConsequencesFactory = Normal` constant tells the transaction queue that `AMMCreate` does not block subsequent transactions from the same account — it is a one-shot creation, not an ongoing lock.

The `DefaultRipple` requirement on IOU issuers is non-obvious but essential: without it, the AMM's trustlines cannot ripple balances between counterparties, breaking the pool's core exchange mechanic.

The zero-credit-limit trustlines for LP tokens are a deliberate security constraint. A holder can only acquire LP tokens through affirmative actions (deposit, trustline setup, offer crossing) — the AMM cannot push tokens to an unwilling account.

The clawback evolution across feature flags illustrates XRPL's incremental amendment strategy: the initial AMM release entirely forbade clawback-capable assets; `featureAMMClawback` later unlocked that restriction with proper accounting, but the older guard code remains active on ledgers where that amendment has not yet passed.
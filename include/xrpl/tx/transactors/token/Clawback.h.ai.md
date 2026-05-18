# `Clawback.h` — Clawback Transactor Declaration

## Role in the System

This header declares `Clawback`, the transactor class responsible for handling `ttCLAWBACK` transactions on the XRP Ledger. Its position in `include/xrpl/tx/transactors/token/` places it alongside the other token-lifecycle transactors — `TrustSet`, `MPTokenIssuanceCreate`, `MPTokenAuthorize`, and others — that collectively govern how fungible value is issued and managed on-ledger. The `Clawback` transactor enforces an issuer's right to reclaim distributed tokens from holder accounts, a feature introduced for regulatory compliance use cases where an issuer needs to reverse a distribution.

The header is intentionally minimal: it exposes only the class declaration and the three well-defined hooks the transaction processing pipeline requires. All implementation logic lives in the corresponding `src/libxrpl/tx/transactors/token/Clawback.cpp`.

## The `Transactor` Inheritance Contract

`Clawback` inherits from `Transactor`, the abstract base that drives the entire transaction processing pipeline in rippled. The pipeline is strictly three-phase:

1. **`preflight`** — stateless, read-only validation run before the transaction touches any ledger state. It checks structural correctness of the transaction fields themselves.
2. **`preclaim`** — read-only validation against the current ledger view. It verifies that the preconditions for success actually hold (the trust line exists, the issuer flag is set, the balance is non-zero).
3. **`doApply`** — the only mutable phase, where ledger state is actually modified.

These three methods are not virtual in the traditional sense. The base class documents this explicitly: they are invoked through `invokePreflight<T>`, a template function that uses name hiding (compile-time polymorphism) rather than vtable dispatch. This design keeps the hot path overhead-free while still allowing derived classes to override individual phases. Only `doApply()` is declared `override` — the other two are `static` and participate in the compile-time dispatch machinery.

## `ConsequencesFactory` and Fee Semantics

```cpp
static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};
```

This constant tells the transaction consequence system how to model the worst-case fee impact of the transaction before it is applied. `Normal` means the transaction claims a fee exactly equal to its declared fee field and does not block other transactions from the same account. The alternative `Blocker` type is used for transactions like `AccountDelete` that prevent further processing of transactions from the account, and `Custom` allows per-transaction consequence calculation. `Clawback` is `Normal` because it is a straightforward fee-paying operation with no unusual sequencing implications.

## Dual Token Type Architecture

The most architecturally significant aspect of `Clawback` — visible only by reading the implementation — is its complete dual support for both IOU trust-line tokens (`Issue`) and Multi-Purpose Tokens (`MPTIssue`). The implementation uses C++20 template specialization with `std::visit` to dispatch over the asset variant held in `sfAmount`:

```cpp
return std::visit(
    [&]<typename T>(T const&) { return preflightHelper<T>(ctx); },
    ctx.tx[sfAmount].asset().value());
```

Each phase (`preflight`, `preclaim`, `doApply`) has a pair of `preflightHelper<Issue>` / `preflightHelper<MPTIssue>` specializations. This avoids a single monolithic function full of `if (isIOU) ... else if (isMPT) ...` branches, keeping the asset-type-specific logic isolated and independently testable.

### IOU Clawback Path

For IOU-based tokens, `preflight` validates that the `sfHolder` field is *absent* (for IOUs, the holder identity is embedded in the `sfAmount`'s issuer field — an intentional encoding choice that avoids adding a separate required field). `preclaim` then confirms that the issuer account has `lsfAllowTrustLineClawback` set and that `lsfNoFreeze` is *not* set — an account that permanently waived freeze rights cannot retain clawback capability, making the flags mutually exclusive at the protocol level. The trust line balance orientation is also validated: XRPL trust line balances are signed and stored from the perspective of the lower-address account, so `preclaim` checks that the sign of the balance matches the expected issuer/holder relationship before calling `accountHolds` (rather than reading the raw balance directly) to account for edge cases introduced by features like XLS-34.

### MPT Clawback Path

For MPT-based tokens, the encoding is different: the holder account is supplied in the separate `sfHolder` field rather than inside `sfAmount`. `preflight` requires `sfHolder` to be present and validates the amount against `maxMPTokenAmount`. `preclaim` reads the `MPTokenIssuance` ledger object and checks the `lsfMPTCanClawback` flag on the issuance itself (not on the issuer account), then confirms the `MPToken` object for that holder exists. MPT clawback is additionally gated on the `featureMPTokensV1` amendment check inside `preflightHelper<MPTIssue>`.

### Application Phase

`doApply()` calls `directSendNoFee` to move tokens from the holder's account back to the issuer — a ledger primitive that transfers IOU or MPT balances without applying transfer fees. The amount transferred is `min(spendableAmount, clawAmount)`, where `spendableAmount` is obtained via `accountHolds` with `fhIGNORE_FREEZE` to bypass any freeze state the issuer may have set. This means clawback always succeeds up to the available balance even if the account is frozen, reflecting the regulatory intent: the issuer is reclaiming their own issued value.

## Guards Against Special Account Types

The `preclaim` implementation contains two noteworthy defensive checks before dispatching to the asset-type helpers. If the `featureSingleAssetVault` amendment is active, clawback is explicitly rejected against pseudo-accounts (returning `tecPSEUDO_ACCOUNT`). If the holder is an AMM account (indicated by the presence of `sfAMMID` on the holder's account root), the transaction is rejected with `tecAMM_ACCOUNT`. AMM liquidity pool accounts hold tokens on behalf of the protocol and should not be subject to issuer clawback; the dedicated `AMMClawback` transactor exists for recovering tokens from an AMM position.
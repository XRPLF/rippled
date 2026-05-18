# `AMMWithdraw.h` — AMM Liquidity Withdrawal Transactor

## Role in the System

`AMMWithdraw.h` declares the `AMMWithdraw` transactor, which handles the `AMMWithdraw` transaction type on the XRP Ledger. It is the mirror image of `AMMDeposit`: where deposit adds liquidity and issues LPTokens to the caller, withdrawal burns LPTokens and returns the underlying pool assets to the liquidity provider. The file lives in `include/xrpl/tx/transactors/dex/` alongside the complete family of AMM-related transactors (`AMMCreate`, `AMMDeposit`, `AMMBid`, `AMMVote`, `AMMDelete`, `AMMClawback`), all of which extend the central `Transactor` base class.

## The Withdrawal Mode Design

The most architecturally distinctive aspect of this class is that it implements **five distinct withdrawal modes**, each selected by the combination of fields the caller supplies in the transaction. Rather than a single monolithic path, each mode is a separate private method:

| Fields supplied | Private method | Fee charged? |
|---|---|---|
| `LPTokens` only | `equalWithdrawTokens` | No |
| `Asset1Out` + `Asset2Out` | `equalWithdrawLimit` | No |
| `Asset1Out` only | `singleWithdraw` | Yes |
| `Asset1Out` + `LPTokens` | `singleWithdrawTokens` | Yes |
| `Asset1Out` + `EPrice` | `singleWithdrawEPrice` | Yes |

The no-fee modes are those that remove liquidity proportionally without disturbing the pool's price ratio, i.e., they neither add nor remove arbitrage opportunity. Single-asset withdrawals are mechanically equivalent to a swap followed by a proportional withdrawal, so the trading fee applies — the same logic that makes single-asset deposits fee-bearing in `AMMDeposit`.

The `equalWithdrawLimit` mode (both assets with caps) is distinctive: the trader specifies maximum amounts for each asset. The transactor computes the largest proportional withdrawal that fits within both caps, so the actual amounts withdrawn may be less than the stated maximums. This gives traders price-slippage protection when removing dual-asset liquidity.

The `singleWithdrawEPrice` mode enforces two simultaneous constraints: a minimum output amount (`Asset1Out`, or zero meaning unconstrained) and a maximum effective price per LP-token burned (`EPrice`). This is the closest analogue to a limit order at exit time — the transaction fails if the market has moved beyond the price the trader was willing to accept.

## The `WithdrawAll` Sentinel

The file introduces a small but important `enum class WithdrawAll : bool { No = false, Yes }`. When a user redeems their entire LP position (all LPTokens), the implementation must handle the final-withdraw case carefully: rounding errors in pool math can leave dust amounts, and the pool itself may become empty. The `isWithdrawAll(STTx const&)` static helper decodes the transaction flags to produce this value, and it flows through to both `equalWithdrawTokens` and `withdraw` so the inner math can apply exact-zero semantics rather than ratio arithmetic.

## Public Static Helpers and the Two-Layer Architecture

`AMMWithdraw` exposes two public static overloads — `equalWithdrawTokens(...)` and `withdraw(...)` — whose signatures are broader than their private counterparts. They accept `FreezeHandling`, `AuthHandling`, `priorBalance`, and a `beast::Journal`, and return a four-tuple `std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>` (error code, new LPToken balance, asset1 withdrawn, optionally asset2 withdrawn). These extra parameters come from `TokenHelpers.h`, where `FreezeHandling` (`fhIGNORE_FREEZE` / `fhZERO_IF_FROZEN`) and `AuthHandling` (`ahIGNORE_AUTH` / `ahZERO_IF_UNAUTHORIZED`) govern how the balance-transfer helpers behave when trustlines are frozen or unauthorised. Exposing these as static functions allows the `AMMDelete` transactor (which must drain a pool as part of account teardown) to call withdrawal logic without constructing a full `AMMWithdraw` transactor instance.

The private counterparts of the same methods drop `FreezeHandling`, `AuthHandling`, `priorBalance`, and the journal — they are called from `applyGuts`, which already holds that context and passes it via the instance's `ApplyContext`. This layering avoids duplication: the public statics carry the full context for external callers; the private methods are thin wrappers that reuse `ctx_` state.

## `deleteAMMAccountIfEmpty`

The static `deleteAMMAccountIfEmpty` encapsulates the post-withdrawal cleanup check. After burning LPTokens, if the total supply of LP tokens has reached zero the AMM instance account and its associated `ltAMM` ledger entry become orphaned objects. This method detects that condition and triggers their deletion before returning, preventing ledger object leaks. It takes a `Sandbox` rather than the live view, which is consistent with the broader pattern in this codebase of staging all mutations in a sandbox that is only committed if `doApply()` succeeds.

## Transaction Lifecycle

The standard three-phase lifecycle mirrors every other transactor in the system:

- `preflight` (static) — validates fields, flags, and feature gates without touching ledger state.
- `preclaim` (static, `ReadView` only) — verifies the AMM account exists, the LP position is valid, and enough LPTokens are held.
- `doApply` / `applyGuts` — selects the appropriate withdrawal mode based on the transaction's field combination, executes it against a `Sandbox`, then commits if successful.

`ConsequencesFactory` is set to `Normal`, meaning the transaction claims a fee on failure in the standard way, not as a blocker or with custom fee logic.

## Relationship to Sibling Files

`AMMWithdraw.h` is structurally symmetric with `AMMDeposit.h`: both offer five field-combination modes, a fee/no-fee split along proportional vs. single-asset lines, `equalXxx` and `singleXxx` private methods, and the same `applyGuts` / `Sandbox` pipeline. `AMMDelete.h` is the downstream consumer of the public statics — it calls into the withdrawal machinery to drain a pool that has already reached zero LP supply but still has residual trust-line cleanup to perform. `AMMHelpers.h` provides the AMM constant-product math (`lpTokensOut`, `ammLPTokens`, etc.) that the private methods call to compute how many LP tokens to burn or how much asset to release.
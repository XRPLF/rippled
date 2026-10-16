# AMM Curves Sandbox — CL + AMMPositionTransfer + CtBinned

**Goal:** ship CL (per-LP custom ranges) with `AMMPositionTransfer`, then add `CtBinned` (fungible per-bin MPT shares) on the same branch. Either curve can be removed before mainnet by toggling its amendment flag. This is a sandbox to see what actually works for XRP/RLUSD.

**Posture:** exploratory. Get both designs right enough to ship and observe; don't over-engineer. Per `.claude/CLAUDE.md`, design on technical merit, implement as simply as possible given the design is right.

**Grounded in:** [xrpl-guides/mm-meeting-questions.md](../../xrpl-guides/mm-meeting-questions.md) Appendix A (on-chain composability and growth data).

---

## Design decisions locked upfront

| Decision | Choice | Rationale |
|---|---|---|
| AMMPositionTransfer: missing trustline policy | **`tecNO_LINE`**, do NOT auto-create | Matches Payment flow mental model. MMs expect to control trustline state. Auto-create is a footgun on accounts with TrustSetAuth. |
| AMMPositionTransfer: fee handling | **Fees follow the position**, no implicit collect | v3 semantics. Implicit collect adds a hidden side-effect and complicates accounting. LP can `AMMCollectFees` before transfer if they want to bank fees. |
| AMMPositionTransfer: authorization | **Destination must be opted-in** (`lsfDisallowAMM` or equivalent flag MUST NOT be set; standard `DepositAuth` check applies) | Prevents drive-by reserve attacks. |
| CtBinned: bin step unit | **Basis points** (uint16, e.g. 1, 10, 100) | Matches v3 fee-tier convention; intuitive for MMs. |
| CtBinned: bin price formula | `price(id) = (1 + binStep/10000)^(id - centerID)` | Standard LB / DLMM definition. |
| CtBinned: bin range bounds | **Bounded ±~221818** (matches v3 effective price range at default tick spacing) | Prevents state bloat from adversarial deep-bin spam. Configurable per pool. |
| CtBinned: bin instantiation | **Lazy** — `ltAMM_BIN` SLE created on first deposit | Avoids pre-allocating 400k+ SLEs at pool creation. |
| CtBinned: LP share model | **One MPT issuance per bin**, AMM account is issuer | Each bin's shares are fungible across LPs; transferable via standard MPT path → composability with XLS-65 vaults / LE / marketplaces for free. |
| CtBinned: fee accounting | **Per-bin accumulator** (`sfFeeGrowthBin0/1`), pro-rata to MPT holders on `AMMCollectFees` | No `feeGrowthInside/Outside` per tick — bins are simpler. |
| CtBinned: swap algorithm | **Bin-walk, constant-sum within bin** | LB / DLMM canonical. |
| Amendment gating | `featureAMMCurves` covers CL + transfer; new `featureAMMBinnedCurve` covers CtBinned | Either curve can be enabled/disabled independently via amendment. |

## Open questions — resolved

1. **MPT issuer identity:** ✅ AMM pseudo-account acts as `sfIssuer` on the per-bin `ltMPTOKEN_ISSUANCE`. Implementation: in `AMMCreate` for `CtBinned`, mint the AMM pseudo-account exactly as today; in `AMMDeposit` (binned path) on first deposit to a bin, the AMM creates the `ltMPTOKEN_ISSUANCE` keyed `(ammPseudoAccount, sequence)` and stores the resulting issuance ID on the bin SLE.
2. **Reserve accounting for bin-MPT holdings:** ✅ confirmed broader scope. Protocol-wide rule: `ltMPTOKEN` whose linked issuance's issuer is an AMM pseudo-account is **reserve-exempt** (not counted toward owner reserve). LP holding shares for 30 bins pays zero owner-reserve for those holdings. Same posture applies to any future MPT issuance with an AMM-pseudo-account issuer. Implementation: check via the AMM SLE's existing pseudo-account flag (likely `lsfAMM` or similar — verify in code). Add the rule at the owner-count tally site (likely `Transactor::view().reserve(ownerCount)` or the `AccountRoot.OwnerCount` adjuster).
3. **`sfNFTokenID` cleanup:** ✅ split into two changes:
    - **On `ltAMM_POSITION` SLE** ([ledger_entries.macro:411](include/xrpl/protocol/detail/ledger_entries.macro#L411)): **remove**. The field was reserved for a vestigial NFT-shape and is unused on the wire. Per CLAUDE.md ("if you are certain it's unused, delete it completely").
    - **As a transaction-input field** (`AMMWithdraw`, `AMMCollectFees`, new `AMMPositionTransfer`): rename `sfNFTokenID` → `sfPositionID`. The value is a position keylet hash, the name should match. Requires defining `sfPositionID` as a new SField. Low cost on this sandbox branch; cleaner naming permanently.
    - **Untouched:** `sfNFTokenID` on `ltNFTOKEN_OFFER` ([line 28](include/xrpl/protocol/detail/ledger_entries.macro#L28)) and `ltDIR_NODE` ([line 173](include/xrpl/protocol/detail/ledger_entries.macro#L173)) — those are the real NFToken feature uses.
4. **Bin-walk iteration cap:** ✅ same `kMaxIterations = 30` as the CL tick walk. Matches DLMM convention (DLMM has an explicit per-swap bin-cross cap; LB caps via EVM gas, not directly comparable). Revisit if Phase 4 sandbox observation shows 30 is too tight at common XRP/RLUSD volatilities with 1bp bin step.

---

## Phase 1 — AMMPositionTransfer (CL only)

### Phase 1a: schema cleanup — remove unused SLE field, rename tx field

- [ ] Define new `sfPositionID` SField (uint256) — find the SField definition file on this branch, add the field with the next available code
- [ ] Remove `sfNFTokenID` line from `ltAMM_POSITION` in [ledger_entries.macro:411](include/xrpl/protocol/detail/ledger_entries.macro#L411) (unused, per audit)
- [ ] Rename tx-input `sfNFTokenID` → `sfPositionID` in [AMMWithdraw.cpp](src/libxrpl/tx/transactors/dex/AMMWithdraw.cpp) (5 sites: preflight/preclaim/apply checks at lines 190, 206, 318, 434)
- [ ] Rename tx-input `sfNFTokenID` → `sfPositionID` in [AMMCollectFees.cpp](src/libxrpl/tx/transactors/dex/AMMCollectFees.cpp) (3 sites at lines 69, 100, 116 + comment)
- [ ] Update tx-format declarations for AMMWithdraw and AMMCollectFees in [transactions.macro](include/xrpl/protocol/detail/transactions.macro) to reference `sfPositionID`
- [ ] Update RPC field maps / JSON serialization if `sfNFTokenID` is referenced there for AMM tx types
- [ ] Update existing AMM tests in [src/test/app/](src/test/app/) that submit AMMWithdraw/AMMCollectFees with `NFTokenID` field name → `PositionID`
- [ ] Verify build passes; verify CL test suite passes unchanged behavior

### Phase 1b: AMMPositionTransfer transactor

- [ ] Create [src/libxrpl/tx/transactors/dex/AMMPositionTransfer.cpp](src/libxrpl/tx/transactors/dex/AMMPositionTransfer.cpp) + header
- [ ] Register transaction type in [include/xrpl/protocol/detail/transactions.macro](include/xrpl/protocol/detail/transactions.macro), gated by `featureAMMCurves`
- [ ] TX fields: `sfAccount` (source), `sfDestination`, `sfPositionID` (position keylet hash, addresses which position to transfer)
- [ ] Preflight: required fields present, source ≠ destination, amendment active
- [ ] Preclaim:
  - position SLE exists and `sfAccount == tx.account`
  - destination account exists (no `tecNO_DST` auto-create)
  - destination has trustline / authorization for both pool assets → `tecNO_LINE` if missing
  - destination not `DepositAuth`-blocked for source
- [ ] Apply:
  - Mutate position SLE: `sfAccount := destination`
  - Remove from source owner directory, insert into destination owner directory (handle `sfOwnerNode` updates on both sides)
  - Adjust source / destination reserve counts
  - **Do not** touch `sfTokensOwed0/1`, `sfFeeGrowthInsideLast0/1`, `sfPositionLiquidity`, or any tick state — pure ownership change
- [ ] Tests: [src/test/app/AMMExtended_test.cpp](src/test/app/AMMExtended_test.cpp) — add new test class
  - happy path: transfer, destination can collect / withdraw
  - source no longer owns: cannot collect / withdraw after transfer
  - missing trustline → `tecNO_LINE`
  - destination DepositAuth → `tecNO_PERMISSION`
  - source ≠ position owner → `tecNO_PERMISSION`
  - amendment disabled → `temDISABLED`
  - reserve insufficient on destination → `tecINSUFFICIENT_RESERVE`
- [ ] Verify CL swap / collect / withdraw still passes existing suite

## Phase 2 — CtBinned spec (no code, just the spec doc)

- [ ] Drop a `docs/binned-amm-spec.md` that captures, with rigorous detail:
  - State model (AMM SLE additions, `ltAMM_BIN` SLE, MPT issuance per bin)
  - Bin price math (formula, rounding, overflow bounds)
  - Deposit algorithm (single-bin, spread distribution, MPT mint)
  - Withdraw algorithm (MPT burn, pro-rata redemption)
  - Swap algorithm (bin-walk pseudo-code, edge cases: empty bin, max-iterations cap, sub-bin partial fill)
  - Fee accumulator math and pro-rata distribution
  - Invariants (sum of MPT outstanding == sum of LP liquidity claims, sum of bin reserves == AMM total reserves)
- [ ] Resolve the four open questions above; record decisions in the spec doc
- [ ] Self-review: is there a simpler design? Could LDFs be smuggled in as a future axis without rework? Should bin step also be a v3-style tier mapping? (Probably not — keep it free per-pool.)

## Phase 3 — CtBinned implementation

### 3a. Schema + amendment

- [ ] New amendment: `featureAMMBinnedCurve` in [include/xrpl/protocol/detail/features.macro](include/xrpl/protocol/detail/features.macro)
- [ ] Add `CtBinned = 3` to [include/xrpl/protocol/AMMCore.h:28-32](include/xrpl/protocol/AMMCore.h#L28)
- [ ] Add per-pool fields to AMM SLE: `sfBinStep` (uint16), `sfActiveBinID` (int32, signed)
- [ ] New SLE `ltAMM_BIN` (suggested code `0x0081` if free) in [include/xrpl/protocol/detail/ledger_entries.macro](include/xrpl/protocol/detail/ledger_entries.macro):
  ```
  {sfAMMID, sfBinID, sfReserve0, sfReserve1,
   sfFeeGrowthBin0, sfFeeGrowthBin1,
   sfMPTokenIssuanceID, sfOwnerNode,
   sfPreviousTxnID, sfPreviousTxnLgrSeq}
  ```
- [ ] Keylet helper in [src/libxrpl/protocol/Indexes.cpp](src/libxrpl/protocol/Indexes.cpp): `ammBin(ammID, binID)`
- [ ] `LedgerNameSpace::AmmBin` enum entry

### 3b. Transactor branching

- [ ] [AMMCreate.cpp](src/libxrpl/tx/transactors/dex/AMMCreate.cpp): accept `CtBinned`, validate `sfBinStep` (must be in {1, 5, 10, 25, 100} or similar curated set — TBD), set `sfActiveBinID` from initial price
- [ ] [AMMDeposit.cpp](src/libxrpl/tx/transactors/dex/AMMDeposit.cpp): branch on `sfCurveType`
  - CL path unchanged (existing tick logic)
  - Binned path: accept `sfBinID` (single bin) OR `sfBinIDs[]` + `sfDistribution[]` (spread); for each bin, instantiate `ltAMM_BIN` if missing, mint MPT to LP via `MPTokenIssuance`, update bin reserves
- [ ] [AMMWithdraw.cpp](src/libxrpl/tx/transactors/dex/AMMWithdraw.cpp): branch on `sfCurveType`
  - Binned path: burn MPT from LP, redeem proportional reserves from each bin
- [ ] [AMMCollectFees.cpp](src/libxrpl/tx/transactors/dex/AMMCollectFees.cpp): branch
  - Binned path: compute pro-rata fee share = (LP_MPT_balance / bin_MPT_outstanding) × bin_accumulator
- [ ] Payment / swap dispatch (whichever file holds the AMM offer construction — likely in `src/xrpld/app/misc/AMMUtils.cpp` or equivalent on this branch): bin-walk swap algorithm
  - Identify active bin
  - Consume constant-sum until bin reserve depleted on swap-out side
  - Advance to next bin (binID += direction)
  - Update `sfActiveBinID` on the AMM SLE
  - Honor `kMaxIterations` cap

### 3c. Fee accumulator math (binned)

- [ ] On swap-through-bin: fee carved from input, added to `sfFeeGrowthBinX` for that bin (per unit of MPT outstanding, scaled by Q64.96 or equivalent fixed-point)
- [ ] On collect: `owed = MPT_balance × (currentFeeGrowth - feeGrowthAtMintSnapshot)` — needs per-LP snapshot stored on the `ltMPTOKEN` somehow, OR pull-only via a per-LP scratch SLE; TBD in 3a spec resolution
- [ ] **Alternative simpler model worth considering in the spec**: split fees into a separate per-bin "fee reserve" pool, paid out as MPT-burnable claim; less elegant but avoids per-LP snapshot bookkeeping

### 3d. Tests

- [ ] [src/test/app/](src/test/app/) new file `AMMBinned_test.cpp`
- [ ] Single-bin deposit + withdraw round-trips
- [ ] Multi-bin spread deposit
- [ ] Swap consuming partial bin; subsequent swap continues from partial state
- [ ] Swap walking multiple bins, hits `kMaxIterations`
- [ ] Single-sided deposit above current price acts as limit ask; price moves into bin → bin auto-fills as LP intent
- [ ] Two LPs in same bin: fees split pro-rata correctly
- [ ] MPT transfer of bin shares between accounts; new holder can collect / withdraw
- [ ] CL regression: every existing CL test still passes
- [ ] Amendment off: `CtBinned` AMMCreate → `temDISABLED`

## Phase 4 — Sandbox observations

This is the "see what works" part. After both curves land:

- [ ] Build alphanet image with both amendments enabled
- [ ] Deploy a CL XRP/RLUSD pool and a Binned XRP/RLUSD pool with comparable initial liquidity
- [ ] Run a swap-volume simulation against both (synthetic order flow)
- [ ] Compare: gas / close-time impact, LP fee yield, slippage, MPT composability (try wrapping a bin share in XLS-65 vault as PoC)
- [ ] Write findings to `tasks/sandbox-findings.md`
- [ ] Re-engage MM with concrete numbers, ask Question A.6 #1 ("would you LP on a bin AMM for XRP/RLUSD?") armed with the comparison

## Removal paths (preserve both)

- **Remove CL**: drop `ltAMM_POSITION`, `AMMPositionTransfer`, tick / sqrtPrice library; toggle `featureAMMCurves` to remove tick fields from existing pools (would require a migration story — out of scope for sandbox); leave `CtConstantProduct` + `CtStableSwap` + `CtBinned`
- **Remove Bins**: drop `ltAMM_BIN`, `featureAMMBinnedCurve`, bin-path branches; leave `CtConstantProduct` + `CtConcentratedLiquidity` + `CtStableSwap`

Removal at the amendment-flag level (toggle the flag) is the path expected for sandbox iteration. Code removal only happens once we've decided which loses.

---

## Notes / non-goals

- **No ALM / vault layer in scope.** Auto-rebalancing of bin positions is a third-party concern. If MM wants ALM behavior, they run it themselves or someone else builds it on top of MPT shares.
- **No LDFs.** Bunni v2 died from custom redistribution math under rounding pressure. Fixed bins explicitly.
- **No dynamic fees per bin (yet).** Static fee per pool, set at creation. Dynamic fees (DLMM volatility accumulator) can be a follow-up axis.
- **No `AMMBinTransfer` transactor.** Bin shares are MPTs — transfer via the standard MPT path. This is the entire point of the MPT integration.

## Review section

### Phase 1a — schema cleanup ✅

- Defined `sfPositionID` (UINT256, code 42).
- Removed unused `sfNFTokenID` from `ltAMM_POSITION`.
- Renamed tx-input field `sfNFTokenID → sfPositionID` in AMMWithdraw, AMMCollectFees, and the new AMMPositionTransfer.
- Updated tx format declarations and AMMCurves test to use the renamed field.
- Build: clean. Regression: existing AMMCurves tests still pass (42 cases / 6230 asserts).

### Phase 1b — AMMPositionTransfer transactor ✅

- New transactor at [src/libxrpl/tx/transactors/dex/AMMPositionTransfer.cpp](../src/libxrpl/tx/transactors/dex/AMMPositionTransfer.cpp) + [header](../include/xrpl/tx/transactors/dex/AMMPositionTransfer.h).
- Registered as `ttAMM_POSITION_TRANSFER = 86`, gated behind `featureAMMCurves`.
- Implements full ownership transfer: source must own, destination must exist, DepositAuth honored, destination reserve checked, owner directories updated on both sides. Fees and tick state untouched (transfer is opacity-preserving).
- Tests at [src/test/app/AMMPositionTransfer_test.cpp](../src/test/app/AMMPositionTransfer_test.cpp): 9 cases / 511 asserts / 0 failures. Covers happy path, source-can't-operate-after-transfer, non-owner rejected, missing position (`tecNO_ENTRY`), missing destination (`tecNO_DST`), DepositAuth blocks (`tecNO_PERMISSION`), source==destination (`temREDUNDANT`), malformed (`temMALFORMED`), amendment disabled (`temDISABLED`).

### Phase 2 — Binned-AMM design spec ✅

[docs/binned-amm-spec.md](../docs/binned-amm-spec.md) — state model, math, transactor changes, invariants, scope cuts, removal path. Open `[TODO-impl]` items listed for Phase 4 resolution.

### Phase 3 — CtBinned (minimum-viable sandbox) ✅

Shipped in this branch:
- New amendment `featureAMMBinnedCurve` ([features.macro](../include/xrpl/protocol/detail/features.macro)).
- `CtBinned = 3` curve type ([AMMCore.h](../include/xrpl/protocol/AMMCore.h)) + curated `validBinSteps = {1, 5, 10, 25, 100}` bp, `minBinID/maxBinID = ±221818` bounds.
- New sfields: `sfBinStep` (UINT16/26), `sfBinID` (INT32/6), `sfActiveBinID` (INT32/7), `sfReserve0/1` (AMOUNT/34/35), `sfFeeGrowthBin0/1` (NUMBER/24/25).
- New SLE `ltAMM_BIN` (0x0085) + `keylet::ammBin(ammID, binID)` with offset-binary encoding for SHAMap range-walk order.
- AMM SLE accepts `sfBinStep` / `sfActiveBinID` as optional fields.
- `AMMCreate` accepts `CtBinned`: validates amendment + binStep, initializes `sfActiveBinID = 0`, skips initial reserve transfer (parallel to CL), skips LP-token mint.
- `AMMInvariant.finalizeCreate` treats Binned the same as CL (zero-balance creation allowed).
- Tests at [src/test/app/AMMBinned_test.cpp](../src/test/app/AMMBinned_test.cpp): 5 cases / 606 asserts / 0 failures. Covers happy path across all 5 bin steps, missing binStep, invalid binStep, amendment-disabled, coexistence with CL on same asset pair (distinct keylets).

**Removability:** disable `featureAMMBinnedCurve` → new `AMMCreate(CtBinned)` returns `temDISABLED`; existing pools sit idle. Code removal is a clean reverse of the diff (no entanglement with CL paths beyond shared "zero-balance create" exemption).

### Phase 4 — Deposit / Withdraw (SHIPPED)

**Design decision after attempting MPT integration:** dropped the per-bin MPT issuance for the sandbox in favor of a `ltAMM_BIN_HOLDING` SLE keyed by `(ammID, owner, binID)`. This trades the fungible/composable MPT-share story for a much simpler implementation that still answers the LP-UX question. Phase 5 will migrate to MPT shares for composability.

**Phase 4a — `AMMDeposit` binned path** ✅
- `sfBinID` added to AMMDeposit tx format.
- Preflight: requires `sfBinID` + `tfTwoAsset` for `CtBinned`; validates bounds.
- Apply: lazy bin SLE creation; lazy LP-holding SLE creation; share computation (first deposit = `min(amount0, amount1)` as drops; subsequent = proportional); reserve transfer from LP to AMM; bin reserves + outstanding shares update.
- AMMInvariant updated to accept zero-balance and zero-LPT for `CtBinned` (parallel to CL).

**Phase 4b — `AMMWithdraw` binned path** ✅
- `sfBinID` added to AMMWithdraw tx format; `tfWithdrawAll` only (partial-withdraw deferred).
- Preflight + preclaim validate LP holds the bin.
- Apply: proportional reserve redemption from bin; reserves sent to LP; holding SLE deleted (owner-count -1); bin SLE deleted if fully drained (sandbox: no lingering empty-bin SLEs).
- Skipped the LP-token verify-and-adjust path for binned (which CL already skips).

**Phase 4b tests** ✅
- `testDepositCreatesBin` — bin SLE + holding SLE shapes are correct.
- `testDepositMissingBinID` — `temMALFORMED` without `sfBinID`.
- `testDepositWithdrawRoundTrip` — alice deposits 50/50 into bin 5, withdraws all, balance restored, bin SLE deleted, holding SLE deleted.
- `testMultiLPSameBinSharesPropotional` — two LPs depositing 100/100 into same bin get equal shares; bin's `sfOutstandingAmount == sum(shares)`.

### Phase 4d — Swap (SHIPPED — multi-bin walk)

**Sandbox scope:** full multi-bin walk on swap. Active bin depleted → walk to next bin in swap direction, repeat until input fully consumed or `kMaxBinIters = 30` reached. `sfActiveBinID` advances to wherever the walk lands.

- New `BinnedCurve` class in [AMMCurve.cpp](../src/libxrpl/ledger/helpers/AMMCurve.cpp): implements `validateParams`, `initialLPTokens` (zero — parallel to CL), `swapIn`, `swapOut`, `spotPrice`, `checkInvariant`, `applySwap`.
- Bin price computed via fast exponentiation: `price(id) = (1 + binStep/10000)^id`.
- `swapIn`: constant-sum at bin price. `dy = dx_after_fee * P` (if asset0 in) or `dx_after_fee / P` (if asset1 in). Capped at available output reserve.
- `applySwap`: mutates active bin SLE — increments input-side reserve, decrements output-side reserve.
- Registered in `getCurve()` under `featureAMMBinnedCurve`.
- `BookStep` updated to treat `CtBinned` as never-empty by LP-token signal (parallel to CL — falls through to poolIn/poolOut balance check).
- `AMMDeposit` now normalises tx fields to canonical `(asset0, asset1)` ordering so bin reserves are stored consistently with the AMM SLE's `sfAsset`/`sfAsset2` ordering. Without this, `applySwap` couldn't match input asset to the right reserve side.

**Tests added:**
- `testSwapAtUnitPrice` — single-bin swap at bin 0 (price=1, zero fee). Exact 100/100 round-trip.
- `testMultiBinWalkOnSwap` — pool with 100 USD / 100 EUR liquidity in each of bins {0, 1, 2}. Bob asks for 250 USD; swap walks past bin 0 into bin 1/2. Verifies `sfActiveBinID > 0` after swap and bin 0's `sfReserve1` (USD) is drained to ~0.

**New walk helper:** `BinnedCurve::walkBins` is a static, read-only walk that returns `(totalDx, totalDy, steps[], finalActiveBinID)`. Both `swapIn` (quoting) and `applySwap` (settlement) call it; identical inputs produce identical walks, so settled state matches the quote.

**Invariant update:** the `finalizeDEX` invariant previously rejected any AMM SLE mutation on swap for non-CL curves. CtBinned now joins CL on the allowed-mutation list (advancing `sfActiveBinID`); aggregate `sfLPTokenBalance` must remain zero for both.

### Phase 4 — DEFERRED (Phase 5+)

**Phase 4c — `AMMCollectFees` binned path** — fees currently stay in the bin's reserves implicitly (the AMM keeps the trading-fee portion of each swap as added reserve). A proper accumulator + per-LP snapshot collection path is Phase 5.

**Phase 4e — Reserve-exemption rule for AMM-issued MPTs** — moot for the holding-SLE model; becomes relevant when Phase 5 migrates to MPT shares.

**Phase 5 — MPT migration** — replace `ltAMM_BIN_HOLDING` with per-bin `ltMPTOKEN_ISSUANCE` (AMM as issuer) so bin shares become fungible and transferable. Unlocks composability with XLS-65 vaults / lending. Includes Phase 4e (reserve exemption for AMM-issued MPTs) as a sub-deliverable.

**Partial-withdrawal from a bin** — sandbox is full-burn only; partial would require a shares-to-burn parameter on the withdraw tx, prorated reserve redemption, and updated invariants.

**Sub-bin slippage / swap-out walk** — `swapOut` (compute required input for a desired output) currently does single-bin math; only `swapIn` walks bins. For most Payment paths swapIn is sufficient, but a multi-bin walk for swapOut would close the symmetry.

### Test totals at branch end-state

| Suite | Cases | Asserts | Failures |
|---|---:|---:|---:|
| AMM (existing) | 92 | 90,105 | 0 |
| AMMCurves (existing CL + curves) | 42 | 6,230 | 0 |
| AMMPositionTransfer (new) | 9 | 511 | 0 |
| AMMBinned (new — create + deposit/withdraw + multi-LP + payment-routed swap + multi-bin walk) | 11 | 1,001 | 0 |

All four suites pass under `featureAMMCurves` and `featureAMMBinnedCurve` enabled.

### What can be done end-to-end on this branch

- **CL pool**: create, deposit, withdraw, swap (via payment engine), collect fees, transfer position to another account.
- **Binned pool**: create, deposit into a bin, multi-LP deposits to same bin share proportionally, withdraw all (full burn), **swap via Payment routing — including multi-bin walks with `sfActiveBinID` advancement when bins deplete**.
- **CtConstantProduct + CtStableSwap**: unchanged from baseline.

### What's NOT possible on this branch (Phase 5)

- **Collect fees from a binned position** — fees currently accumulate in bin reserves; no explicit per-LP collect path.
- **Transfer bin shares between LPs** — requires MPT migration (Phase 5).
- **Compose bin shares into XLS-65 vaults / lending markets** — requires MPT migration.
- **Partial withdrawal from a bin** — sandbox is full-burn only.
- **Multi-bin swapOut walk** — `swapOut` is single-bin only (swapIn walks correctly).

# Vault Fixed Precision — Design Notes

Status: draft, in progress.

## 1. Problem

Vault rounding precision is _derived_ from `AssetsTotal`, not _declared_. Amounts have a fixed budget
of 16 significant digits; as `AssetsTotal` grows, less budget remains for the fraction, so effective
precision shrinks as the Vault grows.

Consequences:

1. **Unpredictable** — the same nominal payment rounds differently depending on Vault size at the
   moment, unrelated to the transaction.
2. **Precision degrades with success** — bigger, more successful Vaults get sloppier accounting.
3. **Cascading complexity** — Loan precision derives from Vault precision, a moving target.
4. **Non-deterministic edge cases** — dust/precision-loss depends on Vault size at operation time,
   not fixed rules.
5. **No stable contract** — integrators can't know rounding precision ahead of time without
   inspecting live (and staleable) Vault state.

### 1.1 The ledger & custody constraint

- **SAV** (internal accounting ledger for total assets/shares) and **Pseudo-account** (the custody
  account actually holding the asset) both share the same **16-significant-digit `STAmount`
  ceiling**.
- That budget splits between integer and fractional parts. At high valuation (e.g. $100B) with fine
  precision (e.g. 6 decimals), an amount like $1.234567 may not fit — a representation limit, not
  policy.

### 1.2 Why a naive fixed precision isn't enough

- Fixing one precision value at creation and applying it everywhere — including internal running
  totals — still hits the 16-digit wall: a repayment's interest/fee component can push `AssetsTotal`
  past what a fixed precision can represent. Rejecting the transaction blocks a borrower from closing
  debt for reasons unrelated to their loan.
- **Asymmetry:** optional operations (e.g. a deposit) can tolerating rounding to Vault precision; mandatory ones
  (a repayment closing debt) cannot — dropping the remainder breaks the assets/shares identity or
  shortchanges what's owed.

**Requirement — two things that must not be conflated:**

- A fixed, design-time precision ceiling for client-facing operations, set once at Vault creation —
  a stable contract for integrators.
- That ceiling must _not_ bound the Vault's internal running totals, which grow unbounded and will
  eventually collide with the 16-digit ceiling if forced into the same representation as a single
  transaction amount.

## 2. Summary of the solution

New/changed fields (full detail in **Technical Details**, below):

- **`sfPrecision`** (`ltVAULT`, new) — rounding ceiling, set once at creation, immutable. Effective
  scale = finer of the ceiling and whatever the 16-digit budget allows given `AssetsTotal`'s size;
  degrades only downward as the Vault grows.
- **`sfDustAccount`** (`ltVAULT`, new) — a Dust Pseudo-account per IOU Vault. Catches the remainder a
  _mandatory_ repayment can't represent at the current effective scale. Representable portion
  settles normally; the rest transfers into this account, whose real balance is the record (no
  separate counter to keep in sync). Swept back into ordinary custody once representable again.
  Nothing silently dropped; no rejection for reasons unrelated to the loan.
- **`sfLoanScale`** (`ltLOAN`, existing field, existing formula) — a Loan's own precision, fixed at
  issuance, immutable: the coarser of what the Vault can currently represent and what the Loan's own
  lifetime value (principal + future interest) independently needs. What changes: the Vault-side
  floor becomes `effective(AssetsTotal)` (bounded by `sfPrecision`) instead of today's raw scale, and
  `LoanAccept` re-validates the already-fixed value against it.

**Behavior by operation:**

- Deposit / Withdraw / `VaultClawback` — round to current effective scale.
- Loan funding — same rule; this is the moment `loan.LoanScale` gets fixed.
- Loan repayment — mandatory, two-stage: round to `loan.LoanScale`, then split between main custody
  and `DustAccount` if the Vault has grown coarser since funding.
- Final withdrawal (drains the Vault to zero) — pays out all remaining `Dust`, since no other
  shareholder is left to divide it.

**Net effect:** a fixed, declared precision contract for client-facing operations, decoupled from the
Vault's unbounded internal totals.

## 3. Technical details

### 3.1 Fields

| Field           | Object           | Notes                                | Set                                        | Mutable                                                                      | Purpose                                                                                                                                                                                   |
| --------------- | ---------------- | ------------------------------------ | ------------------------------------------ | ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `sfPrecision`   | `ltVAULT`        | New                                  | `VaultCreate`                              | No — immutable (assertion only, not yet enforced; Appendix → Open questions) | Design-time rounding ceiling — see _Precision ceiling_ below.                                                                                                                             |
| `sfDustAccount` | `ltVAULT`        | New, `SoeOptional` — IOU Vaults only | `VaultCreate`                              | No — set once, never reassigned                                              | Points at the Vault's Dust Pseudo-account. Its real balance, not a separate ledger field, is the authoritative amount of dust — see _Dust: error accumulation and custody routing_ below. |
| `sfVaultDustID` | `ltACCOUNT_ROOT` | New designator field                 | `VaultCreate` (on the Dust Pseudo-account) | No                                                                           | Reverse link distinguishing the Dust Pseudo-account from the main one (`sfVaultID`) — two `ACCOUNT_ROOT`s for the same Vault must stay distinguishable by field.                          |

**Note:** `sfScale` (`ltVAULT`, existing) is the fixed assets↔shares conversion exponent already used
in `VaultHelpers.cpp` — unrelated to `sfPrecision` despite the similar naming; the two must not be
conflated.

### 3.2 Algorithm

#### Precision ceiling: `effective(AssetsTotal)`

```
effective(AssetsTotal) = min(sfPrecision, maxPrecisionRepresentableGiven(AssetsTotal, 16-sig-digit budget))
```

- Degrades only downward from the ceiling — the Vault never rounds finer than `sfPrecision`.
- Deterministic contract: the finest precision ever used is computable from `AssetsTotal` and
  `sfPrecision`, not an opaque derivation.
- Doesn't resolve the 16-digit collision on internal running totals — that's the Dust mechanism,
  next.

#### Dust: error accumulation and custody routing

- **Purpose:** let a mandatory operation succeed in full when `sfPrecision` can't currently be
  honored, without truncating value or rejecting the tx.
- **Ledger tracking:** no separate accumulator field. `Dust` _is_ the Dust Pseudo-account's real
  balance (`accountHolds(vault.sle[sfDustAccount], asset)`), read directly wherever "how much dust
  exists right now" is needed. A `sfDust` counter on `ltVAULT` would be redundant with the real
  balance, and dangerously so: every consuming path would have to decrement it in lockstep, and it's
  easy to miss one — e.g. the sweep path.
- **Custody routing:** representable portion → main Pseudo-account, as normal; unrepresentable
  fraction → the dedicated Dust Pseudo-account for that Vault.
- **The Sweep:** once dust custody accumulates a whole representable unit, transfer it cleanly into
  main custody.
- **Scope:** invoked only where the case-by-case rules below say so — not a general-purpose
  remainder catcher for ordinary rounding.

**`Dust` must count in both NAV/liquidity totals, not just one:**

```
totalAssetsForPricing = AssetsTotal    + Dust
totalAssetsAvailable   = AssetsAvailable + Dust
```

- `SettleAdd` (_Core primitives_, below) only adds the _settled_ portion into `AssetsTotal`; the
  dust portion sits in the Dust Pseudo-account.
- `Dust` is real, liquid money the Vault fully controls, never lent to anyone — exactly what
  `AssetsAvailable` tracks. Omitting it there understates how much the Vault can actually pay out
  right now.
- `AssetsTotal`/`AssetsAvailable` are each bounded by an absolute `STAmount` ceiling, independent of
  `sfPrecision`. Once pinned there, neither accepts further inflow at _any_ scale — `Dust`, in its
  own account with its own headroom, becomes a **fallover buffer** past that point. Without folding
  it into both sums, that regime is invisible: claims and liquidity stop reflecting real inflows once
  `AssetsTotal` maxes out, even as the Vault keeps receiving (and owing) more.

**Why this doesn't overpay anyone:**

- **Day to day, `Dust` is mathematically real but numerically invisible.** It only exists because
  it's smaller than one representable unit at the current `effective(AssetsTotal)` — that's the
  definition of dust. Including it in either total changes the result by less than one representable
  unit, which rounds away in the same settlement step (`round(amount_requested,
effective(AssetsTotal))`, _Case by case_ below) that produces the amount actually paid. For a
  healthy Vault it never changes what a depositor or withdrawer receives.
- **Once material, it's swept before it matters.** `MaybeSweep` (_Core primitives_, below) runs after
  every fund-moving operation and folds any dust past one representable unit straight into
  `AssetsTotal`/`AssetsAvailable`, where it becomes ordinary, fully-backed Vault money. `Dust` can
  only sit at an economically material level transiently, between crossing that threshold and the
  next sweep.
- So both totals above are correct to include even though nothing pays out of the Dust Pseudo-account
  directly in the common case — the inclusion only matters near the absolute `STAmount` ceiling,
  where `AssetsTotal` itself can't grow further and `Dust`'s headroom is what keeps the Vault's
  reported totals honest.

#### Case by case: how rounding actually works

##### User fund movement — Deposit, Withdraw, `VaultClawback`

Round directly to the effective scale, no `sfPrecision`-first attempt:

```
amount_settled = round(amount_requested, effective(AssetsTotal))
```

- Eliminates Case 1 (_Core primitives_, below) — `dustFromExcess` is always `0` for these callers.
- Does **not** eliminate Case 2: a Deposit that pushes `AssetsTotal` across a magnitude boundary can
  still coarsen the effective scale before `SettleAdd` applies it, producing `dustFromCoarsening` the
  same way a repayment can — rare (only at a growth boundary), not structurally impossible. Deposit
  still goes through `SettleAdd`/`AddAssetsToVault` like every other operation, not a separate
  dust-free path.

##### Loan issuance (funding) — Vault → Loan

- Funding is a Vault-initiated disbursement, structurally a withdrawal.
  Rounds directly to the Vault's _current_ effective scale, no dust.
- Consequence: **the Loan's own precision is fixed once, at funding time**, to the coarser of two
  independent floors — one knowable only from the Vault's current state, the other knowable in full
  from the Loan's own terms, before a single payment is made:

```
loan.scale = max(
    effective(AssetsTotal at funding time),      # Vault-side floor: what the Vault can
                                                   # represent right now
    scale(totalValueOutstanding, asset)           # Loan-side floor: what this Loan's own
                                                   # projected lifetime value needs
)
```

- The Loan-side floor is `scale(principal + all future interest, asset)` — Equation (30) of the
  XLS-66 spec, `totalValueOutstanding = periodicPayment × paymentsRemaining`. `computeLoanProperties`
  (`LendingHelpers.cpp`) already computes exactly this today, as
  `loanScale = max(minimumScale, amount.exponent())`, where `minimumScale` is the Vault-side floor
  passed in by the caller. Both floors matter — they aren't interchangeable:
  - **Loan-side is deterministic at creation.** Interest rate, payment interval, and payment count
    are all fixed at `LoanSet`, so `totalValueOutstanding` — and its natural scale — is computable
    synchronously. A modest principal at a high rate over a long term can have
    `principal + total future interest` land at a materially larger order of magnitude than the
    Vault's current `AssetsTotal`, even though the principal itself came from (and is bounded by) the
    Vault — nothing bounds future interest by the Vault's present size, especially under cash-basis
    accounting, where `AssetsTotal` doesn't recognize interest until it's actually paid (_Loan
    repayment_, below).
  - **Vault-side is not knowable in advance.** How much _more_ the Vault's effective scale might
    degrade over the Loan's life — from other loans, deposits, sweeps, other borrowers' interest
    accruing — depends on the whole Vault's future activity, not this Loan's terms. That's why
    `loan.LoanScale` must be pinned immutably at funding at all: if it were knowable in advance like
    the Loan-side floor, there'd be no need to freeze it.
- Stored in the existing `sfLoanScale` field on `ltLOAN`, set in `LoanSet.cpp` exactly as today —
  `computeLoanProperties` already computes this two-floor `max` at creation. What's new: the
  Vault-side floor becomes `effective(AssetsTotal)` (bounded by `sfPrecision`) instead of today's
  raw, unbounded `getAssetsTotalScale`; and `LoanAccept` gains a re-validation step it doesn't have
  today (see _Pending Loans_, below) — the already-fixed value is checked, never recomputed or
  re-set, at acceptance.
- Immutable thereafter, same as `sfPrecision` is for the Vault. Operating at finer precision than
  actually funded would assert false precision across all future accounting.

**Worked example.** Vault `sfPrecision` = 6. Loan A funds while `AssetsTotal` is small (effective
scale 6) and its own `totalValueOutstanding` also fits scale 6 (short term, low rate) →
`loan.scale` = 6, Vault-side floor binds. `AssetsTotal` later degrades effective scale to 5; Loan
B funds then, with a long term and high rate whose own `totalValueOutstanding` needs scale 4 →
`loan.scale` = 4 — Loan B's own terms bind, not the Vault's size. Same Vault, two Loans, two
fixed precisions, set by whichever floor is coarser at each funding moment.

###### Pending Loans: acceptance can straddle a scale change

- Issuance is two steps: **create** (terms agreed, funds not moved) and **accept** (funds move).
  "Funding time" above is really acceptance time. Only the Vault-side floor can move between create
  and accept — the Loan-side floor is already fixed at create time and doesn't change while pending.
- Between create and accept, `AssetsTotal` can degrade the effective scale enough that the agreed
  amount is no longer representable:
  - Silently re-round at acceptance — rejected: mutates agreed terms invisibly.
  - **Reject the acceptance, forcing re-creation/re-quote — chosen.** Worse for the borrower
    short-term, but fails loudly instead of silently mutating terms.
- So a pending Loan's amount is re-validated against `effective(AssetsTotal)` at acceptance; on
  failure, acceptance is rejected outright, never adjusted.

**Griefable, not just naturally rare — accepted anyway.** `effective(AssetsTotal)` is a public,
deterministic function of `AssetsTotal`, and a pending Loan's agreed principal/scale is visible
on-ledger from creation. Anyone able to deposit into the Vault can compute exactly how much more
`AssetsTotal` needs to grow to cross the next magnitude boundary, deposit that amount right before
the borrower's `LoanAccept`, and force the acceptance to fail — at essentially no cost, since the
deposit converts to shares the attacker still holds and can withdraw right back out. A motivated
party could grief a specific Loan's acceptance repeatedly, forcing the borrower to eat a re-quote
each time, for reasons unrelated to the Loan's own terms.

Unlike the address-squatting vector (Appendix → _Address predictability is a griefing vector under
lazy creation_), which costs real XRP per attempt and is single-ledger-use, this one is cheaper and
repeatable — but **accepted anyway**, on the same cost/benefit basis as ordinary
transaction-propagation risk elsewhere: the failure mode is a rejected acceptance and a forced
re-quote, not a loss of funds or a stuck Vault. Tolerating one boundary crossing (e.g. via the Dust
mechanism, the way repayment does) is real added complexity for an outcome that's merely annoying,
not unsafe. Flagged here rather than fixed.

##### Loan repayment — Loan → Vault

**Mandatory operation.** Two-stage:

1. Round to the Loan's own fixed `loan.scale` (above) — may already be coarser than the Vault's
   current `sfPrecision` ceiling. Applies to the combined cash figure that actually settles —
   principal plus interest together, not principal alone; `LoanPay.cpp` already rounds
   `principalPaid + interestPaid` as one figure before this point today.
2. Check representability at the Vault's _current_ `effective(AssetsTotal)`:
   - `loan.scale <= effective(AssetsTotal)`: settles in full, no dust.
   - Otherwise: split. Representable portion → main custody; delta → the Dust Pseudo-account.

The common case for Case 1 dust (_Core primitives_, below) — a loan's fixed precision going stale as
the Vault has grown since funding. The borrower's payment is always collected in full; the split is
a custody-routing detail, invisible to the borrower's accounting.

**Two separate parameters drive settlement, not one:** `AddAssetsToVault(view, vault, from, amount,
assetsTotalDelta, j)`:

- `amount` — the real cash settling into custody (principal + interest combined, for a repayment).
  Goes through the full dust/precision-routed path (_Core primitives_, below): `AssetsAvailable`
  moves by whatever actually lands (`settled`); any truncated remainder routes to `Dust`.
- `assetsTotalDelta` — how much `vault.AssetsTotal` itself moves. Supplied by the caller, defaulting
  to `settled` when not overridden — correct for Deposit (above) and `MaybeSweep`'s sweep leg (_Core
  primitives_, below), where landed custody genuinely _is_ new value to `AssetsTotal`. A cash-basis
  repayment is the one caller that must override this default, passing `interestPaid` instead. When
  overridden, `assetsTotalDelta` gets its _own_ two-stage truncation, the same shape `amount` gets
  (_Core primitives_, below) — it is dust-routed, just against what `AssetsTotal` itself is about to
  gain, not against `amount`.

**Why the override is required, not optional:** cash-basis accounting
([#7817](https://github.com/XRPLF/rippled/pull/7817), `LendingProtocolV1_1`) tracks `AssetsTotal` as
**principal only** — a receivable recognized once, at origination; interest is recognized **only
once paid**. Principal cash returning at repayment must _not_ grow `AssetsTotal` again — it's the
same receivable converting from loan back to cash, not new value — whereas `settled` (what
`SettleAdd` defaults `assetsTotalDelta` to) is principal _and_ interest combined. Defaulting
`AssetsTotal` to `settled` here, as this doc's algorithm originally did unconditionally, double-counts
principal into `AssetsTotal` on every cash-basis repayment — a real bug, not just an accounting
simplification, and why the override is an explicit, required parameter rather than an afterthought.

**Resolved: `assetsTotalDelta` needs its own dust routing, tied to `settled` — it can't just be
skipped.** Dust/precision routing exists to protect `AssetsTotal` additions from exceeding the
16-sig-digit ceiling. Under cash-basis, `assetsTotalDelta` (`interestPaid`) is the only thing that
grows `AssetsTotal` on repayment, so it's the value that actually needs the same two-stage truncation
`SettleAdd` already does for `amount` — and for a large enough Vault, this isn't a remote edge case:
a Vault near the top of its representable range can round an entire interest payment down to zero
when added to `AssetsTotal` directly. If that shortfall were simply dropped, it would vanish
completely — real cash that landed in custody (as part of `settled`) but is recognized nowhere,
neither in `AssetsTotal` nor in `Dust`. That's a strictly worse failure than any dust case `SettleAdd`
already handles, because there'd be no bucket account for it and nothing to sweep later.

The fix: give `assetsTotalDelta` the same Case 1 + Case 2 truncation `amount` gets, but keyed to what
`AssetsTotal` actually gains (`vault.AssetsTotal + assetsTotalDelta`), not what custody gains
(`vault.AssetsTotal + settled`) — those differ precisely when the override applies. Whatever
`assetsTotalDelta` can't represent at that scale is real value that already landed in custody and
must move out of `settled` and into `dust`, keeping `settled + dust = amount` intact while ensuring
`AssetsAvailable` never silently holds more than `AssetsTotal` and `Dust` together account for. See
_Core primitives_, below, for the worked-through pseudocode.

#### Core primitives

Two shared primitives, invoked by every fund-moving operation above instead of each implementing its
own settlement/dust logic: **add assets to Vault**, **remove assets from Vault**.

##### Add assets to Vault

- **Precondition:** the caller has already rounded `amount` — the cash settlement figure — to _some_
  target scale (Deposit → `effective(AssetsTotal)`; repayment → `loan.scale`, applied to
  principal+interest combined). This algorithm decides how much of `amount` custody
  (`AssetsAvailable`) can absorb right now and routes the rest to `Dust`; `AssetsTotal` moves
  separately, driven by the caller-supplied `assetsTotalDelta` (_Loan repayment_, above) — which, when
  overridden, gets routed through the same truncation as `amount`, not skipped.
- **Two independent, composable dust sources**, not exclusive cases — applied to `amount` always,
  and to an overridden `assetsTotalDelta` too:
  1. **Pre-existing excess** — the value already exceeds the current effective scale
     `scaleBeforeAdd` (e.g. `loan.scale > scaleBeforeAdd`). Detectable before touching `AssetsTotal`.
  2. **Addition-induced coarsening** — even after trimming to `scaleBeforeAdd`, adding the result can
     cross a magnitude boundary and coarsen the scale further (`scaleAfterAdd =
effective(AssetsTotal + assetsTotalDelta) < scaleBeforeAdd` — using what `AssetsTotal` actually
     gains, which is `settled` by default but can differ from it when overridden).
  - Both can fire on one call: trim against `scaleBeforeAdd`, then trim the result again against
    `scaleAfterAdd`. Converges in one extra pass — the second truncation can't reduce a value below
    what it already trimmed to, so it can't coarsen below `scaleAfterAdd`.
- `SettleAdd` is the truncation/bookkeeping core and never sweeps; `AddAssetsToVault` wraps it with
  an opportunistic sweep. Kept separate to avoid `MaybeSweep` recursively triggering itself (below).
- Real fund movement goes through `accountSend`/`accountSendMulti` (`TokenHelpers.h`), same as every
  existing transactor — `LoanPay.cpp` already does this exact split-destination move in one call,
  settling to the Vault's Pseudo-account and the broker's payee simultaneously. `SettleAdd`'s
  main/dust split is the same shape: one sender, up to two destinations, atomic.

```
function SettleAdd(view, vault, from, amount, assetsTotalDelta, j) -> Expected<Number, TER>:
    # amount already rounded to caller's target scale; may or may not
    # fit the Vault's current effective scale. Never triggers a Sweep.
    #
    # assetsTotalDelta is the amount by which vault.AssetsTotal moves.
    # It defaults to `settled` (computed below) when the caller omits it —
    # correct for Deposit and MaybeSweep's sweep leg, where landed custody
    # genuinely is new value to AssetsTotal. A cash-basis repayment is the
    # one caller that MUST override this default, passing `interestPaid`
    # instead — `settled` there is principal+interest combined, and
    # principal must not double-count into AssetsTotal.
    #
    # When overridden, assetsTotalDelta is NOT exempt from dust routing —
    # it gets the same Case 1 + Case 2 truncation `amount` gets, just
    # keyed to what AssetsTotal itself is about to gain. Anything it
    # can't represent is real cash that already landed in custody (it's
    # part of `amount`) but can't be recognized in AssetsTotal yet; that
    # shortfall moves out of `settled` and into `dust`, so it's tracked
    # and sweepable rather than silently vanishing into an ordinary,
    # untracked STAmount rounding on the `+=` below.

    scaleBeforeAdd = effective(vault.AssetsTotal)
    settledAfterExcess, dustFromExcess = truncate(amount, scaleBeforeAdd)   # Case 1 (amount)

    overridden = assetsTotalDelta is not None
    if overridden:
        deltaAfterExcess, deltaDustFromExcess = truncate(assetsTotalDelta, scaleBeforeAdd)   # Case 1 (assetsTotalDelta)
    else:
        deltaAfterExcess, deltaDustFromExcess = settledAfterExcess, 0   # default case: tracks settledAfterExcess exactly

    # Keyed to what AssetsTotal actually gains (deltaAfterExcess), not what
    # custody gains (settledAfterExcess) — those differ exactly when overridden.
    assetsTotalAfterAdd = vault.AssetsTotal + deltaAfterExcess
    scaleAfterAdd = effective(assetsTotalAfterAdd)

    if scaleAfterAdd < scaleBeforeAdd:
        settledAfterCoarsening, dustFromCoarsening =
            truncate(settledAfterExcess, scaleAfterAdd)                    # Case 2 (amount)
        if overridden:
            deltaAfterCoarsening, deltaDustFromCoarsening =
                truncate(deltaAfterExcess, scaleAfterAdd)                  # Case 2 (assetsTotalDelta)
        else:
            deltaAfterCoarsening, deltaDustFromCoarsening = settledAfterCoarsening, 0
    else:
        settledAfterCoarsening, dustFromCoarsening = settledAfterExcess, 0
        deltaAfterCoarsening, deltaDustFromCoarsening = deltaAfterExcess, 0

    deltaShortfall = deltaDustFromExcess + deltaDustFromCoarsening   # always 0 in the default case
    settled = settledAfterCoarsening - deltaShortfall   # custody shrinks by whatever AssetsTotal
                                                          # couldn't recognize
    dust = dustFromExcess + dustFromCoarsening + deltaShortfall   # ...and that shortfall joins dust instead
    assetsTotalDelta = deltaAfterCoarsening   # equals settledAfterCoarsening in the default case

    # Move the real asset first; bookkeeping only applies once it succeeds
    # (mirrors existing transactors, e.g. VaultDeposit.cpp).
    if dust > 0:
        ter = accountSendMulti(
            view, from, vault.asset,
            {{vault.sle[sfAccount], settled}, {vault.sle[sfDustAccount], dust}},
            j, WaiveTransferFee::Yes)
    else:
        ter = accountSend(
            view, from, vault.sle[sfAccount], settled, j, {}, WaiveTransferFee::Yes)
    if ter is not tesSUCCESS:
        return Unexpected(ter)

    vault.AssetsAvailable += settled   # real custody landed, dust-routed
    # No separate Dust bookkeeping here — the accountSendMulti call above
    # already moved `dust` into vault.sle[sfDustAccount]; that account's
    # own real balance is the record, nothing else to update.
    vault.AssetsTotal += assetsTotalDelta   # sole driver of AssetsTotal — NOT `settled`

    return settled   # callers needing the actually-landed amount (e.g.
                      # share issuance math) must use this, not `amount`


function AddAssetsToVault(view, vault, from, amount, assetsTotalDelta, j) -> Expected<Number, TER>:
    settledResult = SettleAdd(view, vault, from, amount, assetsTotalDelta, j)
    if settledResult is error: return settledResult
    MaybeSweep(view, vault, j)   # below — cheap no-op if dust is insufficient
    return settledResult
```

Worked example (the case this closes): a Vault near the top of its representable range —
`effective(AssetsTotal)` = whole units only (`scaleBeforeAdd = 0`) — receives a repayment of
principal 100 plus 37 cents of interest (`amount = 100.37`, `assetsTotalDelta = 0.37`). Under the
old, un-truncated `assetsTotalDelta`: `settled = 100`, `dust = 0.37` (from `amount`'s own Case 1),
and `vault.AssetsTotal += 0.37` would silently round to `+= 0` on the underlying `STAmount` add — the
37 cents is real cash sitting in `AssetsAvailable`, recognized nowhere. With the fix:
`deltaAfterExcess, deltaDustFromExcess = truncate(0.37, scaleBeforeAdd=0) = (0, 0.37)`; no Case 2
boundary crossing, so `deltaShortfall = 0.37`. Final: `settled = 100 - 0.37 = 99.63`, `dust = 0.37 +
0.37 = 0.74`, `assetsTotalDelta = 0`. Check: `settled + dust = 99.63 + 0.74 = 100.37 = amount`. The
interest doesn't vanish — it joins `Dust`, tracked and sweepable, exactly like principal's own excess
already is.

**Open edge case, not resolved here:** `deltaShortfall` is bounded to less than one representable
unit per truncation stage, same as any other dust source — but `settledAfterCoarsening` could in
principle be smaller than `deltaShortfall` for a pathological, interest-heavy repayment against a
very coarse scale (e.g. a late-fee-only payment that's almost entirely "interest" in a huge Vault),
which would make `settled` go negative. Not expected in practice given typical principal/interest
ratios, but not proven impossible either — flagged in the Appendix rather than bounded here.

Notes:

- `truncate(x, scale)` rounds down only — dust is always non-negative, never created in the caller's
  favor.
- Deposit's precondition guarantees `dustFromExcess = 0`; only `dustFromCoarsening` can fire for it.
  Repayment is the case where `dustFromExcess` is the common one. Same function handles both.
- `dust > 0` is only possible when `vault.sle[sfDustAccount]` exists — i.e. an IOU Vault (_Dust
  Pseudo-account lifecycle_, below). XRP/MPT Vaults never produce dust; that falls out of the
  truncation math, not an extra check.
- `WaiveTransferFee::Yes` matches every existing Vault-related transfer — an issuer's transfer rate
  shouldn't eat into internal accounting.

##### `MaybeSweep`

- Opportunistically moves as much of dust custody as is currently representable back into the main
  Pseudo-account/`AssetsTotal`. Called at the end of every `AddAssetsToVault`; a no-op below one
  representable unit of dust.
- The dust-to-main leg is a real transfer too, so it also goes through `SettleAdd`, with the Dust
  Pseudo-account as `from`. Guaranteed a no-op for XRP/MPT Vaults (no `sfDustAccount`, no balance).

```
function MaybeSweep(view, vault, j) -> TER:
    dustBalance = accountHolds(view, vault.sle[sfDustAccount], vault.asset, ...)
    if dustBalance == 0:
        return tesSUCCESS

    scale = effective(vault.AssetsTotal)
    sweepable = truncate(dustBalance, scale)
    if sweepable == 0:
        return tesSUCCESS   # not enough accumulated yet

    # NOT AddAssetsToVault (see below). `from` is the Dust Pseudo-account
    # itself; assetsTotalDelta is omitted (None) so it defaults to
    # whatever this call's own `settled` turns out to be — swept dust was
    # never counted in AssetsTotal to begin with, so landing it here is
    # exactly the "new value" default case, same as Deposit.
    result = SettleAdd(view, vault, vault.sle[sfDustAccount], sweepable, None, j)
    if result is error: return result.error()
    return tesSUCCESS
    # If this transfer induces further coarsening (Case 2, above),
    # SettleAdd's accountSendMulti sends the residual back to
    # vault.sle[sfDustAccount] — the same account it came from (from==to;
    # needs verifying that's a clean no-op — Appendix → Open questions).
    # Net effect on dust custody is always a decrease of exactly `settled`.
```

- Why `SettleAdd`, not `AddAssetsToVault`: the latter would trigger another `MaybeSweep`, which in
  the degenerate `settled == 0` case recomputes the same `candidate` against unchanged state —
  infinite recursion. `SettleAdd` bounds this to one attempt; nothing else calls `MaybeSweep` from
  inside itself.
- `MaybeSweep` never rejects or blocks — it strictly improves (or leaves unchanged) how much of
  `AssetsTotal` is backed by main custody.

##### Remove assets from Vault

- **Precondition:** same shape as above; callers are Withdraw, `VaultClawback`, Loan
  issuance/acceptance — all already rounded to `effective(AssetsTotal)` before calling. No Case-1
  analogue (no caller targets a different, finer precision).
- **No Case-2 analogue either** — the key asymmetry: removing only ever **shrinks** `AssetsTotal`,
  and effective scale is non-increasing in magnitude, so `effective(AssetsTotal - amount) >=
effective(AssetsTotal)` always. A removal can only hold the scale steady or refine it, never
  coarsen it. **Removing assets never generates dust.**
- Fund movement mirrors the algorithm above: a single-destination `accountSend` out of main custody,
  the same shape `doWithdraw` (`View.cpp`) already uses for Withdraw/`LoanBrokerCoverWithdraw`.
  Destination-specific setup (trust-line/MPToken creation, `verifyDepositPreauth`) stays the
  caller's job, same split as `VaultWithdraw::doApply`/`doWithdraw` today.
- **Terminal withdrawal: all `Dust` settles to the last withdrawer.** Non-terminal withdrawers never
  have a live problem here (Dust section, above): `MaybeSweep` folds `Dust` back into `AssetsTotal`
  as soon as it's big enough to move any settlement, so anyone withdrawing while other shares remain
  outstanding is pricing against a total that's already recognized `Dust` for real, or is carrying an
  amount too small to change their payout. What's left once a withdrawal drains the Vault to zero is
  only the residual that's _structurally_ un-sweepable — below one representable unit at closure,
  with no other shareholder left to divide it with. That's the one case needing a special rule: pay
  the whole residual to the withdrawer closing out the Vault, rather than leave it stranded. This
  also leaves `Dust` custody at zero, which `VaultDelete` needs anyway (_Dust Pseudo-account
  lifecycle_, below).

```
function RemoveAssetsFromVault(view, vault, to, amount, j) -> Expected<Number, TER>:
    # amount already rounded to effective(vault.AssetsTotal); sufficiency
    # checked upstream (preclaim) — not this algorithm's concern.

    ter = accountSend(view, vault.sle[sfAccount], to, amount, j, {}, WaiveTransferFee::Yes)
    if ter is not tesSUCCESS:
        return Unexpected(ter)

    vault.AssetsTotal -= amount
    vault.AssetsAvailable -= amount   # lockstep with AssetsTotal, per the scope note above

    if vault.AssetsTotal == 0 and vault.SharesTotal == 0 and vault.sle[sfDustAccount] is present:
        # Terminal: no other shareholder to preserve Dust for, so it
        # doesn't go through truncate()/effective() at all — just pay out
        # whatever's left, in full.
        dustBalance = accountHolds(view, vault.sle[sfDustAccount], vault.asset, ...)
        if dustBalance > 0:
            ter = accountSend(view, vault.sle[sfDustAccount], to, dustBalance, j, {}, WaiveTransferFee::Yes)
            if ter is not tesSUCCESS:
                return Unexpected(ter)
            # No Dust field to zero out — the account's own balance is now
            # zero, which is already the whole record.
    else:
        MaybeSweep(view, vault, j)   # non-terminal: still worth attempting

    return amount   # always settles in full
```

- Why check both `AssetsTotal == 0` and `SharesTotal == 0`: they should move to zero together on a
  full redemption, but are tracked independently — requiring both avoids treating a same-value
  coincidence as "terminal" when shares are still outstanding.
- Why call `MaybeSweep` on the non-terminal path despite removal never creating dust: removal
  _refines_ the effective scale, which can retroactively unlock a sweep that wasn't previously
  possible (a smaller required unit). Depositing calls it because an addition may have _created_
  dust; withdrawal calls it because a removal may have _unlocked_ dust already sitting there.

#### Dust Pseudo-account lifecycle

- Only **IOU Vaults** ever need one — XRP/MPT force `sfPrecision` to 0, so dust routing never
  applies. Like any ledger object, it carries an owner-reserve cost.
- **Decision: created eagerly, at Vault creation.** Every IOU Vault gets it up front, used or not —
  chosen over lazy creation (on first dust) because lazy creation has its own griefing vector: the
  address stays publicly predictable for the Vault's whole life while creation timing is
  unpredictable, letting an attacker pre-squat it and fail a mandatory repayment. Eager creation
  collapses the window to ordinary transaction-propagation risk, same as every other pseudo-account.
  Full analysis of the rejected alternative is in the Appendix, for the record; what follows is what's
  actually built.

##### Eager creation and tracking

Creation moves entirely into `VaultCreate` — no lazy-check anywhere in the settlement path; by the
time any `SettleAdd` runs, the Dust Pseudo-account (for an IOU Vault) already exists.

```
function VaultCreate(view, ...):
    ... (existing logic: build the ltVAULT SLE, ...)

    mainPseudo = createPseudoAccount(view, vault->key(), sfVaultID)
    if mainPseudo is error: return mainPseudo.error()
    vault.sle[sfAccount] = mainPseudo->key()
    # ... existing reserve accounting for the main Pseudo-account ...

    if vault.asset is IOU:                      # IOU-only
        dustPseudo = createPseudoAccount(view, vault->key(), sfVaultDustID)
        if dustPseudo is error: return dustPseudo.error()
        vault.sle[sfDustAccount] = dustPseudo->key()
        # Normal reserve check/increment, same as the main account above —
        # no carve-out needed: VaultCreate is explicit, blocks nothing
        # mandatory.

    ...
```

- Both accounts use the same seed, `vault->key()`; the retry loop assigns main → index 0, dust →
  index 1, entirely within one atomic transaction — no gap between the seed becoming known and either
  address being claimed, so the griefing window described in the Appendix doesn't exist here.
- **Tracking, in full:**
  - `ltVAULT.sfAccount` → main Pseudo-account (existing, required).
  - `ltVAULT.sfDustAccount` → Dust Pseudo-account (optional; present iff IOU). Set once at
    `VaultCreate`, never reassigned.
  - `ltACCOUNT_ROOT.sfVaultID`/`sfVaultDustID` — reverse links, distinguishing the two roles per
    Vault (see Appendix → _Storage and key generation_).
  - **Lifecycle symmetry:** since `sfDustAccount` is now unconditional for every IOU Vault, whatever
    deletes the main Pseudo-account on `VaultDelete` must be extended to delete the Dust
    Pseudo-account too, under the same invariants (e.g. zero balance). Worked out next.

##### `VaultDelete`: cleaning up the Dust Pseudo-account

- `VaultDelete::preclaim` already requires `sfAssetsAvailable == 0`, `sfAssetsTotal == 0`, and zero
  outstanding shares before allowing deletion. The terminal-withdrawal rule above means `Dust` is
  drained to zero by the same event that drives `AssetsTotal`/`SharesTotal` to zero — so by the time
  `preclaim`'s checks pass, the Dust Pseudo-account should already be empty. "Should" isn't
  "guaranteed by construction independent of bugs elsewhere," though, so `doApply` re-verifies it
  directly, the same defensive-depth style already used for the main Pseudo-account.
- `doApply` today, for the main Pseudo-account: `removeEmptyHolding` to destroy its trust-line/MPToken
  holding, destroy the share issuance, confirm the pseudo-account's owner directory is empty, confirm
  zero balance/owner count/no directory, erase it, then `decreaseOwnerCountForObject(view(), owner,
vault, 2, j_)` for the two objects destroyed (Vault + main Pseudo-account).
- Extension, mirrored for the Dust Pseudo-account, conditional on `vault.sle[sfDustAccount]` being
  present (IOU Vaults only):

```
function VaultDelete::doApply():
    ... (existing: removeEmptyHolding + erase main pseudo-account, per above) ...

    dustCount = 0
    if vault.sle[sfDustAccount] is present:
        dustPseudoAcct = view().peek(keylet::account(vault.sle[sfDustAccount]))
        if not dustPseudoAcct: return tefBAD_LEDGER          # LCOV_EXCL_LINE — should never happen

        if ter := removeEmptyHolding(applyViewContext, vault.sle[sfDustAccount], asset, j_);
           not isTesSuccess(ter): return ter

        # Same defensive re-check as the main Pseudo-account, not just
        # trusting preclaim's zero-AssetsTotal implication.
        if dustPseudoAcct[sfBalance] != 0: return tecHAS_OBLIGATIONS
        if dustPseudoAcct[sfOwnerCount] != 0: return tecHAS_OBLIGATIONS
        if view().exists(keylet::ownerDir(vault.sle[sfDustAccount])): return tecHAS_OBLIGATIONS

        view().erase(dustPseudoAcct)
        dustCount = 1

    ... (existing: dirRemove vault from owner's directory) ...

    # Was `decreaseOwnerCountForObject(view(), owner, vault, 2, j_)` for
    # Vault + main Pseudo-account; now 2 + dustCount, so IOU Vaults with a
    # Dust Pseudo-account correctly release its reserve too.
    decreaseOwnerCountForObject(view(), owner, vault, 2 + dustCount, j_)

    view().erase(vault)
```

- The Dust Pseudo-account never held a share-issuance-style object of its own (only ever a plain
  asset holding, same as the main account), so there's no analogue to the share-issuance-destruction
  block — just the holding, the balance/owner-count/directory checks, and the erase.

## 4. Appendix

### 4.1 Why `DebtTotal` doesn't need this

`ltLOAN_BROKER.sfDebtTotal` has its own rounding mechanism, already in production, that looks
structurally similar to the problem the Dust mechanism solves: `adjustImpreciseNumber`
(`LendingHelpers.h`) rounds every adjustment to `DebtTotal` to the Vault's current `vaultScale`, and
its own comment at the `LoanPay.cpp` call site says so plainly — "despite our best efforts, it's
possible for rounding errors to accumulate in the loan broker's debt total... because the broker may
have more than one loan with significantly different scales." Same shape of problem: an aggregate
bounded by the 16-sig-digit ceiling, fed by inputs at multiple different fixed scales, coarsening as
it grows. Why this document doesn't extend `Dust`/`SettleAdd` to cover it too:

- **`DebtTotal` isn't custodied money.** `AssetsTotal` must reconcile exactly with a real balance —
  the Vault's Pseudo-account — which is why a dropped remainder needs somewhere to go (`Dust`).
  `DebtTotal` is a receivables _estimate_ (how much principal this broker's loans still owe); no
  account balance anywhere corresponds to it, so there's nothing for its rounding error to misplace.
- **The error is symmetric, not a one-directional drain.** `Number`'s default rounding mode is
  `ToNearest`, so `DebtTotal`'s accumulated error random-walks rather than systematically leaking in
  one direction.
- **Its one safety-relevant consumer already rounds conservatively.** `minimumBrokerCover` rounds
  its result _up_, specifically "to be conservative... ensures `CoverAvailable` never drops below
  the theoretical minimum" — so any drift in `DebtTotal` can only make the required cover look
  slightly larger than the true debt, never smaller, which is the safe direction for solvency.
- **It predates this design and isn't changed by it.** Existing, already-accepted behavior, not
  something introduced or made worse by `sfPrecision`/`Dust` — nothing in this design touches how
  `DebtTotal` is computed or rounded.

**Conclusion: left as-is, deliberately, not an oversight.**

### 4.2 Rejected alternative: lazy Dust-account creation

Kept for the record; superseded by eager creation (Technical Details → _Dust Pseudo-account
lifecycle_) once §4.3 below found a griefing vector specific to it.

`createPseudoAccount(view, pseudoOwnerKey, ownerField)` creates the `ACCOUNT_ROOT`, same helper the
main Pseudo-account already uses. The codebase's reserve-**sponsorship** mechanism (`sfSponsor`,
`checkReserve` with a sponsor) doesn't help here — it's allow-listed per transaction type and still
_enforces_ a check, just against a different account; it relocates the failure, doesn't remove it.
The real precedent is `adjustLoanBrokerOwnerCount`, which maintains a LoanBroker owner-count field
deliberately _not_ wired into `checkReserve`/`increaseOwnerCount` at all.

```
function EnsureDustPseudoAccount(view, vault) -> AccountID:
    if vault.sle[sfDustAccount] is present:
        return vault.sle[sfDustAccount]

    # Must not fail the enclosing mandatory transaction — no
    # checkReserve/increaseOwnerCount, per adjustLoanBrokerOwnerCount's
    # precedent. Same seed as the main Pseudo-account; the retry loop
    # lands on the next free address.
    sleResult = createPseudoAccount(view, vault->key(), sfVaultDustID)
    vault.sle[sfDustAccount] = sleResult->key()
    return sleResult->key()

function SettleAdd(view, vault, amount) -> settled:
    ... (unchanged truncation math) ...
    if dust > 0:
        dustAccountId = EnsureDustPseudoAccount(view, vault)   # lazily, on first dust
        vault.Dust += dust
        DustPseudoAccount(dustAccountId).balance += dust
    ...
```

Consequence: whichever repayment happens to be first pays a one-time `ACCOUNT_ROOT`-creation cost no
other repayment on that Vault pays — asymmetric, but bounded to once per Vault.

### 4.3 Address predictability is a griefing vector under lazy creation

`pseudoAccountAddress` hashes only public inputs: `vault->key()` (public and permanent once the
Vault exists) and `parentHash` (public the instant a ledger closes). Fine for the **main**
Pseudo-account, created atomically with `ltVAULT` — the only exposure is ordinary
transaction-propagation time.

Not fine for a **lazily**-created Dust Pseudo-account (§4.2, above): the seed is public for the
Vault's entire remaining life while creation timing stays unpredictable. Attack:

1. Watch Vault X for a repayment likely to generate dust (the whole reason lazy creation exists is
   that this moment is unpredictable).
2. Once visible, precompute the exact candidate address `EnsureDustPseudoAccount` will try at retry
   index 1 for that ledger's `parentHash` (index 0 is always the main account).
3. Submit a trivial Payment there first, auto-creating an `ACCOUNT_ROOT`. The real call advances to
   index 2 — also precomputable and squattable.
4. Squat all 256 candidates in that ledger → `pseudoAccountAddress` returns `beast::kZero`,
   `createPseudoAccount` returns `tecDUPLICATE`. The triggering repayment — a mandatory operation
   for a borrower unrelated to this attack — fails.

That's exactly the failure mode §1.2 exists to eliminate. Squatting 256 addresses costs real (if
modest) XRP, and `parentHash` changes every ledger so a squat set is single-use — but the attacker
only needs to deny one repayment, at a moment of their choosing, with no advance preparation window
required (the seed has been public since Vault creation).

**Implication:** structural argument for eager creation — atomic creation with `VaultCreate`
collapses the window to the same risk every other pseudo-account already accepts; no seed formula
(§4.4, below) fixes lazy creation's exposure. **Decision: eager** (Technical Details → _Dust
Pseudo-account lifecycle_).

### 4.4 Storage and key generation (background, applies to either option)

**Storage.** `ltVAULT.sfAccount` (required) already points at the main Pseudo-account. Symmetric new
field **`sfDustAccount`** (`SoeOptional` — only IOU Vaults populate it).

**Reverse link.** `ltACCOUNT_ROOT` already has designator fields (`sfAMMID`, `sfVaultID`,
`sfLoanBrokerID`, flagged `sMD_PseudoAccount` for `getPseudoAccountFields()`/`isPseudoAccount()`).
The Dust Pseudo-account needs its own — **`sfVaultDustID`** — rather than reusing `sfVaultID`, which
would make two different `ACCOUNT_ROOT`s for the same Vault indistinguishable by field, breaking the
implicit one-designator-one-pseudo-account assumption invariant checks rely on.

(`ltVAULT` (`0x0084`) has `0x0085`–`0x0087` reserved for future Vault-related objects — capacity
exists if a fuller object type is ever needed, but a bare `ACCOUNT_ROOT` is enough here.)

**Key generation.** No new primitive needed — `createPseudoAccount`/`pseudoAccountAddress` (hashing
`(retryIndex, parentHash, pseudoOwnerKey)`, retrying up to 256 times) is the same machinery every
pseudo-account already uses. The only choice is the `pseudoOwnerKey` seed: **eager creation** reuses
`vault->key()` unchanged (the retry loop naturally lands on the next free address after the main
account); **lazy creation** would have derived an explicitly distinct seed (e.g.
`sha512Half(vault->key(), <discriminator>)`). **Both are equally vulnerable to §4.3's griefing
attack** — the vulnerability is about _when_ creation happens relative to how long the seed's been
public, not which formula computes it.

### 4.5 Open questions / not yet resolved

- **`SettleAdd`'s `deltaShortfall` reconciliation can drive `settled` negative in a pathological
  case.** (Technical Details → _Add assets to Vault_.) Moving an overridden `assetsTotalDelta`'s own
  truncation shortfall out of `settled` and into `dust` assumes `settledAfterCoarsening >=
deltaShortfall`. Not
  proven for every combination of principal/interest split and Vault scale — e.g. a late-fee-only
  repayment that's almost entirely "interest" against a very coarse `effective(AssetsTotal)`. Not
  expected in practice given typical principal/interest ratios; needs a bound or a clamp before this
  ships.
- **No invariant-check design.** Candidates implied by the design but never stated: post-`MaybeSweep`,
  dust custody never exceeds one representable unit at the current effective scale; a global
  conservation identity across `AssetsTotal`/`Dust`/`AssetsAvailable`. (An earlier candidate — "Dust
  Pseudo-account's real balance ≡ `vault.Dust`" — no longer applies: dropping the redundant `sfDust`
  field, above, means there's only one number left, so there's nothing separate for it to disagree
  with.)
- **Dust-sweep timing has a narrow, bounded attribution quirk — accepted, not fixed.** A withdrawal's
  own settlement amount is computed against `AssetsTotal` _before_ that same transaction's
  `MaybeSweep` call runs. If unswept `Dust` crosses the representable-unit threshold as a side effect
  of this withdrawal, the withdrawer doesn't get credit for it — the next depositor's or withdrawer's
  exchange rate does. Bounded to less than one representable unit by construction (Technical Details
  → _Dust: error accumulation and custody routing_), the same order of magnitude as ordinary rounding
  noise elsewhere in this design; not considered worth a dedicated fix.
- **`sfPrecision` immutability asserted, not enforced.** No confirmation `VaultSet` rejects attempts
  to change it post-creation.
- **PoC test file is stale.** Predates the `Dust` rename, eager-creation decision, terminal-withdrawal
  rule, and real `accountSend` integration.
- **`LoanPay.cpp`'s real repayment splits three ways** (vault main custody, vault dust custody,
  broker payee); `SettleAdd`'s `accountSendMulti` only models the first two. Whether the broker-fee
  leg composes alongside `SettleAdd` or stays entirely the caller's own separate transfer was never
  spelled out.

### 4.6 Next steps

- Audit Loan/LoanBroker code paths for assumptions of dynamic Vault scale — confirm `loan.scale`
  is captured at funding (Technical Details → _Loan issuance_), not recomputed per-operation.
- Resolve the open questions above before finalizing the dust-custody mechanism.

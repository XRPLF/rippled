# Vault Fixed Precision — Design Notes

Status: draft, in progress.

## 1. Problem

Vault rounding precision is currently _derived_ from `AssetsTotal` rather than _declared_ once.
Amounts have a fixed budget of significant digits (16, §1.1); as `AssetsTotal` grows, less of that
budget is left for the fraction, so effective precision shrinks as the Vault grows:

1. **Unpredictable** — the same nominal payment rounds differently depending on Vault size at the
   moment, unrelated to the transaction.
2. **Precision degrades with success** — bigger, more successful Vaults get sloppier accounting.
3. **Cascading complexity** — Loan precision derives from Vault precision, a moving target.
4. **Non-deterministic edge cases** — dust/precision-loss depend on Vault size at operation time,
   not fixed rules.
5. **No stable contract** — integrators can't know rounding precision ahead of time without
   inspecting live (and staleable) Vault state.

### 1.1 The Ledger & Custody Constraint

Every Vault has two entities, both bound by the same **16 significant-digit ceiling** (rippled's
amount representation limit):

- **SAV** — internal accounting ledger for total assets/shares.
- **Pseudo-account** — the custody account actually holding the asset.

That 16-digit budget splits between integer and fractional parts. At high valuation (e.g. $100B)
with fine precision (e.g. 6 decimals), an amount like $1.234567 may not fit — a representation
limit, not policy.

### 1.2 Why a naive fixed precision isn't enough

Fixing one precision value at Vault creation and applying it everywhere — including internal running
totals — still hits the 16-digit wall: a repayment's interest/fee component can push `AssetsTotal`
past what a fixed precision can represent. Rejecting the transaction blocks a borrower from closing
debt for reasons unrelated to their loan.

There's also an asymmetry: **optional** operations (e.g. a deposit) can tolerate silent truncation;
**mandatory** ones (a repayment closing debt) cannot — dropping the remainder breaks the
assets/shares identity or shortchanges what's owed.

So the fix needs two things that must not be conflated:

- A fixed, design-time precision ceiling for client-facing operations, set once at Vault creation,
  giving integrators a stable contract.
- That ceiling must _not_ also bound the Vault's internal running totals, which grow unbounded and
  will eventually collide with the 16-digit ceiling if forced into the same representation as a
  single transaction amount.

## 2. Summary of the solution

Two new fields on `ltVAULT` (`sfPrecision`, `sfDustAccount`), one new reverse-link field on
`ltACCOUNT_ROOT`, one new pseudo-account per IOU Vault, and one existing, unchanged `ltLOAN` field
(`sfLoanScale`) whose Vault-side input and validation timing change (full detail in **Technical
Details**, below):

- **`sfPrecision`** — a rounding _ceiling_, set once at Vault creation, immutable thereafter. The
  Vault's effective rounding scale is the finer of that ceiling and whatever the 16-sig-digit budget
  currently allows given `AssetsTotal`'s size — it only ever degrades from the ceiling as the Vault
  grows, never below it. This gives integrators a stable, computable contract instead of an opaque,
  ever-shifting derivation.
- **`sfDustAccount`**, a new **Dust Pseudo-account** per IOU Vault — captures the value a
  _mandatory_ operation (a Loan repayment) would otherwise have to drop when the Vault's current
  effective scale can no longer represent it. The representable portion settles normally; the
  unrepresentable remainder transfers into the Dust Pseudo-account. There's no separate `sfDust`
  counter — the account's own real balance _is_ the record of deferred value, so it can't drift out
  of sync with itself. Once enough dust accumulates to be representable again, it's swept back into
  ordinary custody. Nothing is ever silently dropped, and a repayment is never rejected for reasons
  unrelated to the loan itself.
- **`sfLoanScale`** (existing field, existing `max`-of-two-floors formula, unchanged) — a Loan's own
  rounding precision, fixed once at issuance and immutable thereafter, set to the coarser of what the
  Vault can currently represent and what the Loan's own total lifetime value (principal plus all
  future interest) independently needs. Both floors are knowable at issuance time, so nothing about a
  Loan's precision ever needs to be recomputed later. What this design actually changes: the
  Vault-side floor becomes `effective(AssetsTotal)` (bounded by `sfPrecision`) instead of today's raw,
  unbounded scale, and a pending Loan's already-fixed value gets re-validated once, at acceptance,
  against that floor — a check `LoanAccept` doesn't do today.

Put together: optional operations (Deposit, Withdraw, `VaultClawback`) round directly to the Vault's
current effective scale and essentially never touch `Dust`. Loan funding is treated the same way, and
that's the moment a Loan's own precision gets fixed. Loan repayment is the one mandatory, two-stage
case: round to the Loan's own fixed precision, then — if the Vault has since grown coarser — split the
payment between main custody and the Dust Pseudo-account rather than truncating or rejecting. A
Vault's final withdrawal, which drains it to zero, pays out whatever `Dust` is left in full, since
there's no other shareholder left to divide it with.

This gives the Vault a fixed, declared precision contract for anything a client interacts with, while
keeping its unbounded internal totals safely decoupled from that same 16-digit ceiling.

## 3. Technical details

### 3.1 Fields

| Field           | Object           | Notes                                | Set                                        | Mutable                                                                      | Purpose                                                                                                                                                                                   |
| --------------- | ---------------- | ------------------------------------ | ------------------------------------------ | ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `sfPrecision`   | `ltVAULT`        | New                                  | `VaultCreate`                              | No — immutable (assertion only, not yet enforced; Appendix → Open questions) | Design-time rounding ceiling — see _Precision ceiling_ below.                                                                                                                             |
| `sfDustAccount` | `ltVAULT`        | New, `SoeOptional` — IOU Vaults only | `VaultCreate`                              | No — set once, never reassigned                                              | Points at the Vault's Dust Pseudo-account. Its real balance, not a separate ledger field, is the authoritative amount of dust — see _Dust: error accumulation and custody routing_ below. |
| `sfVaultDustID` | `ltACCOUNT_ROOT` | New designator field                 | `VaultCreate` (on the Dust Pseudo-account) | No                                                                           | Reverse link distinguishing the Dust Pseudo-account from the main one (`sfVaultID`) — two `ACCOUNT_ROOT`s for the same Vault must stay distinguishable by field.                          |
| `sfLoanScale`   | `ltLOAN`         | Existing field, existing formula     | `LoanSet` (create), exactly as today       | No — immutable once set                                                      | The Loan's own fixed rounding precision — see _Loan issuance_ below. Re-validated (never re-set) at `LoanAccept`.                                                                         |

**Not new, but relevant:** `sfScale` (`ltVAULT`, existing) is the fixed assets↔shares conversion
exponent already used in `VaultHelpers.cpp` — unrelated to `sfPrecision` despite the similar naming;
the two must not be conflated.

### 3.2 Algorithm

#### Precision ceiling: `effective(AssetsTotal)`

```
effective(AssetsTotal) = min(sfPrecision, maxPrecisionRepresentableGiven(AssetsTotal, 16-sig-digit budget))
```

Degradation is strictly downward from the ceiling — the Vault never rounds finer than `sfPrecision`.
Integrators get a deterministic contract: the finest precision ever used, computable from
`AssetsTotal` and `sfPrecision` rather than an opaque derivation. It doesn't resolve the 16-digit
collision on internal running totals — that's the Dust mechanism, next.

#### Dust: error accumulation and custody routing

Lets a mandatory operation succeed in full when `sfPrecision` can't currently be honored, without
truncating value or rejecting the tx.

- **Ledger tracking:** no separate accumulator field. `Dust` _is_ the Dust Pseudo-account's real
  balance (`accountHolds(vault.sle[sfDustAccount], asset)`), read directly wherever a "how much dust
  exists right now" answer is needed. Whenever an amount rounds down from `sfPrecision` to the
  effective scale, the dropped remainder transfers into that account — the transfer itself is the
  record; there's no separate ledger number that could drift out of sync with it. (An earlier version
  of this design kept a redundant `sfDust` counter on `ltVAULT` alongside the real balance — dropped:
  every consuming path would have had to remember to decrement it exactly in step with the account's
  real balance, and the sweep path didn't, which is exactly the kind of bug a derived, single-source
  balance can't have.)
- **Custody routing:** the representable portion flows to the main Pseudo-account as normal; the
  unrepresentable fraction flows to the dedicated Dust Pseudo-account for that Vault.
- **The Sweep:** once dust custody accumulates a whole representable unit, it's transferred cleanly
  into the main Pseudo-account.

Invoked only where the case-by-case rules below say so — not a general-purpose remainder catcher for
ordinary rounding.

**`Dust` is real value, and must count as such — for both totals, not just one.** `SettleAdd` (_Core
primitives_, below) only adds the _settled_ portion into `AssetsTotal`; the dust portion sits, for
real, in the Dust Pseudo-account. If the figures exposed for NAV/share pricing and liquidity don't
include it, both silently undercount the Vault:

```
totalAssetsForPricing = AssetsTotal    + Dust
totalAssetsAvailable   = AssetsAvailable + Dust
```

`Dust` isn't just "extra value" NAV needs to know about — it's real, liquid money, never lent to
anyone, in an account the Vault fully controls. That's precisely what `AssetsAvailable` tracks (the
not-out-on-loan, liquid portion), so `Dust` belongs in _both_ sums: leaving it out of
`AssetsAvailable` would understate how much the Vault can actually pay out right now.

This matters beyond bookkeeping hygiene: `AssetsTotal`/`AssetsAvailable` are each bounded by an
absolute `STAmount` ceiling, independent of `sfPrecision`'s value — once pinned there, neither can
accept further inflow at _any_ scale. `Dust`, in its own account with its own headroom, becomes a
**fallover buffer** past that point. Without folding `Dust` into both sums, that fallover regime
would be invisible — depositors' claims and withdrawal liquidity would both stop reflecting real
inflows once `AssetsTotal` maxes out, even as the Vault keeps receiving (and owing) more.

**Why folding `Dust` into both totals doesn't cause anyone to be overpaid.** This looks, at first
glance, like it could commit the Vault to paying out value that only exists in a separate account no
withdrawal ever touches (except the terminal case, below). It doesn't, for two reasons that hold
together:

- **Day to day, it's mathematically real but numerically invisible.** `Dust` only exists because
  it's smaller than one representable unit at the current `effective(AssetsTotal)` — that's the
  definition of dust. Adding a sub-unit quantity into either total before computing a share price or a
  withdrawal amount changes that computation by less than one representable unit — which then rounds
  away in the same settlement step (`round(amount_requested, effective(AssetsTotal))`, _Case by case_
  below) that produces the amount actually paid. Including `Dust` in the formula is correct, but for a
  healthy Vault it never changes what any single depositor or withdrawer actually receives.
- **Once it's not invisible, it's not `Dust` for long.** `MaybeSweep` (_Core primitives_, below) runs
  after every fund-moving operation and folds any dust balance that's grown past one representable
  unit straight into `AssetsTotal`/`AssetsAvailable` — at which point it's ordinary, fully-backed
  Vault money, not a separate pool anything else needs to account for. The only way `Dust` sits at an
  economically material level is transiently, between the moment it crosses that threshold and the
  next operation that triggers a sweep.

So both totals above are correct to include even though nothing pays out of the Dust Pseudo-account
directly in the common case — the inclusion only ever matters in the regime this design already calls
out separately: near the absolute `STAmount` ceiling, where `AssetsTotal` itself can't grow further
and `Dust`'s own headroom is what keeps the Vault's reported totals honest.

**Scope note on `AssetsAvailable`.** The fuller share-pricing model (`VaultSharePricing_test.cpp`
PoC) also tracks `interestUnrealized`/`lossUnrealized`, under which `AssetsTotal` and
`AssetsAvailable` can diverge further still — e.g. funding a loan moves principal out of
`AssetsAvailable` without touching `AssetsTotal`, since the loan remains an asset, just an illiquid
one. **This document doesn't model that bucket** — it's scoped to precision/dust.

Within that narrower scope, `AssetsTotal` and `AssetsAvailable` already diverge on their own, by
construction, not as future work: `SettleAdd` moves `AssetsAvailable` by the full dust-routed
settlement (`settled` — real cash landing in custody), but moves `AssetsTotal` by a separately
supplied `assetsTotalDelta`, which is **not** `settled` for a cash-basis repayment (_Loan repayment_,
below) — Deposit is the one degenerate case where the two happen to coincide. That divergence is
required for cash-basis correctness; it's not the same divergence as the interest-accrual bucket
above, which is what's flagged in the Appendix's open questions, not resolved here.

#### Case by case: how rounding actually works

##### User fund movement — Deposit, Withdraw, `VaultClawback`

**Optional operations** — round directly to the effective scale, no `sfPrecision`-first attempt:

```
amount_settled = round(amount_requested, effective(AssetsTotal))
```

This pre-rounding eliminates Case 1 (_Core primitives_, below) — `dust_0` is always `0` for these
callers, unlike repayment. It does **not** eliminate Case 2: a Deposit that pushes `AssetsTotal`
across a magnitude boundary can still coarsen the effective scale between the caller's rounding and
the moment `SettleAdd` applies it, producing `dust_1` the same way a repayment can. That's rare — it
only fires exactly at a growth boundary — but it isn't structurally impossible, so Deposit still goes
through `SettleAdd`/`AddAssetsToVault` like every other operation, not a separate dust-free path. What
_is_ true for these operations: none of them are obligations owed in full, so if Case 2 fires, it's
the caller's own requested amount that silently absorbs the truncation — never an already-agreed,
mandatory amount the way a repayment's `loan.precision` mismatch is.

##### Loan issuance (funding) — Vault → Loan

Funding is a Vault-initiated disbursement, not a debt obligation — structurally a withdrawal.
Follows the rule above: rounds directly to the Vault's _current_ effective scale, no dust.

Consequence: **the Loan's own precision is fixed once, at funding time, to the effective scale used
for that disbursement** — not the Vault's raw `sfPrecision` ceiling. But "the effective scale used
for that disbursement" isn't just the Vault's own scale: it's the coarser of two independent floors —
one knowable only from the Vault's current state, the other knowable in full from the Loan's own
terms, before a single payment is ever made:

```
loan.precision = max(
    effective(AssetsTotal at funding time),      # Vault-side floor: what the Vault can
                                                   # represent right now
    scale(totalValueOutstanding, asset)           # Loan-side floor: what this Loan's own
                                                   # projected lifetime value needs
)
```

The Loan-side floor is `scale(principal + all future interest, asset)` — Equation (30) of the XLS-66
spec, `totalValueOutstanding = periodicPayment × paymentsRemaining`. `computeLoanProperties`
(`LendingHelpers.cpp`) already computes exactly this today, as
`loanScale = max(minimumScale, amount.exponent())`, where `minimumScale` is the Vault-side floor
passed in by the caller (see its "base the loan scale on the total value, since that's going to be
the biggest number involved" comment). This document's earlier formula dropped that second term —
worth stating explicitly why it's safe, and necessary, to fold back in rather than leave as an open
unknown:

- **The Loan-side floor is fully deterministic at creation time.** Interest rate, payment interval,
  and payment count are all fixed loan terms chosen at `LoanSet`, so `totalValueOutstanding` — and
  therefore its natural scale — is computable synchronously, with no dependency on anything that
  hasn't happened yet. A modest principal at a high rate over a long term can have
  `principal + total future interest` land at a materially larger order of magnitude than the Vault's
  current `AssetsTotal`, even though the principal itself came from (and is bounded by) the Vault —
  nothing bounds future interest by the Vault's present size, especially under cash-basis accounting,
  where `AssetsTotal` doesn't recognize interest until it's actually paid (_Loan repayment_, below).
- **The Vault-side floor is not knowable in advance.** How much _more_ the Vault's own effective
  scale might degrade over the Loan's life — from other loans funding/repaying, deposits, sweeps,
  other borrowers' interest accruing — depends on the whole Vault's future activity, not this Loan's
  terms. That unpredictability is the actual reason `loan.precision` must be pinned immutably at
  funding at all: if it were knowable in advance the way the Loan-side floor is, there'd be no need
  to freeze it.

Stored as the existing `sfLoanScale` field on `ltLOAN`, set in `LoanSet.cpp` exactly as today —
`computeLoanProperties` already computes this two-floor `max` at creation. What's new: the Vault-side
floor becomes `effective(AssetsTotal)` (bounded by `sfPrecision`) instead of today's raw, unbounded
`getAssetsTotalScale`; and `LoanAccept` gains a re-validation step it doesn't have today (see
_Pending Loans_ below) — the already-fixed value is checked, never recomputed or re-set, at
acceptance.

Immutable thereafter, same as `sfPrecision` is for the Vault. Operating a Loan at finer precision
than it was actually funded with would assert false precision across all its future accounting.

**Worked example.** Vault `sfPrecision` = 6. Loan A funds while `AssetsTotal` is small (effective
scale 6) and Loan A's own `totalValueOutstanding` also fits at scale 6 (short term, low rate) →
`loan.precision` = 6, the Vault-side floor binds. Later `AssetsTotal` grows enough to degrade
effective scale to 5; Loan B funds then, with a long term and high rate whose projected
`totalValueOutstanding` alone needs scale 4 → `loan.precision` = 4, the coarser of the two floors —
Loan B's _own_ terms are the binding constraint here, not the Vault's size at all. Same Vault, two
Loans, two fixed precisions, each set by whichever floor is coarser at that Loan's funding moment.

###### Pending Loans: acceptance can straddle a scale change

Issuance is two steps: **create** (amount agreed/reserved, funds not yet moved) and **accept**
(funds move). "Funding time" above is really **acceptance time**.

Between create and accept, `AssetsTotal` can degrade the effective scale enough that the agreed
amount is no longer representable. Only the Vault-side floor (above) can move in that window — the
Loan-side floor is already fixed at create time, from the Loan's own agreed terms, and doesn't
change while pending. Two options:

- Silently re-round at acceptance — rejected: mutates agreed loan terms invisibly, exactly the
  non-determinism this design eliminates.
- **Reject the acceptance**, forcing re-creation/re-quote — worse for the borrower short-term, but
  fails loudly instead of silently mutating terms. **Chosen.**

So a pending Loan's amount is re-validated against `effective(AssetsTotal)` at acceptance; on
failure, acceptance is rejected outright, never adjusted.

**This is griefable, not just naturally rare — accepted anyway.** `effective(AssetsTotal)` is a
public, deterministic function of `AssetsTotal`, and a pending Loan's agreed principal/scale is
visible on-ledger from the moment it's created. Anyone able to deposit into the Vault can compute
exactly how much more `AssetsTotal` needs to grow to cross the next magnitude boundary, deposit that
amount right before the borrower's `LoanAccept`, degrade the effective scale, and force the
acceptance to fail — at essentially no cost, since the deposit isn't spent, it converts to shares the
attacker still holds (and can withdraw right back out afterward). A motivated party could grief a
specific pending Loan's acceptance repeatedly, each time forcing the borrower to eat a re-quote
(possibly at worse terms if the market's moved), for reasons unrelated to that Loan's own terms.

Unlike the address-squatting vector below (Appendix → _Address predictability is a griefing vector
under lazy creation_) — which costs real (if modest) XRP per attempt and is single-ledger-use, and
which this document treats as worth designing around — this one is cheaper and repeatable.
**Decision: accepted risk anyway**, on the same cost/benefit basis as ordinary transaction-propagation
risk elsewhere: the failure mode is a rejected acceptance and a forced re-quote, not a loss of funds
or a stuck Vault, and building in tolerance (e.g. letting acceptance absorb one boundary crossing via
the Dust mechanism, the way repayment does, instead of hard-rejecting) is real added complexity for a
griefing outcome that's merely annoying, not unsafe. Flagged here rather than fixed.

##### Loan repayment — Loan → Vault

**Mandatory operation.** Two-stage:

1. Round to the Loan's own fixed `loan.precision` (above) — may already be coarser than the Vault's
   current `sfPrecision` ceiling. This applies to the combined cash figure that actually settles —
   principal plus interest together — not principal alone; `LoanPay.cpp` already rounds
   `principalPaid + interestPaid` as one figure before this point today.
2. Check representability at the Vault's _current_ `effective(AssetsTotal)`:
   - `loan.precision <= effective(AssetsTotal)`: settles in full, no dust.
   - Otherwise: split. The representable portion settles into main custody; the delta routes to the
     Dust Pseudo-account, recorded in `Dust`.

The common case for Case 1 dust (_Core primitives_, below) — a loan's fixed precision going stale as
the Vault has grown since funding. The borrower's payment is always collected in full — the split is
a custody-routing detail, invisible to the borrower's accounting.

**`AssetsTotal` and custody settlement are driven by two different numbers, so `AddAssetsToVault`/
`SettleAdd` take them as two separate parameters, not one.** `AddAssetsToVault(view, vault, from,
amount, assetsTotalDelta, j)`:

- `amount` — the real cash settling into custody (principal + interest combined, for a repayment).
  Goes through the full dust/precision-routed path (_Core primitives_, below): `AssetsAvailable`
  moves by whatever of it actually lands (`settled`); any truncated remainder routes to `Dust`.
- `assetsTotalDelta` — how much `vault.AssetsTotal` itself moves. Supplied by the caller, defaulting
  to `settled` when not overridden — that default is what makes Deposit's case (above) and
  `MaybeSweep`'s sweep leg (_Core primitives_, below) work correctly, since for both of those, landed
  custody genuinely _is_ new value to `AssetsTotal`. A cash-basis repayment is the one caller that
  must override this default, passing `interestPaid` instead of letting it default to `settled` —
  never dust-routed, never changing `AssetsAvailable`, no transfer of its own (bookkeeping only,
  layered on the same real cash movement as `amount`).

Why a cash-basis repayment can't just take the default: this mirrors `LendingProtocolV1_1`'s
cash-basis accounting model ([#7817](https://github.com/XRPLF/rippled/pull/7817)) — `AssetsTotal`
tracks **principal only**, as a receivable recognized once, at origination; interest is recognized
**only once paid**, not at origination. Principal cash returning at repayment must _not_ grow
`AssetsTotal` again — it's the same receivable converting from loan back to cash, not new value —
whereas `settled` (the thing `SettleAdd` defaults `assetsTotalDelta` to) is principal _and_ interest
combined. Letting `AssetsTotal` default to `settled` here, as this doc's algorithm originally did
unconditionally, double-counts principal into `AssetsTotal` on every cash-basis repayment — that was
a real bug in the original `SettleAdd` pseudocode, not just an accounting simplification, and is why
the override exists as an explicit, required parameter rather than an afterthought.

**Open question this raises, not resolved here:** dust/precision routing exists to protect
`AssetsTotal` additions from exceeding the 16-sig-digit ceiling. Under cash-basis, `assetsTotalDelta`
(i.e. `interestPaid`) is the only thing that grows `AssetsTotal` on repayment, so it's the value that
could, in principle, need the same two-stage truncation `SettleAdd` already does for `amount`. This
document gives it no dust routing of its own (Appendix → Open questions) — assumed small enough
relative to `AssetsTotal` to add cleanly, not verified.

#### Core primitives

Two shared primitives, invoked by every fund-moving operation above instead of each implementing its
own settlement/dust logic: **add assets to Vault**, **remove assets from Vault**.

##### Add assets to Vault

**Precondition:** the caller has already rounded `amount` — the cash settlement figure — to _some_
target scale (Deposit → `effective(AssetsTotal)`; repayment → `loan.precision`, applied to
principal+interest combined). This algorithm decides how much of `amount` custody
(`AssetsAvailable`) can absorb right now and routes the rest to `Dust`; `AssetsTotal` moves
separately, driven by the caller-supplied `assetsTotalDelta` (_Loan repayment_, above), not by this
truncation.

Two independent, _composable_ dust sources, not exclusive cases:

1. **Pre-existing excess** — the amount already exceeds the current effective scale `e0` (e.g.
   `loan.precision > e0`). Detectable before touching `AssetsTotal`.
2. **Addition-induced coarsening** — even after trimming to `e0`, adding the result can cross a
   magnitude boundary and coarsen the scale further (`e1 = effective(AssetsTotal + settled_0) <
e0`).

Both can fire on one call: trim against `e0`, then trim the result again against `e1`. Converges in
one extra pass — `settled_1 <= settled_0`, so re-adding it can't coarsen below `e1`.

The truncation/bookkeeping core is `SettleAdd`, which never sweeps; `AddAssetsToVault` wraps it with
an opportunistic sweep. Keeping them separate avoids `MaybeSweep` recursively triggering itself
(below).

Real fund movement goes through `accountSend`/`accountSendMulti` (`TokenHelpers.h`), same as every
existing transactor. `LoanPay.cpp` already does this exact split-destination move in one call —
settling to the Vault's Pseudo-account and the broker's payee simultaneously via `accountSendMulti`.
`SettleAdd`'s main/dust split is the same shape: one sender, up to two destinations, atomic.

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
    # assetsTotalDelta is never dust-routed and never changes
    # AssetsAvailable — bookkeeping only, no transfer of its own.

    e0 = effective(vault.AssetsTotal)
    settled_0, dust_0 = truncate(amount, e0)          # Case 1: pre-existing excess

    projectedTotal = vault.AssetsTotal + settled_0
    e1 = effective(projectedTotal)

    if e1 < e0:
        settled_1, dust_1 = truncate(settled_0, e1)   # Case 2: addition-induced coarsening
    else:
        settled_1, dust_1 = settled_0, 0

    settled = settled_1
    dust = dust_0 + dust_1

    if assetsTotalDelta is None:
        assetsTotalDelta = settled   # Deposit / sweep default

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

Notes:

- `truncate(x, scale)` rounds down only — dust is always non-negative, never created in the caller's
  favor.
- Deposit's precondition guarantees `dust_0 = 0`; only `dust_1` can fire for it. Repayment is the
  case where `dust_0` is the common one. Same function handles both.
- `dust > 0` is only possible when `vault.sle[sfDustAccount]` exists — i.e. an IOU Vault (_Dust
  Pseudo-account lifecycle_, below). XRP/MPT Vaults never produce dust; that falls out of the
  truncation math, not an extra check.
- `WaiveTransferFee::Yes` matches every existing Vault-related transfer — an issuer's transfer rate
  shouldn't eat into internal accounting.

##### `MaybeSweep`

Opportunistically moves as much of dust custody as is currently representable back into the main
Pseudo-account/`AssetsTotal`. Called at the end of every `AddAssetsToVault`; a no-op below one
representable unit of dust.

The dust-to-main leg is a real transfer too, so it also goes through `SettleAdd`, with the Dust
Pseudo-account as `from`. Guaranteed a no-op for XRP/MPT Vaults (no `sfDustAccount`, no balance).

```
function MaybeSweep(view, vault, j) -> TER:
    dustBalance = accountHolds(view, vault.sle[sfDustAccount], vault.asset, ...)
    if dustBalance == 0:
        return tesSUCCESS

    e = effective(vault.AssetsTotal)
    candidate = truncate(dustBalance, e)
    if candidate == 0:
        return tesSUCCESS   # not enough accumulated yet

    # NOT AddAssetsToVault (see below). `from` is the Dust Pseudo-account
    # itself; assetsTotalDelta is omitted (None) so it defaults to
    # whatever this call's own `settled` turns out to be — swept dust was
    # never counted in AssetsTotal to begin with, so landing it here is
    # exactly the "new value" default case, same as Deposit.
    result = SettleAdd(view, vault, vault.sle[sfDustAccount], candidate, None, j)
    if result is error: return result.error()
    return tesSUCCESS
    # If this transfer induces further coarsening (Case 2, above),
    # SettleAdd's accountSendMulti sends the residual back to
    # vault.sle[sfDustAccount] — the same account it came from (from==to;
    # needs verifying that's a clean no-op — Appendix → Open questions).
    # Net effect on dust custody is always a decrease of exactly `settled`.
```

Why `SettleAdd`, not `AddAssetsToVault`: the latter would trigger another `MaybeSweep`, which in the
degenerate `settled == 0` case recomputes the same `candidate` against unchanged state — infinite
recursion. `SettleAdd` bounds this to one attempt; nothing else calls `MaybeSweep` from inside
itself.

`MaybeSweep` never rejects or blocks — it strictly improves (or leaves unchanged) how much of
`AssetsTotal` is backed by main custody.

##### Remove assets from Vault

**Precondition:** same shape as above; callers are Withdraw, `VaultClawback`, Loan
issuance/acceptance — all already rounded to `effective(AssetsTotal)` before calling. No Case-1
analogue (no caller targets a different, finer precision).

No Case-2 analogue either — the key asymmetry: removing only ever **shrinks** `AssetsTotal`, and
effective scale is non-increasing in magnitude, so `effective(AssetsTotal - amount) >=
effective(AssetsTotal)` always. A removal can only hold the scale steady or refine it, never coarsen
it. **Removing assets never generates dust.**

Fund movement mirrors the algorithm above: a single-destination `accountSend` out of main custody,
the same shape `doWithdraw` (`View.cpp`) already uses for Withdraw/`LoanBrokerCoverWithdraw`.
Destination-specific setup (trust-line/MPToken creation, `verifyDepositPreauth`) stays the caller's
job, same split as `VaultWithdraw::doApply`/`doWithdraw` today.

**Terminal withdrawal: all `Dust` settles to the last withdrawer.** The Dust section above
established `Dust` counts toward every shareholder's value, and also explains why that's never a
live problem for non-terminal withdrawers: `MaybeSweep` keeps `Dust` folded back into `AssetsTotal`
as soon as it's big enough to move any settlement, so anyone withdrawing while other shares remain
outstanding is pricing against a total that's already either recognized `Dust` for real or is
carrying an amount too small to change their payout. What's left once a withdrawal drains the Vault
to zero is only the residual that's _structurally_ un-sweepable — below one representable unit at
closure, with no future operation left to ever cross that threshold, and no other shareholder left to
divide it with. That's genuinely the one case needing a special rule: pay the whole residual to the
one withdrawer closing out the Vault, rather than leave it stranded forever. This also leaves `Dust`
custody at zero, which `VaultDelete` needs anyway (_Dust Pseudo-account lifecycle_, below).

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

Why check both `AssetsTotal == 0` and `SharesTotal == 0`: they should move to zero together on a
full redemption, but are tracked independently — requiring both avoids treating a same-value
coincidence as "terminal" when shares are still outstanding.

Why call `MaybeSweep` on the non-terminal path despite removal never creating dust: removal _refines_
the effective scale, which can retroactively unlock a sweep that wasn't previously possible (a
smaller required unit). Depositing calls it because an addition may have _created_ dust; withdrawal
calls it because a removal may have _unlocked_ dust already sitting there.

#### Dust Pseudo-account lifecycle

Only **IOU Vaults** ever need one — XRP/MPT force `sfPrecision` to 0, so dust routing never applies.
Like any ledger object, it carries an owner-reserve cost.

**Decision: created eagerly, at Vault creation.** Every IOU Vault gets it up front, used or not. This
was chosen over lazily creating it on first dust because lazy creation has a griefing vector specific
to it — its address stays publicly predictable for the Vault's whole life while creation timing is
unpredictable, letting an attacker pre-squat the address and fail a mandatory repayment. Eager
creation collapses the window to ordinary transaction-propagation risk, same as every other
pseudo-account. Full analysis of the rejected lazy-creation option and the griefing vector that ruled
it out live in the Appendix, for the record; what follows is what's actually built.

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

Both accounts use the same seed, `vault->key()`; the retry loop assigns main → index 0, dust → index
1, entirely within one atomic transaction — no gap between the seed becoming known and either address
being claimed, so the griefing window described in the Appendix doesn't exist here.

**Tracking, in full:**

- `ltVAULT.sfAccount` → main Pseudo-account (existing, required).
- `ltVAULT.sfDustAccount` → Dust Pseudo-account (optional; present iff IOU). Set once at
  `VaultCreate`, never reassigned.
- `ltACCOUNT_ROOT.sfVaultID`/`sfVaultDustID` — reverse links, distinguishing the two roles per Vault
  (see Appendix → _Storage and key generation_).
- **Lifecycle symmetry:** since `sfDustAccount` is now unconditional for every IOU Vault, whatever
  deletes the main Pseudo-account on `VaultDelete` must be extended to delete the Dust
  Pseudo-account too, under the same invariants (e.g. zero balance). Worked out next.

##### `VaultDelete`: cleaning up the Dust Pseudo-account

`VaultDelete::preclaim` already requires `sfAssetsAvailable == 0`, `sfAssetsTotal == 0`, and zero
outstanding shares before allowing deletion. The terminal-withdrawal rule above means `Dust` is
drained to zero by the same event that drives `AssetsTotal`/`SharesTotal` to zero — so by the time
`preclaim`'s checks pass, the Dust Pseudo-account should already be empty. "Should" isn't
"guaranteed by construction independent of bugs elsewhere," though, so `doApply` re-verifies it
directly, the same defensive-depth style already used for the main Pseudo-account (re-checking
balance/owner-count/directory right before erasing, even though `preclaim` already implies zero).

`doApply` today, for the main Pseudo-account: `removeEmptyHolding` to destroy its trust-line/MPToken
holding, destroy the share issuance, confirm the pseudo-account's owner directory is empty, confirm
zero balance/owner count/no directory, erase it, then `decreaseOwnerCountForObject(view(), owner,
vault, 2, j_)` for the two objects destroyed (Vault + main Pseudo-account).

Extension, mirrored for the Dust Pseudo-account, conditional on `vault.sle[sfDustAccount]` being
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

The Dust Pseudo-account never held a share-issuance-style object of its own (only ever a plain asset
holding, same as the main account), so there's no analogue to the share-issuance-destruction block —
just the holding, the balance/owner-count/directory checks, and the erase.

## 4. Appendix

### 4.1 Why `DebtTotal` doesn't need this

`ltLOAN_BROKER.sfDebtTotal` has its own rounding mechanism, already in production, that looks
structurally similar to the problem the Dust mechanism solves: `adjustImpreciseNumber`
(`LendingHelpers.h`) rounds every adjustment to `DebtTotal` to the Vault's current `vaultScale`, and
its own comment at the `LoanPay.cpp` call site says so plainly — "despite our best efforts, it's
possible for rounding errors to accumulate in the loan broker's debt total... because the broker may
have more than one loan with significantly different scales." That's the same shape of problem: an
aggregate bounded by the 16-sig-digit ceiling, fed by inputs at multiple different fixed scales,
coarsening as it grows. Worth stating explicitly why this document doesn't extend `Dust`/`SettleAdd`
to cover it too:

- **`DebtTotal` isn't custodied money.** `AssetsTotal` must reconcile exactly with a real balance —
  the Vault's Pseudo-account — which is why a dropped remainder needs somewhere to go (`Dust`).
  `DebtTotal` is a receivables _estimate_ (how much principal this broker's loans still owe); no
  account balance anywhere corresponds to it, so there's nothing for its rounding error to
  misplace.
- **The error is symmetric, not a one-directional drain.** `Number`'s default rounding mode is
  `ToNearest`, so `DebtTotal`'s accumulated error random-walks rather than systematically leaking in
  one direction.
- **Its one safety-relevant consumer already rounds conservatively.** `minimumBrokerCover` rounds
  its result _up_, specifically "to be conservative... ensures `CoverAvailable` never drops below
  the theoretical minimum" — so any drift in `DebtTotal` can only make the required cover look
  slightly larger than the true debt, never smaller, which is the safe direction for solvency.
- **It predates this design and isn't changed by it.** This is existing, already-accepted behavior,
  not something introduced or made worse by `sfPrecision`/`Dust` — nothing in this design touches how
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

- **Does `assetsTotalDelta` (Technical Details → _Loan repayment_ / _Add assets to Vault_) need its
  own dust/precision routing?** For a cash-basis repayment it's `interestPaid`, and `SettleAdd` only
  does `vault.AssetsTotal += assetsTotalDelta`, with no routing, unlike `amount`. Under cash-basis
  accounting ([#7817](https://github.com/XRPLF/rippled/pull/7817)), `interestPaid` is what actually
  drives `AssetsTotal` growth at repayment — so it's the value that could exceed the 16-sig-digit
  ceiling `SettleAdd` exists to protect against, not principal. Currently assumed small enough to add
  cleanly; not verified.
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

- Audit Loan/LoanBroker code paths for assumptions of dynamic Vault scale — confirm `loan.precision`
  is captured at funding (Technical Details → _Loan issuance_), not recomputed per-operation.
- Resolve the open questions above before finalizing the dust-custody mechanism.

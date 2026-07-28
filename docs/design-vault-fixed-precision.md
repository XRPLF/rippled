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

## 2. Mechanism

### 2.1 `sfPrecision` — a ceiling, not an absolute value

A new field, `sfPrecision`, set once at Vault creation, immutable thereafter. **Not** the existing
`sfScale` field — `sfScale` is already the fixed assets↔shares conversion factor used today in
`VaultHelpers.cpp`; `sfPrecision` is new, and is this design's rounding ceiling:

```
effective(AssetsTotal) = min(sfPrecision, maxPrecisionRepresentableGiven(AssetsTotal, 16-sig-digit budget))
```

Degradation is strictly downward from the ceiling — the Vault never rounds finer than `sfPrecision`.
Integrators get a deterministic contract: the finest precision ever used, computable from
`AssetsTotal` and `sfPrecision` rather than an opaque derivation. It doesn't resolve the 16-digit
collision on internal running totals — that's §2.2.

### 2.2 Error Accumulation (Kahan Summation & Vault Dust Custody)

Lets a mandatory operation succeed in full when `sfPrecision` can't currently be honored, without
truncating value or rejecting the tx.

- **Ledger tracking (SAV):** a `Dust` accumulator, stored as a new field **`sfDust`** on `ltVAULT`
  (Kahan-style error term). Whenever an amount rounds down from `sfPrecision` to the effective
  scale, the dropped remainder adds into `sfDust` — an exact, auditable record of deferred value.
  (Written as `Dust`/`vault.Dust` elsewhere for readability; same field.)
- **Custody routing:** the representable portion flows to the main Pseudo-account as normal; the
  unrepresentable fraction flows to a dedicated **Dust Pseudo-account** for that Vault.
- **The Sweep:** once dust custody accumulates a whole representable unit, it's transferred cleanly
  into the main Pseudo-account.

Invoked only where §3 says so — not a general-purpose remainder catcher for ordinary rounding.

**`Dust` is real value, and must count as such — for both totals, not just one.** `SettleAdd` (§4.1)
only adds the _settled_ portion into `AssetsTotal`; the dust portion sits, for real, in the Dust
Pseudo-account. If the figures exposed for NAV/share pricing and liquidity don't include it, both
silently undercount the Vault:

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

**Scope note on `AssetsAvailable`.** The fuller share-pricing model (`VaultSharePricing_test.cpp`
PoC) also tracks `interestUnrealized`/`lossUnrealized`, under which `AssetsTotal` and
`AssetsAvailable` can diverge — e.g. funding a loan moves principal out of `AssetsAvailable` without
touching `AssetsTotal`, since the loan remains an asset, just an illiquid one. **This document
doesn't model that bucket** — it's scoped to precision/dust. Within that scope, every operation in
§3 moves `AssetsAvailable` by the same `settled` delta as `AssetsTotal` (§4). Wiring into the full
interest-accrual model may require Loan funding/repayment to diverge the two — flagged in §5, not
resolved here.

## 3. Case by case: how rounding actually works

### 3.1 User fund movement — Deposit, Withdraw, `VaultClawback`

**Optional operations** — round directly to the effective scale, no `sfPrecision`-first attempt:

```
amount_settled = round(amount_requested, effective(AssetsTotal))
```

Representable by construction, so §2.2 never fires — no `Dust` entry, no custody routing. Acceptable
because none of these are obligations owed in full.

### 3.2 Loan issuance (funding) — Vault → Loan

Funding is a Vault-initiated disbursement, not a debt obligation — structurally a withdrawal.
Follows the §3.1 rule: rounds directly to the Vault's _current_ effective scale, no dust.

Consequence: **the Loan's own precision is fixed once, at funding time, to the effective scale used
for that disbursement** — not the Vault's raw `sfPrecision` ceiling:

```
loan.precision = effective(AssetsTotal at funding time)   [ = min(sfPrecision, ...) ]
```

Stored as the existing **`sfLoanScale`** field on `ltLOAN` (set in `LoanSet.cpp`, read in
`LoanPay.cpp`/`LoanManage.cpp`) — same relationship as `sfPrecision`/`sfScale` on the Vault side:
the field already exists, what's new is _when_ and _to what value_ it gets set (this funding-time
rule, not whatever governs it today).

Immutable thereafter, same as `sfPrecision` is for the Vault. Operating a Loan at finer precision
than it was actually funded with would assert false precision across all its future accounting.

**Worked example.** Vault `sfPrecision` = 6. Loan A funds while `AssetsTotal` is small (effective
scale 6) → `loan.precision` = 6. Later `AssetsTotal` grows enough to degrade effective scale to 5;
Loan B funds then → `loan.precision` = 5, permanently — same Vault, two Loans, two fixed precisions,
set by Vault size at each funding moment.

#### 3.2.1 Pending Loans: acceptance can straddle a scale change

Issuance is two steps: **create** (amount agreed/reserved, funds not yet moved) and **accept**
(funds move). "Funding time" above is really **acceptance time**.

Between create and accept, `AssetsTotal` can degrade the effective scale enough that the agreed
amount is no longer representable. Two options:

- Silently re-round at acceptance — rejected: mutates agreed loan terms invisibly, exactly the
  non-determinism this design eliminates.
- **Reject the acceptance**, forcing re-creation/re-quote — worse for the borrower short-term, but
  fails loudly instead of silently mutating terms. **Chosen.**

So a pending Loan's amount is re-validated against `effective(AssetsTotal)` at acceptance; on
failure, acceptance is rejected outright, never adjusted.

### 3.3 Loan repayment — Loan → Vault

**Mandatory operation.** Two-stage:

1. Round to the Loan's own fixed `loan.precision` (§3.2) — may already be coarser than the Vault's
   current `sfPrecision` ceiling.
2. Check representability at the Vault's _current_ `effective(AssetsTotal)`:
   - `loan.precision <= effective(AssetsTotal)`: settles in full, no dust.
   - Otherwise: split. The representable portion settles into main custody; the delta routes to the
     Dust Pseudo-account, recorded in `Dust` (§2.2).

The only case that invokes dust routing. The borrower's payment is always collected in full — the
split is a custody-routing detail, invisible to the borrower's accounting.

**Principal and interest hit `AssetsTotal` differently, so `AddAssetsToVault`/`SettleAdd` take a
separate `interest` parameter.** `AddAssetsToVault(view, vault, from, amount, interest, j)`:
`amount` (principal) goes through the normal dust/precision-routed path exactly as before —
`AssetsAvailable` moves in lockstep, dust possible. `interest` only increments `vault.AssetsTotal`
directly (§4.1) — no dust routing, no `AssetsAvailable` change, no transfer of its own (it's a
bookkeeping-only split on top of the same real cash movement as `amount`, not a second transfer).

Why they need separating: this mirrors `LendingProtocolV1_1`'s cash-basis accounting model
([#7817](https://github.com/XRPLF/rippled/pull/7817)) — `AssetsTotal` tracks **principal only**, and
interest is recognized **only once paid**, not at origination. Principal returning at repayment
doesn't grow `AssetsTotal` (already counted as a receivable since funding); only interest is
genuinely new value. Folding both into one undifferentiated `settled` value, as this doc originally
did, would have made `AssetsTotal` cash-basis-wrong by construction.

**Open question this raises, not resolved here:** dust/precision routing exists to protect
`AssetsTotal` additions from exceeding the 16-sig-digit ceiling (§2.2). Under cash-basis, `interest`
— not principal — is what actually drives `AssetsTotal` growth on repayment, so it's the value that
could need the same two-stage truncation `SettleAdd` already does for `amount`. This document gives
`interest` no dust routing of its own (§5) — assumed small enough to add cleanly, not verified.

## 4. Algorithms

Two shared primitives, invoked by every fund-moving operation in §3 instead of each implementing its
own settlement/dust logic: **add assets to Vault**, **remove assets from Vault**.

### 4.1 Add assets to Vault

**Precondition:** the caller has already rounded the amount to _some_ target scale (Deposit →
`effective(AssetsTotal)`; repayment → `loan.precision`). This algorithm only decides how much of
that amount `AssetsTotal` can absorb right now, and routes the rest.

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
(§4.2).

Real fund movement goes through `accountSend`/`accountSendMulti` (`TokenHelpers.h`), same as every
existing transactor. `LoanPay.cpp` already does this exact split-destination move in one call —
settling to the Vault's Pseudo-account and the broker's payee simultaneously via `accountSendMulti`.
`SettleAdd`'s main/dust split is the same shape: one sender, up to two destinations, atomic.

```
function SettleAdd(view, vault, from, amount, interest, j) -> Expected<Number, TER>:
    # amount already rounded to caller's target scale; may or may not
    # fit the Vault's current effective scale. Never triggers a Sweep.
    # `interest` (default 0) ONLY increments vault.AssetsTotal, below —
    # no dust routing, no AssetsAvailable change, no separate transfer.

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

    vault.AssetsTotal += settled
    vault.AssetsAvailable += settled   # §2.2 scope note: moves in lockstep
                                        # with AssetsTotal within this doc
    if dust > 0:
        vault.Dust += dust
    vault.AssetsTotal += interest   # separate from settled/dust above

    return settled   # callers needing the actually-landed amount (e.g.
                      # share issuance math) must use this, not `amount`


function AddAssetsToVault(view, vault, from, amount, interest, j) -> Expected<Number, TER>:
    settledResult = SettleAdd(view, vault, from, amount, interest, j)
    if settledResult is error: return settledResult
    MaybeSweep(view, vault, j)   # §4.2 — cheap no-op if dust is insufficient
    return settledResult
```

Notes:

- `truncate(x, scale)` rounds down only — dust is always non-negative, never created in the caller's
  favor.
- Deposit's precondition guarantees `dust_0 = 0`; only `dust_1` can fire for it. Repayment is the
  case where `dust_0` is the common one. Same function handles both.
- `dust > 0` is only possible when `vault.sle[sfDustAccount]` exists — i.e. an IOU Vault (§4.4.4).
  XRP/MPT Vaults never produce dust; that falls out of the truncation math, not an extra check.
- `WaiveTransferFee::Yes` matches every existing Vault-related transfer — an issuer's transfer rate
  shouldn't eat into internal accounting.

### 4.2 `MaybeSweep`

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
    # itself; a sweep never carries an interest component.
    result = SettleAdd(view, vault, vault.sle[sfDustAccount], candidate, 0, j)
    if result is error: return result.error()
    return tesSUCCESS
    # If this transfer induces further coarsening (§4.1 Case 2),
    # SettleAdd's accountSendMulti sends the residual back to
    # vault.sle[sfDustAccount] — the same account it came from (from==to;
    # needs verifying that's a clean no-op — §5). Net effect on dust
    # custody is always a decrease of exactly `settled`.
```

Why `SettleAdd`, not `AddAssetsToVault`: the latter would trigger another `MaybeSweep`, which in the
degenerate `settled == 0` case recomputes the same `candidate` against unchanged state — infinite
recursion. `SettleAdd` bounds this to one attempt; nothing else calls `MaybeSweep` from inside
itself.

`MaybeSweep` never rejects or blocks — it strictly improves (or leaves unchanged) how much of
`AssetsTotal` is backed by main custody.

### 4.3 Remove assets from Vault

**Precondition:** same shape as §4.1; callers are Withdraw, `VaultClawback`, Loan
issuance/acceptance — all already rounded to `effective(AssetsTotal)` before calling. No §4.1-Case-1
analogue (no caller targets a different, finer precision).

No §4.1-Case-2 analogue either — the key asymmetry: removing only ever **shrinks** `AssetsTotal`,
and effective scale is non-increasing in magnitude, so `effective(AssetsTotal - amount) >=
effective(AssetsTotal)` always. A removal can only hold the scale steady or refine it, never coarsen
it. **Removing assets never generates dust.**

Fund movement mirrors §4.1: a single-destination `accountSend` out of main custody, the same shape
`doWithdraw` (`View.cpp`) already uses for Withdraw/`LoanBrokerCoverWithdraw`. Destination-specific
setup (trust-line/MPToken creation, `verifyDepositPreauth`) stays the caller's job, same split as
`VaultWithdraw::doApply`/`doWithdraw` today.

**Terminal withdrawal: all `Dust` settles to the last withdrawer.** §2.2 established `Dust` counts
toward every shareholder's value — but while _other_ shares remain outstanding, there's no clean way
to pay out one depositor's fractional slice of unswept dust without leaving the rest ambiguous (§5).
That ambiguity disappears the moment a withdrawal drains the Vault to zero: no other shareholders
remain to divide anything with, so the entire `Dust` balance pays out to the one withdrawer closing
out the Vault. This also leaves `Dust` custody at zero, which `VaultDelete` needs anyway (§4.4.4).

```
function RemoveAssetsFromVault(view, vault, to, amount, j) -> Expected<Number, TER>:
    # amount already rounded to effective(vault.AssetsTotal); sufficiency
    # checked upstream (preclaim) — not this algorithm's concern.

    ter = accountSend(view, vault.sle[sfAccount], to, amount, j, {}, WaiveTransferFee::Yes)
    if ter is not tesSUCCESS:
        return Unexpected(ter)

    vault.AssetsTotal -= amount
    vault.AssetsAvailable -= amount   # §2.2 scope note: lockstep with AssetsTotal

    if vault.AssetsTotal == 0 and vault.SharesTotal == 0 and vault.sle[sfDustAccount] is present:
        # Terminal: no other shareholder to preserve Dust for, so it
        # doesn't go through truncate()/effective() at all — just pay out
        # whatever's left, in full.
        dustBalance = accountHolds(view, vault.sle[sfDustAccount], vault.asset, ...)
        if dustBalance > 0:
            ter = accountSend(view, vault.sle[sfDustAccount], to, dustBalance, j, {}, WaiveTransferFee::Yes)
            if ter is not tesSUCCESS:
                return Unexpected(ter)
            vault.Dust -= dustBalance
    else:
        MaybeSweep(view, vault, j)   # non-terminal: still worth attempting

    return amount   # always settles in full
```

Why check both `AssetsTotal == 0` and `SharesTotal == 0`: they should move to zero together on a
full redemption, but are tracked independently — requiring both avoids treating a same-value
coincidence as "terminal" when shares are still outstanding.

Why call `MaybeSweep` on the non-terminal path despite removal never creating dust: removal
_refines_ the effective scale, which can retroactively unlock a sweep that wasn't previously
possible (a smaller required unit). §4.1 calls it because an addition may have _created_ dust; §4.3
because a removal may have _unlocked_ dust already sitting there.

### 4.4 When is the Dust Pseudo-account created?

Only **IOU Vaults** ever need one — XRP/MPT force `sfPrecision` to 0, so dust routing never applies
(§4.1/§4.3). Like any ledger object, it carries an owner-reserve cost.

**Option A — eager, at Vault creation.** Every IOU Vault gets it up front, used or not.

- Simple: settlement logic never branches on "does it exist yet."
- Costs reserve for a resource many Vaults may never use.
- Reserve charged normally as part of `VaultCreate` — explicit, user-initiated; failing it blocks
  nothing mandatory.

**Option B — lazy, on first dust.** Created the first time `SettleAdd` needs to defer a nonzero
remainder (only ever inside a repayment).

- Reserve spent only if actually needed.
- More complex: settlement must handle "no dust account yet."
- **Reserve must be forgiven**: creation is now a side effect of a _mandatory_ operation. Failing it
  would either reject a repayment for a reserve shortfall unrelated to the borrower (§1.2's exact
  anti-pattern) or leave inconsistent state — so this object is exempt from the usual reserve rule.
  Bounded exposure: at most one unfunded object per Vault.

**Decision: Option A.** §4.4.3 found a griefing vector specific to lazy creation — its address stays
publicly predictable for the Vault's whole life while creation timing is unpredictable, letting an
attacker pre-squat the address and fail a mandatory repayment. Eager creation collapses the window
to ordinary transaction-propagation risk, same as every other pseudo-account. §4.4.1 and §4.4.3 are
kept below for the record; §4.4.4 is what's actually built.

#### 4.4.1 Option B mechanics (rejected — see §4.4.3)

Kept for the record.

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
    ... (unchanged truncation math from §4.1) ...
    if dust > 0:
        dustAccountId = EnsureDustPseudoAccount(view, vault)   # lazily, on first dust
        vault.Dust += dust
        DustPseudoAccount(dustAccountId).balance += dust
    ...
```

Consequence: whichever repayment happens to be first pays a one-time `ACCOUNT_ROOT`-creation cost no
other repayment on that Vault pays — asymmetric, but bounded to once per Vault.

#### 4.4.2 Storage and key generation (background, applies to either option)

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
pseudo-account already uses. The only choice is the `pseudoOwnerKey` seed: **Option A** reuses
`vault->key()` unchanged (the retry loop naturally lands on the next free address after the main
account); **Option B** derives an explicitly distinct seed (e.g. `sha512Half(vault->key(),
<discriminator>)`). **Both are equally vulnerable to §4.4.3's griefing attack** — the vulnerability
is about _when_ creation happens relative to how long the seed's been public, not which formula
computes it.

#### 4.4.3 Address predictability is a griefing vector under lazy creation

`pseudoAccountAddress` hashes only public inputs: `vault->key()` (public and permanent once the
Vault exists) and `parentHash` (public the instant a ledger closes). Fine for the **main**
Pseudo-account, created atomically with `ltVAULT` — the only exposure is ordinary
transaction-propagation time.

Not fine for a **lazily**-created Dust Pseudo-account: the seed is public for the Vault's entire
remaining life while creation timing stays unpredictable. Attack:

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
(§4.4.2) fixes lazy creation's exposure. **Decision: eager (§4.4.4).**

#### 4.4.4 Option A, worked out: eager creation and tracking

Creation moves entirely into `VaultCreate` — no lazy-check anywhere in the settlement path; by the
time any `SettleAdd` runs, the Dust Pseudo-account (for an IOU Vault) already exists.

```
function VaultCreate(view, ...):
    ... (existing logic: build the ltVAULT SLE, ...)

    mainPseudo = createPseudoAccount(view, vault->key(), sfVaultID)
    if mainPseudo is error: return mainPseudo.error()
    vault.sle[sfAccount] = mainPseudo->key()
    # ... existing reserve accounting for the main Pseudo-account ...

    if vault.asset is IOU:                      # §4.4: IOU-only
        dustPseudo = createPseudoAccount(view, vault->key(), sfVaultDustID)
        if dustPseudo is error: return dustPseudo.error()
        vault.sle[sfDustAccount] = dustPseudo->key()
        # Normal reserve check/increment, same as the main account above —
        # no carve-out needed: VaultCreate is explicit, blocks nothing
        # mandatory.

    ...
```

Both accounts use the same seed, `vault->key()`; the retry loop assigns main → index 0, dust → index
1, entirely within one atomic transaction — no gap between the seed becoming known and either
address being claimed, so §4.4.3's window doesn't exist here.

**Tracking, in full:**

- `ltVAULT.sfAccount` → main Pseudo-account (existing, required).
- `ltVAULT.sfDustAccount` → Dust Pseudo-account (optional; present iff IOU). Set once at
  `VaultCreate`, never reassigned.
- `ltACCOUNT_ROOT.sfVaultID`/`sfVaultDustID` — reverse links, distinguishing the two roles per Vault
  (§4.4.2).
- **Lifecycle symmetry:** since `sfDustAccount` is now unconditional for every IOU Vault, whatever
  deletes the main Pseudo-account on `VaultDelete` must be extended to delete the Dust
  Pseudo-account too, under the same invariants (e.g. zero balance). Worked out in §4.4.5.

#### 4.4.5 `VaultDelete`: cleaning up the Dust Pseudo-account

`VaultDelete::preclaim` already requires `sfAssetsAvailable == 0`, `sfAssetsTotal == 0`, and zero
outstanding shares before allowing deletion. §4.3's terminal-withdrawal rule means `Dust` is drained
to zero by the same event that drives `AssetsTotal`/`SharesTotal` to zero — so by the time
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

## 5. Open questions / not yet resolved

- **Does `interest` (§3.3/§4.1) need its own dust/precision routing?** `AddAssetsToVault`'s
  `interest` parameter only does `vault.AssetsTotal += interest`, with no routing, unlike `amount`.
  Under cash-basis accounting ([#7817](https://github.com/XRPLF/rippled/pull/7817)), `interest` is
  what actually drives `AssetsTotal` growth at repayment — so it's the value that could exceed the
  16-sig-digit ceiling `SettleAdd` exists to protect against, not principal. Currently assumed small
  enough to add cleanly; not verified.
- **No invariant-check design.** Candidates implied by the design but never stated: Dust
  Pseudo-account's real balance ≡ `vault.Dust`; post-`MaybeSweep`, dust custody never exceeds one
  representable unit at the current effective scale; a global conservation identity across
  `AssetsTotal`/`Dust`/`AssetsAvailable`.
- **`sfPrecision` immutability asserted, not enforced.** No confirmation `VaultSet` rejects attempts
  to change it post-creation.
- **PoC test file is stale.** Predates the `Dust` rename, eager-creation decision (§4.4.4),
  terminal-withdrawal rule (§4.3), and real `accountSend` integration (§4.1–§4.3).
- **`LoanPay.cpp`'s real repayment splits three ways** (vault main custody, vault dust custody,
  broker payee); `SettleAdd`'s `accountSendMulti` only models the first two. Whether the broker-fee
  leg composes alongside `SettleAdd` or stays entirely the caller's own separate transfer was never
  spelled out.

## 6. Next steps

- Audit Loan/LoanBroker code paths for assumptions of dynamic Vault scale — confirm `loan.precision`
  is captured at funding (§3.2), not recomputed per-operation.
- Resolve §5 before finalizing the dust-custody mechanism.


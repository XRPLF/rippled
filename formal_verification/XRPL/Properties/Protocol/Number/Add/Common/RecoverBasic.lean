import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Add.Common.AlignDown
import XRPL.Properties.Protocol.Number.Rounding.Guard
import XRPL.Properties.Protocol.Number.Rounding.ScaleDown

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Invariants for the `recover` nested helper

The different-sign branch of `Number.operator_add` uses a nested
`let rec recover (m : UInt64) (e : Int) (g : Guard) (fuel : Nat)` to
"pull back" digits from the guard into the mantissa, restoring
normalization after a subtraction-of-near-equals cancelled high digits.

Each loop body executes one `Guard.pop` (extracting the top nibble `d`),
multiplies the mantissa by 10, subtracts `d`, and decrements the exponent.
The loop exits when the mantissa is no longer "small" — concretely when
either `m ≥ largeRange.min` or `m * 10 > maxRep`.

This block proves the algorithmic invariants of `recover` that downstream
proofs reuse:

* `recover_noop_zero` / `recover_noop_exit` / `recover_step` — equation
  lemmas (the three branches of the recursion).
* `recover_exit_idemp` — once the exit condition holds, any fuel is a no-op.
* `pop_digit_eq_nibble_15` / `pop_digit_lt_16` / `pop_digit_le_9` —
  bounds on the popped digit.
* `mul_ten_no_overflow_of_lt_lr_min` / `pop_digit_le_mul_ten_of_pos` /
  `recover_step_mantissa_nat` — nat-level mantissa step formula when there
  is no UInt64 wrap-around.
* `recover_exponent_le` / `recover_exponent_ge` / `recover_exponent_eq` —
  exponent bounds: output exponent is in `[e - fuel, e]`.
* `recover_output_exits_or_fuel_exhausted` — exit characterization: if the
  output doesn't satisfy the exit condition, exactly `fuel` steps ran.
* `recover_fuel_add_of_exits` — fuel monotonicity: once the exit is reached,
  extra fuel doesn't change the output. -/

/-! ## Equation lemmas for `recover` -/

/-- No-op case: `recover` with no fuel returns its inputs. -/
lemma recover_noop_zero (m : UInt64) (e : Int) (g : Guard) :
    Number.operator_add.recover m e g 0 = (m, e, g) := by
  rw [Number.operator_add.recover.eq_def]

/-- No-op case: when the exit condition holds, `recover` returns its inputs
regardless of remaining fuel. -/
lemma recover_noop_exit {m : UInt64} {e : Int} {g : Guard} {fuel : ℕ}
    (h : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep)) :
    Number.operator_add.recover m e g (fuel + 1) = (m, e, g) := by
  rw [Number.operator_add.recover.eq_def]
  have hbool : (decide (m < largeRange.min) && decide (m * 10 ≤ maxRep)) ≠ true := by
    intro heq
    rw [Bool.and_eq_true] at heq
    obtain ⟨h1, h2⟩ := heq
    exact h ⟨of_decide_eq_true h1, of_decide_eq_true h2⟩
  change (if (decide (m < largeRange.min) && decide (m * 10 ≤ maxRep)) = true
          then _ else (m, e, g)) = (m, e, g)
  rw [if_neg hbool]

/-- Step case: when fuel > 0 and the loop condition holds, `recover` unrolls
one `Guard.pop` step. -/
lemma recover_step {m : UInt64} {e : Int} {g : Guard} {fuel : ℕ}
    (h1 : m < largeRange.min) (h2 : m * 10 ≤ maxRep) :
    Number.operator_add.recover m e g (fuel + 1)
      = Number.operator_add.recover (m * 10 - g.pop.2) (e - 1) g.pop.1 fuel := by
  rw [Number.operator_add.recover.eq_def]
  have hbool : (decide (m < largeRange.min) && decide (m * 10 ≤ maxRep)) = true := by
    rw [Bool.and_eq_true]
    exact ⟨decide_eq_true h1, decide_eq_true h2⟩
  change (if (decide (m < largeRange.min) && decide (m * 10 ≤ maxRep)) = true then
            match g.pop with
            | (g', d) => Number.operator_add.recover (m * 10 - d) (e - 1) g' fuel
          else (m, e, g))
        = Number.operator_add.recover (m * 10 - g.pop.2) (e - 1) g.pop.1 fuel
  rw [if_pos hbool]

/-- Once the exit condition holds at the entry, `recover` is a no-op for any fuel. -/
lemma recover_exit_idemp {m : UInt64} {e : Int} {g : Guard} {fuel : ℕ}
    (h : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep)) :
    Number.operator_add.recover m e g fuel = (m, e, g) := by
  cases fuel with
  | zero => exact recover_noop_zero m e g
  | succ n => exact recover_noop_exit h

/-- `recover` preserves the guard's sticky bit (`Guard.pop` only updates
`digits_`). -/
theorem recover_sbit (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ) :
    (Number.operator_add.recover m e g fuel).2.2.sbit_ = g.sbit_ := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g => rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2, IH, ← hpop1]
    rfl
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_noop_exit hprop]

/-- `recover` preserves the guard's overflow bit (`Guard.pop` keeps `xbit_`). -/
theorem recover_xbit (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ) :
    (Number.operator_add.recover m e g fuel).2.2.xbit_ = g.xbit_ := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g => rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2, IH, ← hpop1]
    rfl
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_noop_exit hprop]

/-- `recover` preserves an all-zero `digits_` field (`Guard.pop` of a guard with
no packed digits leaves the packed-digit field zero). -/
theorem recover_digits_zero (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hg : g.digits_ = 0) :
    (Number.operator_add.recover m e g fuel).2.2.digits_ = 0 := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g => rw [recover_noop_zero]; exact hg
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hg' : g'.digits_ = 0 := by
      rw [← hpop1]; unfold Guard.pop; simp only; rw [hg]; rfl
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    exact IH hg'
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_noop_exit hprop]; exact hg

/-! ## Pop digit bounds

(`pop_digit_eq_nibble_15` and its `≤ 9` consequence are now proved in
`XRPL/Number/Properties/Rounding/Guard.lean` alongside the other
`Guard.pop` algebraic identities. We re-state the remaining bound here
for backwards compatibility with downstream proofs.) -/

/-- The popped digit is strictly less than 16 (it's a nibble). -/
lemma pop_digit_lt_16 (g : Guard) : g.pop.2.toNat < 16 := by
  rw [pop_digit_eq_nibble_15]
  unfold nibble
  exact Nat.mod_lt _ (by decide)

/-- If all nibbles of `g.digits_` are valid decimal digits (≤ 9), then
the popped digit is at most 9. -/
lemma pop_digit_le_9 (g : Guard) (hall : allNibblesAtMost9 g.digits_) :
    g.pop.2.toNat ≤ 9 := by
  rw [pop_digit_eq_nibble_15]
  exact hall ⟨15, by omega⟩

/-! ## Mantissa-arithmetic helpers (no UInt64 wrap-around) -/

/-- `largeRange.min` in `ℕ` is `10^18`. -/
lemma largeRange_min_toNat : largeRange.min.toNat = 10 ^ 18 := by decide

/-- `maxRep.toNat` as a concrete value. -/
lemma maxRep_toNat_val : maxRep.toNat = maxRepNat := by decide

/-- When `m < largeRange.min`, the product `m * 10` does not overflow `UInt64`. -/
lemma mul_ten_no_overflow_of_lt_lr_min {m : UInt64}
    (h : m < largeRange.min) :
    (m * 10).toNat = m.toNat * 10 := by
  rw [UInt64.toNat_mul]
  have h10 : (10 : UInt64).toNat = 10 := rfl
  rw [h10]
  rw [UInt64.lt_iff_toNat_lt, largeRange_min_toNat] at h
  apply Nat.mod_eq_of_lt
  have h1 : m.toNat * 10 < 10 ^ 18 * 10 := by nlinarith
  have h2 : (10 ^ 18 * 10 : ℕ) < 2 ^ 64 := by decide
  omega

/-- A sufficient condition for the loop body's subtraction not to underflow:
if `m < largeRange.min`, `1 ≤ m.toNat`, and the popped digit `d ≤ 9`, then
`d ≤ m * 10` as `UInt64`s. -/
lemma pop_digit_le_mul_ten_of_pos {m d : UInt64}
    (hm_pos : 1 ≤ m.toNat) (hm_lt : m < largeRange.min) (hd : d.toNat ≤ 9) :
    d ≤ m * 10 := by
  rw [UInt64.le_iff_toNat_le, UInt64.toNat_mul]
  have h10 : (10 : UInt64).toNat = 10 := rfl
  rw [h10]
  rw [UInt64.lt_iff_toNat_lt, largeRange_min_toNat] at hm_lt
  have hbnd : m.toNat * 10 < 2 ^ 64 := by
    have h1 : m.toNat * 10 < 10 ^ 18 * 10 := by nlinarith
    have h2 : (10 ^ 18 * 10 : ℕ) < 2 ^ 64 := by decide
    omega
  rw [Nat.mod_eq_of_lt hbnd]
  have : (9 : ℕ) ≤ m.toNat * 10 := by nlinarith
  omega

/-- Nat-level identity for the loop body's mantissa update:
if `m < largeRange.min` (so `m * 10` does not overflow) and `d ≤ m * 10`
(so the subtraction does not underflow), then
`(m * 10 - d).toNat = m.toNat * 10 - d.toNat`. -/
lemma recover_step_mantissa_nat {m d : UInt64}
    (hm : m < largeRange.min) (hd : d ≤ m * 10) :
    (m * 10 - d).toNat = m.toNat * 10 - d.toNat := by
  rw [UInt64.toNat_sub_of_le _ _ hd, mul_ten_no_overflow_of_lt_lr_min hm]

/-! ## Exponent bounds -/

/-- Output exponent is at most the input exponent: each step decrements `e`. -/
theorem recover_exponent_le (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ) :
    (Number.operator_add.recover m e g fuel).2.1 ≤ e := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g => rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    linarith [IH]
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_noop_exit hprop]

/-- Output exponent is at least `e - fuel`: at most `fuel` steps execute. -/
theorem recover_exponent_ge (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ) :
    e - fuel ≤ (Number.operator_add.recover m e g fuel).2.1 := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g => rw [recover_noop_zero]; simp
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    have IH' := IH
    push_cast at IH' ⊢
    linarith
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_noop_exit hprop]; push_cast; omega

/-- The output exponent is exactly `e - k` for some `0 ≤ k ≤ fuel`. -/
theorem recover_exponent_eq (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ) :
    ∃ k : ℕ, k ≤ fuel ∧
      (Number.operator_add.recover m e g fuel).2.1 = e - k := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g =>
    refine ⟨0, le_refl _, ?_⟩
    rw [recover_noop_zero]; simp
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    obtain ⟨k, hk_le, hk_eq⟩ := IH
    refine ⟨k + 1, by omega, ?_⟩
    rw [hk_eq]; push_cast; ring
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    refine ⟨0, by omega, ?_⟩
    rw [recover_noop_exit hprop]; simp

/-! ## Exit characterization -/

/-- Exit characterization: either the output satisfies the loop-exit condition
(so the loop terminated naturally), or fuel was completely exhausted
(exponent decreased by exactly `fuel`). -/
theorem recover_output_exits_or_fuel_exhausted
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ) :
    let r := Number.operator_add.recover m e g fuel
    (¬ (r.1 < largeRange.min ∧ r.1 * 10 ≤ maxRep))
    ∨ r.2.1 = e - fuel := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g => rw [recover_noop_zero]; right; simp
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    rcases IH with hexit | heq
    · left; exact hexit
    · right; rw [heq]; push_cast; ring
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_noop_exit hprop]
    left; exact hprop

/-! ## Fuel monotonicity -/

/-- Once the output satisfies the exit condition, additional fuel is a no-op. -/
theorem recover_fuel_add_of_exits {m : UInt64} {e : Int} {g : Guard} {fuel : ℕ}
    (hexit : let r := Number.operator_add.recover m e g fuel
             ¬ (r.1 < largeRange.min ∧ r.1 * 10 ≤ maxRep))
    (extra : ℕ) :
    Number.operator_add.recover m e g (fuel + extra)
      = Number.operator_add.recover m e g fuel := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g =>
    rw [recover_noop_zero] at hexit
    simp only at hexit
    rw [recover_noop_zero, Nat.zero_add, recover_exit_idemp hexit]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    have hreidx : fuel + 1 + extra = (fuel + extra) + 1 := by omega
    rw [hreidx]
    rw [recover_step h1 h2]
    rw [recover_step h1 h2] at hexit
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2] at hexit
    rw [hpop1, hpop2]
    conv_rhs => rw [recover_step h1 h2, hpop1, hpop2]
    exact IH hexit
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_exit_idemp hprop, recover_exit_idemp hprop]

end XRPL.Model.Protocol

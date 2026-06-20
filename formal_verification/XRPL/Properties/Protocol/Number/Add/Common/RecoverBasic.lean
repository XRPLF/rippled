import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Add.Common.AlignDown
import XRPL.Properties.Protocol.Number.Common.Rounding.Guard
import XRPL.Properties.Protocol.Number.Common.Rounding.ScaleDown


namespace XRPL.Model.Protocol

/-! ## Equation lemmas for `recover` -/

/-- No-op case: `recover` with no fuel returns its inputs. -/
lemma recover_noop_zero (upperLimit m : UInt128) (e : Int) (g : Guard) :
    Number.operator_add.recover upperLimit m e g 0 = (m, e, g) := by
  rw [Number.operator_add.recover.eq_def]

/-- No-op case: when the exit condition holds, `recover` returns its inputs
regardless of remaining fuel. -/
lemma recover_noop_exit {upperLimit m : UInt128} {e : Int} {g : Guard} {fuel : ℕ}
    (h : ¬ (m < upperLimit ∧ ¬ g.empty)) :
    Number.operator_add.recover upperLimit m e g (fuel + 1) = (m, e, g) := by
  rw [Number.operator_add.recover.eq_def]
  simp only [if_neg h]

/-- Step case: when fuel > 0 and the loop condition holds, `recover` unrolls
one `Guard.pop` step. -/
lemma recover_step {upperLimit m : UInt128} {e : Int} {g : Guard} {fuel : ℕ}
    (h1 : m < upperLimit) (h2 : ¬ g.empty) :
    Number.operator_add.recover upperLimit m e g (fuel + 1)
      = Number.operator_add.recover upperLimit (m * 10 - toUInt128 g.pop.2) (e - 1) g.pop.1 fuel := by
  rw [Number.operator_add.recover.eq_def]
  simp only [if_pos (And.intro h1 h2)]

/-- Once the exit condition holds at the entry, `recover` is a no-op for any fuel. -/
lemma recover_exit_idemp {upperLimit m : UInt128} {e : Int} {g : Guard} {fuel : ℕ}
    (h : ¬ (m < upperLimit ∧ ¬ g.empty)) :
    Number.operator_add.recover upperLimit m e g fuel = (m, e, g) := by
  cases fuel with
  | zero => exact recover_noop_zero upperLimit m e g
  | succ n => exact recover_noop_exit h

/-- `recover` preserves the guard's overflow bit (`Guard.pop` keeps `xbit_`). -/
theorem recover_xbit (upperLimit m : UInt128) (e : Int) (g : Guard) (fuel : ℕ) :
    (Number.operator_add.recover upperLimit m e g fuel).2.2.xbit_ = g.xbit_ := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit) with
  | case1 m e g => rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2, IH, ← hpop1]
    rfl
  | case3 m e g fuel hcond =>
    rw [recover_noop_exit hcond]

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

/-- When `m < largeRange.min`, the product `m * 10` does not overflow `UInt64`. -/
lemma mul_ten_no_overflow_of_lt_lr_min {m : UInt64}
    (h : m < largeRange.min) :
    (m * 10).toNat = m.toNat * 10 := by
  rw [UInt64.toNat_mul]
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
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
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
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

/-! ## UInt128 step arithmetic (new recover loop)

The rewritten loop works on `UInt128` mantissas bounded by an `upperLimit`
(`toUInt128 largeRange.min * 1000 = 10^21` at the model's call site). The
single hypothesis `upperLimit.toNat * 10 < 2^128` makes every step's
`m * 10` overflow-free; it is discharged by `decide` at the call site. -/

/-- When `m < upperLimit` and `upperLimit.toNat * 10 < 2^128`, the product
`m * 10` does not overflow `UInt128`. -/
lemma mul_ten_no_overflow_of_lt_upperLimit {upperLimit m : UInt128}
    (hUL : upperLimit.toNat * 10 < 2 ^ 128) (h : m < upperLimit) :
    (m * 10 : UInt128).toNat = m.toNat * 10 := by
  have hlt : m.toNat < upperLimit.toNat := BitVec.lt_def.mp h
  have h10 : ((10 : UInt128)).toNat = 10 := by decide
  rw [BitVec.toNat_mul, h10]
  apply Nat.mod_eq_of_lt
  omega

/-- No-underflow: the popped digit (≤ 9) is at most `m * 10` when `1 ≤ m.toNat`. -/
lemma pop_digit_le_mul_ten_of_pos128 {upperLimit m : UInt128} {d : UInt64}
    (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (hm_pos : 1 ≤ m.toNat) (hm_lt : m < upperLimit) (hd : d.toNat ≤ 9) :
    toUInt128 d ≤ m * 10 := by
  rw [BitVec.le_def, toNat_toUInt128, mul_ten_no_overflow_of_lt_upperLimit hUL hm_lt]
  omega

/-- Nat-level identity for the new loop body's mantissa update:
`(m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat` under the no-overflow
and no-underflow side conditions. -/
lemma recover_step_mantissa_nat128 {upperLimit m : UInt128} {d : UInt64}
    (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (hm : m < upperLimit) (hd : toUInt128 d ≤ m * 10) :
    (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat := by
  rw [BitVec.toNat_sub_of_le hd, mul_ten_no_overflow_of_lt_upperLimit hUL hm,
      toNat_toUInt128]

/-! ## Exponent bounds -/

/-- Output exponent is at most the input exponent: each step decrements `e`. -/
theorem recover_exponent_le (upperLimit m : UInt128) (e : Int) (g : Guard) (fuel : ℕ) :
    (Number.operator_add.recover upperLimit m e g fuel).2.1 ≤ e := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit) with
  | case1 m e g => rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2, show g.pop = (g', d) from hpop]
    linarith [IH]
  | case3 m e g fuel hcond =>
    rw [recover_noop_exit hcond]

/-- Output exponent is at least `e - fuel`: at most `fuel` steps execute. -/
theorem recover_exponent_ge (upperLimit m : UInt128) (e : Int) (g : Guard) (fuel : ℕ) :
    e - fuel ≤ (Number.operator_add.recover upperLimit m e g fuel).2.1 := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit) with
  | case1 m e g => rw [recover_noop_zero]; simp
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2, show g.pop = (g', d) from hpop]
    have IH' := IH
    push_cast at IH' ⊢
    linarith
  | case3 m e g fuel hcond =>
    rw [recover_noop_exit hcond]; push_cast; omega

/-- The output exponent is exactly `e - k` for some `0 ≤ k ≤ fuel`. -/
theorem recover_exponent_eq (upperLimit m : UInt128) (e : Int) (g : Guard) (fuel : ℕ) :
    ∃ k : ℕ, k ≤ fuel ∧
      (Number.operator_add.recover upperLimit m e g fuel).2.1 = e - k := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit) with
  | case1 m e g =>
    refine ⟨0, le_refl _, ?_⟩
    rw [recover_noop_zero]; simp
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2, show g.pop = (g', d) from hpop]
    obtain ⟨k, hk_le, hk_eq⟩ := IH
    refine ⟨k + 1, by omega, ?_⟩
    rw [hk_eq]; push_cast; ring
  | case3 m e g fuel hcond =>
    refine ⟨0, by omega, ?_⟩
    rw [recover_noop_exit hcond]; simp

/-! ## Exit characterization -/

/-- Exit characterization: either the output satisfies the loop-exit condition
(so the loop terminated naturally), or fuel was completely exhausted
(exponent decreased by exactly `fuel`). -/
theorem recover_output_exits_or_fuel_exhausted
    (upperLimit m : UInt128) (e : Int) (g : Guard) (fuel : ℕ) :
    let r := Number.operator_add.recover upperLimit m e g fuel
    (¬ (r.1 < upperLimit ∧ ¬ r.2.2.empty))
    ∨ r.2.1 = e - fuel := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit) with
  | case1 m e g => rw [recover_noop_zero]; right; simp
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2, show g.pop = (g', d) from hpop]
    rcases IH with hexit | heq
    · left; exact hexit
    · right; rw [heq]; push_cast; ring
  | case3 m e g fuel hcond =>
    rw [recover_noop_exit hcond]
    left; exact hcond

/-! ## Fuel monotonicity -/

/-- Once the output satisfies the exit condition, additional fuel is a no-op. -/
theorem recover_fuel_add_of_exits {upperLimit m : UInt128} {e : Int} {g : Guard} {fuel : ℕ}
    (hexit : let r := Number.operator_add.recover upperLimit m e g fuel
             ¬ (r.1 < upperLimit ∧ ¬ r.2.2.empty))
    (extra : ℕ) :
    Number.operator_add.recover upperLimit m e g (fuel + extra)
      = Number.operator_add.recover upperLimit m e g fuel := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit) with
  | case1 m e g =>
    rw [recover_noop_zero] at hexit
    simp only at hexit
    rw [recover_noop_zero, Nat.zero_add, recover_exit_idemp hexit]
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
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
    rw [recover_exit_idemp hcond, recover_exit_idemp hcond]

end XRPL.Model.Protocol

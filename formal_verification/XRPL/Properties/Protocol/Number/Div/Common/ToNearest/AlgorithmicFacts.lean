import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.DivQuotient
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128


namespace XRPL.Model.Protocol

/-! # Division algorithmic decomposition (staged form, PR 7389 head)

`operator_div` routes the staged quotient through `doNormalize128` with the
`dropped` sticky flag, exactly like diff-sign addition. This file packages
`operator_div x y mode = .ok result` into the `doNormalize128` keystone
inputs `(M, ze', δ, zn, sticky)`:

* `δ = r/ym ∈ [0, 1)` is the division residual, with `sticky = false → δ = 0`
  (the staged `dropped` flag is exact);
* `1 ≤ M < 10^23` (the corrected quotient reaches `~10^23` when `xm ≥ ym`);
* the tail-vs-mantissa ratio `δ·10^20 ≤ M` holds in both stages: the corrected
  stage has `M ≥ 10^21`, and the correction-free stage has the tiny residual
  `r·10^5 < ym` (i.e. `δ < 10⁻⁵`) against `M ≥ 10^16`;
* `|x/y| = (M + δ)·10^ze'` — the value equation;
* `doNormalize128 zn M ze' largeRange.min largeRange.max mode sticky = .ok result`;
* `zn` matches the sign of the quotient.

These are exactly the inputs of `doNormalize128_rounds_to_nearest` /
`doNormalize128_rounds_any` / `doNormalize128_rounds_direction`. -/

set_option maxHeartbeats 1600000 in
-- Large existential packaging with ℚ cross-multiplication algebra.
structure DivDecomp (x y result : Number) (mode : rounding_mode)
    (M : UInt128) (ze' : Int) (δ : ℚ) (zn sticky : Bool) : Prop where
  δ_nonneg : 0 ≤ δ
  δ_le_one : δ ≤ 1
  sticky_false_δ_zero : sticky = false → δ = 0
  M_ge_one : 1 ≤ M.toNat
  M_lt : M.toNat < 10 ^ 23
  sticky_δ_bound : sticky = true → δ * 10 ^ 20 ≤ (M.toNat : ℚ)
  value_eq : |x.toRat / y.toRat| = ((M.toNat : ℚ) + δ) * 10 ^ ze'
  normalizes : doNormalize128 zn M ze' largeRange.min largeRange.max mode sticky = .ok result
  sign : (zn = true → x.toRat / y.toRat ≤ 0) ∧ (zn = false → 0 ≤ x.toRat / y.toRat)
  δ_lt_one : δ < 1
  sticky_δ_pos : sticky = true → 0 < δ

theorem operator_div_algorithmic_facts_represents
    (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y mode = .ok result) :
    ∃ (M : UInt128) (ze' : Int) (δ : ℚ) (zn sticky : Bool),
      DivDecomp x y result mode M ze' δ zn sticky := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hxm_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := (mantissaBounds_nat_of hx_bounds).1
  have hym_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := (mantissaBounds_nat_of hy_bounds).1
  have hxm_le : x.mantissa_.toNat ≤ 10 ^ 19 - 1 := by have := (mantissaBounds_nat_of hx_bounds).2; omega
  have hym_le : y.mantissa_.toNat ≤ 10 ^ 19 - 1 := by have := (mantissaBounds_nat_of hy_bounds).2; omega
  have hxm_pos : 0 < x.mantissa_.toNat := by omega
  have hym_pos : 0 < y.mantissa_.toNat := by omega
  have hx_ne_zero := Number.not_operator_eq_zero_of_mantissa_ne hx_mant_ne
  have hy_ne_zero := Number.not_operator_eq_zero_of_mantissa_ne hy_mant_ne
  -- The staged quotient contract.
  obtain ⟨zmq, zeq, dropped, N, r, heq, hN, heuc, hr_lt, hze, hdrop_iff, htiny⟩ :=
    divQuotient128_correct x.mantissa_ y.mantissa_ x.exponent_ y.exponent_
      hxm_pos hym_pos
      (by rw [largeRange_max_val]; omega)
      (by rw [largeRange_max_val]; omega)
      (by rw [largeRange_min_val]; omega)
  -- Reduce operator_div to the doNormalize128 call. Every step is equational
  -- (if_neg / the divQuotient128 tuple equation) or a small zeta+iota `change`,
  -- keeping each kernel defeq shallow (the kernel's recursion guard cannot
  -- survive a one-shot defeq against the unfolded staged body).
  unfold Number.operator_div at hok
  rw [if_neg hy_ne_zero, if_neg hx_ne_zero, heq] at hok
  change doNormalize128 (x.negative_ != y.negative_) zmq zeq
    largeRange.min largeRange.max mode dropped = Except.ok result at hok
  -- The residual fraction.
  have hym_q_pos : (0 : ℚ) < (y.mantissa_.toNat : ℚ) := by exact_mod_cast hym_pos
  set δ : ℚ := (r : ℚ) / (y.mantissa_.toNat : ℚ) with hδ_def
  have hδ_nn : 0 ≤ δ := div_nonneg (Nat.cast_nonneg _) (Nat.cast_nonneg _)
  have hδ_le : δ ≤ 1 := by
    rw [hδ_def, div_le_one hym_q_pos]
    exact_mod_cast le_of_lt hr_lt
  have hsticky_zero : dropped = false → δ = 0 := by
    intro hd
    have hr0 : r = 0 := by
      by_contra hr
      exact absurd (hdrop_iff.mpr hr) (by rw [hd]; exact Bool.false_ne_true)
    rw [hδ_def, hr0, Nat.cast_zero, zero_div]
  -- M positivity: M = 0 would force xm·10^N = r < 10^19 against ≥ 10^35.
  have hbig : 10 ^ 35 ≤ x.mantissa_.toNat * 10 ^ N := by
    rcases hN with rfl | rfl
    · calc (10 : ℕ) ^ 35 = 10 ^ 18 * 10 ^ 17 := by norm_num
        _ ≤ x.mantissa_.toNat * 10 ^ 17 := Nat.mul_le_mul_right _ hxm_min
    · calc (10 : ℕ) ^ 35 ≤ 10 ^ 18 * 10 ^ 22 := by norm_num
        _ ≤ x.mantissa_.toNat * 10 ^ 22 := Nat.mul_le_mul_right _ hxm_min
  have hM_pos : 1 ≤ zmq.toNat := by
    by_contra h
    push_neg at h
    have h0 : zmq.toNat = 0 := by omega
    rw [h0, Nat.zero_mul, Nat.zero_add] at heuc
    omega
  -- M < 10^23: M·ym ≤ xm·10^N ≤ (10^19−1)·10^22 < 10^41 = 10^23·10^18.
  have hN_le : N ≤ 22 := by rcases hN with rfl | rfl <;> norm_num
  have hM_lt : zmq.toNat < 10 ^ 23 := by
    by_contra h
    push_neg at h
    have h1 : 10 ^ 23 * 10 ^ 18 ≤ zmq.toNat * y.mantissa_.toNat :=
      Nat.mul_le_mul h hym_min
    have h2 : x.mantissa_.toNat * 10 ^ N ≤ (10 ^ 19 - 1) * 10 ^ 22 :=
      Nat.mul_le_mul hxm_le (Nat.pow_le_pow_right (by norm_num) hN_le)
    omega
  -- The tail-vs-mantissa ratio, via the ℕ core r·10^20 ≤ M·ym.
  have hδM : dropped = true → δ * 10 ^ 20 ≤ (zmq.toNat : ℚ) := by
    intro _
    have hcore : r * 10 ^ 20 ≤ zmq.toNat * y.mantissa_.toNat := by
      rcases hN with rfl | rfl
      · -- Stage-1-only: tiny residual r·10^5 < ym against M ≥ 10^16.
        have hM_ge : 10 ^ 16 ≤ zmq.toNat := by
          by_contra hlt
          push_neg at hlt
          have h1 : zmq.toNat * y.mantissa_.toNat ≤ (10 ^ 16 - 1) * (10 ^ 19 - 1) :=
            Nat.mul_le_mul (by omega) hym_le
          omega
        calc r * 10 ^ 20 = r * 10 ^ 5 * 10 ^ 15 := by ring
          _ ≤ y.mantissa_.toNat * 10 ^ 15 :=
              Nat.mul_le_mul_right _ (le_of_lt (htiny rfl))
          _ ≤ y.mantissa_.toNat * zmq.toNat :=
              Nat.mul_le_mul_left _ (le_trans (by norm_num) hM_ge)
          _ = zmq.toNat * y.mantissa_.toNat := Nat.mul_comm _ _
      · -- Corrected stage: M ≥ 10^21 swallows r ≤ ym − 1.
        have hM_ge : 10 ^ 21 ≤ zmq.toNat := by
          by_contra hlt
          push_neg at hlt
          have h1 : zmq.toNat * y.mantissa_.toNat ≤ (10 ^ 21 - 1) * (10 ^ 19 - 1) :=
            Nat.mul_le_mul (by omega) hym_le
          have h2 : 10 ^ 18 * 10 ^ 22 ≤ x.mantissa_.toNat * 10 ^ 22 :=
            Nat.mul_le_mul_right _ hxm_min
          omega
        calc r * 10 ^ 20 ≤ y.mantissa_.toNat * 10 ^ 20 :=
              Nat.mul_le_mul_right _ (le_of_lt hr_lt)
          _ ≤ y.mantissa_.toNat * zmq.toNat :=
              Nat.mul_le_mul_left _ (le_trans (by norm_num) hM_ge)
          _ = zmq.toNat * y.mantissa_.toNat := Nat.mul_comm _ _
    rw [hδ_def, div_mul_eq_mul_div, div_le_iff₀ hym_q_pos]
    exact_mod_cast hcore
  -- The value equation |x/y| = (M + δ)·10^zeq.
  have heuc_q : (x.mantissa_.toNat : ℚ) * 10 ^ (N : ℕ)
      = (zmq.toNat : ℚ) * (y.mantissa_.toNat : ℚ) + (r : ℚ) := by
    exact_mod_cast heuc
  have habs_x : |x.toRat| = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := abs_toRat_eq x
  have habs_y : |y.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := abs_toRat_eq y
  have h10ye_pos : (0 : ℚ) < (10 : ℚ) ^ y.exponent_ := zpow_pos (by norm_num) _
  have hden_pos : (0 : ℚ) < (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ :=
    mul_pos hym_q_pos h10ye_pos
  have htruth : |x.toRat / y.toRat| = ((zmq.toNat : ℚ) + δ) * 10 ^ zeq := by
    rw [abs_div, habs_x, habs_y, hze, hδ_def]
    rw [div_eq_iff (ne_of_gt hden_pos)]
    symm
    have hcancel : ((zmq.toNat : ℚ) + (r : ℚ) / (y.mantissa_.toNat : ℚ)) * (y.mantissa_.toNat : ℚ)
        = (zmq.toNat : ℚ) * (y.mantissa_.toNat : ℚ) + (r : ℚ) := by
      field_simp
    calc ((zmq.toNat : ℚ) + (r : ℚ) / (y.mantissa_.toNat : ℚ))
            * 10 ^ (x.exponent_ - y.exponent_ - (N : ℕ))
            * ((y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_)
        = (((zmq.toNat : ℚ) + (r : ℚ) / (y.mantissa_.toNat : ℚ)) * (y.mantissa_.toNat : ℚ))
            * (10 ^ (x.exponent_ - y.exponent_ - (N : ℕ)) * 10 ^ y.exponent_) := by ring
      _ = ((zmq.toNat : ℚ) * (y.mantissa_.toNat : ℚ) + (r : ℚ))
            * (10 ^ (x.exponent_ - y.exponent_ - (N : ℕ)) * 10 ^ y.exponent_) := by rw [hcancel]
      _ = ((x.mantissa_.toNat : ℚ) * 10 ^ (N : ℕ))
            * (10 ^ (x.exponent_ - y.exponent_ - (N : ℕ)) * 10 ^ y.exponent_) := by rw [heuc_q]
      _ = (x.mantissa_.toNat : ℚ)
            * (10 ^ ((N : ℕ) : ℤ) * (10 ^ (x.exponent_ - y.exponent_ - (N : ℕ))
                * 10 ^ y.exponent_)) := by
          rw [zpow_natCast]
          ring
      _ = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := by
          rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0),
              ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0),
              show (x.exponent_ - y.exponent_ - ((N : ℕ) : ℤ)) + y.exponent_
                  = x.exponent_ - ((N : ℕ) : ℤ) from by ring,
              show ((N : ℕ) : ℤ) + (x.exponent_ - ((N : ℕ) : ℤ)) = x.exponent_ from by ring]
  -- Sign facts for the quotient.
  set zn : Bool := x.negative_ != y.negative_ with hzn_def
  have hsign : (zn = true → x.toRat / y.toRat ≤ 0) ∧ (zn = false → 0 ≤ x.toRat / y.toRat) := by
    constructor
    · intro hzn
      have hdiff : x.negative_ ≠ y.negative_ := by
        intro hsame
        rw [hzn_def] at hzn
        rw [hsame, bne_self_eq_false] at hzn
        exact Bool.false_ne_true hzn
      rw [div_eq_mul_inv]
      rcases hxn : x.negative_ with _ | _
      · have hyn : y.negative_ = true := by
          rcases hyn : y.negative_ with _ | _
          · exact absurd (hxn.trans hyn.symm) hdiff
          · rfl
        have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
        have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
        exact mul_nonpos_of_nonneg_of_nonpos hx_nn (inv_nonpos.mpr hy_np)
      · have hyn : y.negative_ = false := by
          rcases hyn : y.negative_ with _ | _
          · rfl
          · exact absurd (hxn.trans hyn.symm) hdiff
        have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
        have hy_nn : (0 : ℚ) ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
        exact mul_nonpos_of_nonpos_of_nonneg hx_np (inv_nonneg.mpr hy_nn)
    · intro hzn
      have hsame : x.negative_ = y.negative_ := by
        rw [hzn_def] at hzn
        rwa [bne_eq_false_iff_eq] at hzn
      rw [div_eq_mul_inv]
      rcases hxn : x.negative_ with _ | _
      · have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
        have hy_nn : (0 : ℚ) ≤ y.toRat :=
          Number.toRat_nonneg_of_nonnegative y (by rw [← hsame]; exact hxn)
        exact mul_nonneg hx_nn (inv_nonneg.mpr hy_nn)
      · have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
        have hy_np : y.toRat ≤ 0 :=
          Number.toRat_nonpos_of_negative y (by rw [← hsame]; exact hxn)
        exact mul_nonneg_of_nonpos_of_nonpos hx_np (inv_nonpos.mpr hy_np)
  have hδ_lt : δ < 1 := by
    rw [hδ_def, div_lt_one hym_q_pos]
    exact_mod_cast hr_lt
  have hsticky_pos : dropped = true → 0 < δ := by
    intro hd
    have hr_ne : r ≠ 0 := hdrop_iff.mp hd
    have hr_pos : (0 : ℚ) < (r : ℚ) := by
      exact_mod_cast Nat.pos_of_ne_zero hr_ne
    rw [hδ_def]
    exact div_pos hr_pos hym_q_pos
  exact ⟨zmq, zeq, δ, zn, dropped,
    ⟨hδ_nn, hδ_le, hsticky_zero, hM_pos, hM_lt, hδM, htruth, hok, hsign, hδ_lt, hsticky_pos⟩⟩

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.AlgorithmicFacts.DiffSignRepresents
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128Facts


namespace XRPL.Model.Protocol

/-! # Shared pieces for the addition discrete-rounding files

Sign-transfer helpers for the same-sign branch, the exact-cancellation
characterization for the `x = -y` guard, and the underflow/result-shape
bridges for the diff-sign branch (which routes through `doNormalize128`). -/

/-- A positive same-sign sum forces the shared sign bit clear. -/
lemma add_same_sign_xn_false_of_truth_pos (x y : Number)
    (h_same : x.negative_ = y.negative_)
    (h_pos : 0 < x.toRat + y.toRat) : x.negative_ = false := by
  rcases hxn : x.negative_ with _ | _
  · rfl
  · exfalso
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y (h_same ▸ hxn)
    linarith

/-- A negative same-sign sum forces the shared sign bit set. -/
lemma add_same_sign_xn_true_of_truth_neg (x y : Number)
    (h_same : x.negative_ = y.negative_)
    (h_neg : x.toRat + y.toRat < 0) : x.negative_ = true := by
  rcases hxn : x.negative_ with _ | _
  · exfalso
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y (h_same ▸ hxn)
    linarith
  · rfl

/-- Exact cancellation: a structural match against the negation zeroes the
sum. -/
lemma add_truth_zero_of_eq_neg (x y : Number)
    (h : x.operator_eq y.operator_neg = true) :
    x.toRat + y.toRat = 0 := by
  unfold Number.operator_eq at h
  simp only [Bool.and_eq_true, beq_iff_eq] at h
  obtain ⟨⟨h1, h2⟩, h3⟩ := h
  have h_x_toRat : x.toRat = y.operator_neg.toRat := by
    unfold Number.toRat
    rw [h1, h2, h3]
  rw [h_x_toRat, Number.toRat_neg]
  ring

/-- A nonzero sum of nonzero operands (excluding exact cancellation): the
diff-sign keystone facts certify it through the value frame; the same-sign
side is immediate. -/
theorem operator_add_truth_ne (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (mode : rounding_mode) (result : Number)
    (hok : Number.operator_add x y mode = .ok result) :
    x.toRat + y.toRat ≠ 0 := by
  by_cases h_sign_eq : x.negative_ = y.negative_
  · -- Same sign: |x + y| = |x| + |y| > 0.
    intro h0
    have hx_ne : x.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero x hx_mant_ne
    have hy_ne : y.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero y hy_mant_ne
    rcases hxn : x.negative_ with _ | _
    · have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
      have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y (h_sign_eq ▸ hxn)
      have hx_pos : 0 < x.toRat := lt_of_le_of_ne hx_nn (Ne.symm hx_ne)
      linarith
    · have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
      have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y (h_sign_eq ▸ hxn)
      have hx_neg : x.toRat < 0 := lt_of_le_of_ne hx_np hx_ne
      linarith
  · -- Diff sign: the keystone facts expose `|x + y| = (M + δ)·10^ze` with `M ≥ 1`.
    obtain ⟨M, ze', δ, zn, sticky, hδ_low, _, _, hM_pos, _, _, htruth, _, _, _, _⟩ :=
      operator_add_algorithmic_facts_diff_sign_represents x y result mode hx hy
        hx_mant_ne hy_mant_ne h_sign_eq h_not_zero hok
    intro h0
    rw [h0, abs_zero] at htruth
    have hM1 : (1 : ℚ) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_pos
    have h10 : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
    nlinarith

/-- The addition result with nonzero mantissa is normalized. (Same-sign: from
the facts' `doRoundUp`+`normalize` invariants; diff-sign: from the
`doNormalize128` keystone.) -/
theorem operator_add_result_isNormalized (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.isNormalized := by
  obtain ⟨M, ze', δ, zn, sticky, _, _, _, hM_pos, _, _, _, hok128, _, _, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result mode hx hy
      hx_mant_ne hy_mant_ne h_diff_sign h_not_zero hok
  exact doNormalize128_result_isNormalized zn M ze' sticky mode hM_pos result hok128 hresult

/-- A zero diff-sign addition result forces the sum strictly below the
smallest positive representable. -/
theorem operator_add_underflow_truth_small (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y mode = .ok result)
    (hres0 : result.mantissa_ = 0) :
    |x.toRat + y.toRat| < (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, _, hsticky_zero, hM_pos, hM_lt, hM_big,
      htruth, hok128, _, hδ_lt, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result mode hx hy
      hx_mant_ne hy_mant_ne h_diff_sign h_not_zero hok
  rw [htruth]
  exact doNormalize128_underflow_value_small zn M ze' δ sticky mode hδ_low hδ_lt
    hsticky_zero hM_pos (lt_trans hM_lt (by norm_num))
    (fun hst => by
      have h2 : ((10 : ℚ) ^ 20) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_big hst
      linarith [le_of_lt hδ_lt])
    result hok128 hres0

/-- Every normalized value sits strictly below the lattice top. -/
lemma Number.abs_toRat_lt_top (x : Number) (hx : x.isNormalized) :
    |x.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
  have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ (maxExponent : ℤ) := zpow_pos (by norm_num) _
  rcases hx with hz | ⟨_, hmax, _, _, hemax⟩
  · rw [hz, Number.toRat_zero, abs_zero]
    positivity
  · have h_abs := abs_toRat_eq x
    have h_mant_le : (x.mantissa_.toNat : ℚ) ≤ 10 ^ 19 - 1 := by
      have h_le : x.mantissa_.toNat ≤ largeRange.max.toNat := UInt64.le_iff_toNat_le.mp hmax
      have h_max_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      rw [h_max_v] at h_le
      have h_cast : (x.mantissa_.toNat : ℚ) ≤ (9999999999999999999 : ℚ) := by
        exact_mod_cast h_le
      linarith
    have h_pow_le : (10 : ℚ) ^ x.exponent_ ≤ (10 : ℚ) ^ (maxExponent : ℤ) :=
      zpow_le_zpow_right₀ (by norm_num) hemax
    have h_pow_pos_e : (0 : ℚ) < (10 : ℚ) ^ x.exponent_ := zpow_pos (by norm_num) _
    rw [h_abs]
    calc (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
        ≤ (10 ^ 19 - 1 : ℚ) * 10 ^ x.exponent_ :=
          mul_le_mul_of_nonneg_right h_mant_le (le_of_lt h_pow_pos_e)
      _ ≤ (10 ^ 19 - 1 : ℚ) * (10 : ℚ) ^ (maxExponent : ℤ) :=
          mul_le_mul_of_nonneg_left h_pow_le (by norm_num)
      _ < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
          have h_step := mul_lt_mul_of_pos_right
            (show (10 ^ 19 - 1 : ℚ) < 10 ^ 19 by norm_num) h_pow_pos
          linarith

/-- Opposite-sign sums never reach the lattice top: the sum magnitude is
bounded by the larger operand magnitude. -/
lemma add_diff_sign_truth_top (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (h_diff_sign : x.negative_ ≠ y.negative_) :
    |x.toRat + y.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
  have hx_top := Number.abs_toRat_lt_top x hx
  have hy_top := Number.abs_toRat_lt_top y hy
  rcases hxn : x.negative_ with _ | _
  · have hyn : y.negative_ = true := by
      rcases hh : y.negative_ with _ | _
      · exfalso; apply h_diff_sign; rw [hxn, hh]
      · rfl
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
    rw [abs_of_nonneg hx_nn] at hx_top
    rw [abs_of_nonpos hy_np] at hy_top
    apply abs_lt.mpr
    exact ⟨by linarith, by linarith⟩
  · have hyn : y.negative_ = false := by
      rcases hh : y.negative_ with _ | _
      · rfl
      · exfalso; apply h_diff_sign; rw [hxn, hh]
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
    rw [abs_of_nonpos hx_np] at hx_top
    rw [abs_of_nonneg hy_nn] at hy_top
    apply abs_lt.mpr
    exact ⟨by linarith, by linarith⟩

end XRPL.Model.Protocol

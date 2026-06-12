import XRPL.Properties.Protocol.Number.Common.Defs

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## Helper lemmas for `operator_mul_rounded` -/

/-- If `n` is the maximal normalized Number with `n.toRat ≤ q`, then
`n.toRat = n_lo.toRat` where `n_lo = Number.lower q`. -/
lemma Number.toRat_eq_lower_of_max (q : ℚ) (n n_lo : Number)
    (h_lower : Number.lower q = some n_lo)
    (h_norm : n.isNormalized) (h_le : n.toRat ≤ q)
    (h_max : ∀ m : Number, m.isNormalized → m.toRat ≤ q → m.toRat ≤ n.toRat) :
    n.toRat = n_lo.toRat := by
  have h_n_lo_norm : n_lo.isNormalized := Number.lower_isNormalized q n_lo h_lower
  have h_n_lo_le : n_lo.toRat ≤ q := Number.lower_le q n_lo h_lower
  have h1 : n.toRat ≤ n_lo.toRat := Number.lower_tight q n_lo h_lower n h_norm h_le
  have h2 : n_lo.toRat ≤ n.toRat := h_max n_lo h_n_lo_norm h_n_lo_le
  linarith

/-- If `n` is the minimal normalized Number with `q ≤ n.toRat`, then
`n.toRat = n_up.toRat` where `n_up = Number.upper q`. -/
lemma Number.toRat_eq_upper_of_min (q : ℚ) (n n_up : Number)
    (h_upper : Number.upper q = some n_up)
    (h_norm : n.isNormalized) (h_ge : q ≤ n.toRat)
    (h_min : ∀ m : Number, m.isNormalized → q ≤ m.toRat → n.toRat ≤ m.toRat) :
    n.toRat = n_up.toRat := by
  have h_n_up_norm : n_up.isNormalized := Number.upper_isNormalized q n_up h_upper
  have h_n_up_ge : q ≤ n_up.toRat := Number.le_upper q n_up h_upper
  have h1 : n_up.toRat ≤ n.toRat := Number.upper_tight q n_up h_upper n h_norm h_ge
  have h2 : n.toRat ≤ n_up.toRat := h_min n_up h_n_up_norm h_n_up_ge
  linarith

/-! ## Nonzero product -/

/-- The product `x.toRat * y.toRat` is nonzero when both mantissas are nonzero
and the inputs are normalized. -/
lemma toRat_mul_ne_zero_of_normalized (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0) :
    x.toRat * y.toRat ≠ 0 := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have habsx : |x.toRat| > 0 := by
    rw [abs_toRat_eq]
    have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := by
      have := UInt64.le_iff_toNat_le.mp hx_bounds.1
      rw [largeRange_min_val] at this; exact this
    have h_mant_pos : (0 : ℚ) < (x.mantissa_.toNat : ℚ) := by
      have : (0 : ℕ) < x.mantissa_.toNat := by
        have : (10 : ℕ) ^ 18 > 0 := by norm_num
        omega
      exact_mod_cast this
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ x.exponent_ := zpow_pos (by norm_num) _
    positivity
  have habsy : |y.toRat| > 0 := by
    rw [abs_toRat_eq]
    have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := by
      have := UInt64.le_iff_toNat_le.mp hy_bounds.1
      rw [largeRange_min_val] at this; exact this
    have h_mant_pos : (0 : ℚ) < (y.mantissa_.toNat : ℚ) := by
      have : (0 : ℕ) < y.mantissa_.toNat := by
        have : (10 : ℕ) ^ 18 > 0 := by norm_num
        omega
      exact_mod_cast this
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ y.exponent_ := zpow_pos (by norm_num) _
    positivity
  intro h
  rw [mul_eq_zero] at h
  rcases h with hx0 | hy0
  · rw [hx0] at habsx; simp at habsx
  · rw [hy0] at habsy; simp at habsy

/-! ## Structural absolute bounds for normalized Numbers

These lemmas provide the "any normalized Number lives in a finite range"
facts needed to discharge the result-magnitude side hypothesis in the
combined add/sub rounding bounds:

* `Number.abs_toRat_le_of_isNormalized` — for any normalized `n`,
  `|n.toRat| ≤ 10^19 * 10^maxExponent`.
* `Number.abs_toRat_le_largeRange_max_of_isNormalized` — tighter variant
  using the exact mantissa cap `10^19 - 1`. -/

/-- For any normalized Number `n`, `|n.toRat| ≤ 10^19 * 10^maxExponent`. -/
lemma Number.abs_toRat_le_of_isNormalized {n : Number} (h : n.isNormalized) :
    |n.toRat| ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ maxExponent := by
  rcases h with hzero | ⟨_, hmax, _, _, hemax⟩
  · -- Zero: |0| = 0 ≤ ...
    rw [hzero, Number.toRat_zero, abs_zero]
    positivity
  · rw [abs_toRat_eq n]
    -- mantissa ≤ largeRange.max.toNat = 10^19 - 1 < 10^19
    have hm_le : n.mantissa_.toNat ≤ largeRange.max.toNat := UInt64.le_iff_toNat_le.mp hmax
    have hmax_v : largeRange.max.toNat = 9999999999999999999 := by decide
    have h_mq_le : (n.mantissa_.toNat : ℚ) ≤ ((10 : ℕ) ^ 19 : ℚ) := by
      have : (n.mantissa_.toNat : ℕ) ≤ 10 ^ 19 := by rw [hmax_v] at hm_le; omega
      have h_cast : (n.mantissa_.toNat : ℚ) ≤ ((10 ^ 19 : ℕ) : ℚ) := by exact_mod_cast this
      have h_pow_cast : ((10 ^ 19 : ℕ) : ℚ) = ((10 : ℕ) ^ 19 : ℚ) := rfl
      rw [h_pow_cast] at h_cast
      exact h_cast
    have h_mq_le' : (n.mantissa_.toNat : ℚ) ≤ (10 : ℚ) ^ (19 : ℕ) := by
      have : ((10 : ℕ) ^ 19 : ℚ) = (10 : ℚ) ^ (19 : ℕ) := by push_cast; rfl
      rw [this] at h_mq_le; exact h_mq_le
    -- exponent ≤ maxExponent
    have h_pow_mono : (10 : ℚ) ^ n.exponent_ ≤ (10 : ℚ) ^ maxExponent := by
      apply zpow_le_zpow_right₀ (by norm_num : (1 : ℚ) ≤ 10) hemax
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    have h_pow_maxExp_pos : (0 : ℚ) < (10 : ℚ) ^ maxExponent := zpow_pos (by norm_num) _
    have h_pow_19_pos : (0 : ℚ) < (10 : ℚ) ^ (19 : ℕ) := by positivity
    have h_mq_nn : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) := Nat.cast_nonneg _
    calc (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ n.exponent_ :=
          mul_le_mul_of_nonneg_right h_mq_le' (le_of_lt h_pow_pos)
      _ ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ maxExponent :=
          mul_le_mul_of_nonneg_left h_pow_mono (le_of_lt h_pow_19_pos)

/-- Tighter version of `Number.abs_toRat_le_of_isNormalized`: for any normalized
Number `n`, `|n.toRat| ≤ (10^19 - 1) * 10^maxExponent`. This uses the actual
`largeRange.max = 9999999999999999999 = 10^19 - 1` mantissa upper bound. -/
lemma Number.abs_toRat_le_largeRange_max_of_isNormalized {n : Number} (h : n.isNormalized) :
    |n.toRat| ≤ ((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent := by
  rcases h with hzero | ⟨_, hmax, _, _, hemax⟩
  · -- Zero: |0| = 0 ≤ ...
    rw [hzero, Number.toRat_zero, abs_zero]
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ maxExponent := zpow_pos (by norm_num) _
    have h_pow_19_ge : (1 : ℚ) ≤ (10 : ℚ) ^ (19 : ℕ) := by
      have : (1 : ℚ) ≤ 10 := by norm_num
      exact one_le_pow₀ this
    have h_factor_nn : (0 : ℚ) ≤ (10 : ℚ) ^ (19 : ℕ) - 1 := by linarith
    exact mul_nonneg h_factor_nn (le_of_lt h_pow_pos)
  · rw [abs_toRat_eq n]
    -- mantissa ≤ largeRange.max.toNat = 10^19 - 1
    have hm_le : n.mantissa_.toNat ≤ largeRange.max.toNat := UInt64.le_iff_toNat_le.mp hmax
    have hmax_v : largeRange.max.toNat = 9999999999999999999 := by decide
    have h_mq_le : (n.mantissa_.toNat : ℚ) ≤ ((10 : ℚ) ^ (19 : ℕ) - 1) := by
      have h_nat : n.mantissa_.toNat ≤ 9999999999999999999 := by rw [hmax_v] at hm_le; exact hm_le
      have h_cast : (n.mantissa_.toNat : ℚ) ≤ ((9999999999999999999 : ℕ) : ℚ) := by exact_mod_cast h_nat
      have h_eq : ((9999999999999999999 : ℕ) : ℚ) = (10 : ℚ) ^ (19 : ℕ) - 1 := by
        push_cast; norm_num
      rw [h_eq] at h_cast; exact h_cast
    -- exponent ≤ maxExponent
    have h_pow_mono : (10 : ℚ) ^ n.exponent_ ≤ (10 : ℚ) ^ maxExponent := by
      apply zpow_le_zpow_right₀ (by norm_num : (1 : ℚ) ≤ 10) hemax
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    have h_pow_maxExp_pos : (0 : ℚ) < (10 : ℚ) ^ maxExponent := zpow_pos (by norm_num) _
    have h_pow_19_ge : (1 : ℚ) ≤ (10 : ℚ) ^ (19 : ℕ) := by
      have : (1 : ℚ) ≤ 10 := by norm_num
      exact one_le_pow₀ this
    have h_factor_nn : (0 : ℚ) ≤ (10 : ℚ) ^ (19 : ℕ) - 1 := by linarith
    have h_mq_nn : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) := Nat.cast_nonneg _
    calc (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        ≤ ((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ n.exponent_ :=
          mul_le_mul_of_nonneg_right h_mq_le (le_of_lt h_pow_pos)
      _ ≤ ((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent :=
          mul_le_mul_of_nonneg_left h_pow_mono h_factor_nn

/-- Even tighter version of `result_bounded_by_truth_diff_sign_tight`: under the
extra assumption `1 ≤ |x.toRat + y.toRat|`, we get
`|result| ≤ |truth| * (10^19 - 1) * 10^maxExp`, dropping a factor of
`10^maxExp` from the previous tight bound. This corresponds to the case where
the truth is "large" (at least 1), avoiding the `10^maxExp` blowup from the
worst-case denominator scaling. -/
lemma result_bounded_by_truth_diff_sign_truth_ge_one {result : Number}
    (hresult_norm : result.isNormalized)
    (truth : ℚ) (h_truth_ge_one : 1 ≤ |truth|) :
    |result.toRat| ≤ |truth| *
      (((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent) := by
  have h_result_le : |result.toRat| ≤ ((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent :=
    Number.abs_toRat_le_largeRange_max_of_isNormalized hresult_norm
  have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ maxExponent := zpow_pos (by norm_num) _
  have h_pow_19_ge : (1 : ℚ) ≤ (10 : ℚ) ^ (19 : ℕ) := by
    have : (1 : ℚ) ≤ 10 := by norm_num
    exact one_le_pow₀ this
  have h_factor_nn : (0 : ℚ) ≤ (10 : ℚ) ^ (19 : ℕ) - 1 := by linarith
  have h_rhs_nn : (0 : ℚ) ≤ ((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent :=
    mul_nonneg h_factor_nn (le_of_lt h_pow_pos)
  calc |result.toRat|
      ≤ ((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent := h_result_le
    _ = 1 * (((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent) := by ring
    _ ≤ |truth| * (((10 : ℚ) ^ (19 : ℕ) - 1) * (10 : ℚ) ^ maxExponent) :=
        mul_le_mul_of_nonneg_right h_truth_ge_one h_rhs_nn

end XRPL.Model.Protocol

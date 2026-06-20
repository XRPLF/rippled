import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.ScaleDown
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Underflow characterization for `operator_mul`

A zero result mantissa (with nonzero normalized operands) means the mul-stage
`doRoundUp` flushed — the normalize stage cannot introduce a zero on the
invariant range — and a flush pins the value frame to the representable floor.
Hence the exact product is strictly below the smallest positive representable.
The hypothesis-minimal discrete-rounding theorems consume this in their
underflow branches. -/

/-- With the product strictly positive, the result of `operator_mul` carries the
nonnegative sign (the factors agree in sign, so `zn = false`). -/
lemma mul_result_nonneg_of_truth_pos (x y result : Number)
    (h_sign : result.negative_ = (x.negative_ != y.negative_))
    (h_truth_pos : 0 < x.toRat * y.toRat) :
    (0 : ℚ) ≤ result.toRat := by
  have h_xy_same_sign : x.negative_ = y.negative_ := by
    by_contra h_diff
    have h_xy_neg : x.toRat * y.toRat ≤ 0 := by
      rcases hxn : x.negative_ with _ | _
      · rcases hyn : y.negative_ with _ | _
        · exfalso; apply h_diff; rw [hxn, hyn]
        · have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
          have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
          exact mul_nonpos_of_nonneg_of_nonpos hx_nn hy_np
      · rcases hyn : y.negative_ with _ | _
        · have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
          have hy_nn : (0 : ℚ) ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
          exact mul_nonpos_of_nonpos_of_nonneg hx_np hy_nn
        · exfalso; apply h_diff; rw [hxn, hyn]
    linarith
  have h_zn_false : (x.negative_ != y.negative_) = false := by
    rw [h_xy_same_sign]; simp [bne]
  exact Number.toRat_nonneg_of_nonnegative result (by rw [h_sign, h_zn_false])

/-- With the product strictly negative the factors disagree in sign:
`zn = (x.negative_ != y.negative_) = true`. -/
lemma mul_zn_true_of_truth_neg (x y : Number)
    (h_truth_neg : x.toRat * y.toRat < 0) :
    (x.negative_ != y.negative_) = true := by
  have h_xy_diff_sign : x.negative_ ≠ y.negative_ := by
    intro h_same
    have h_xy_nn : (0 : ℚ) ≤ x.toRat * y.toRat := by
      rcases hxn : x.negative_ with _ | _
      · have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
        have hy_nn : (0 : ℚ) ≤ y.toRat :=
          Number.toRat_nonneg_of_nonnegative y (h_same ▸ hxn)
        exact mul_nonneg hx_nn hy_nn
      · have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
        have hy_np : y.toRat ≤ 0 :=
          Number.toRat_nonpos_of_negative y (h_same ▸ hxn)
        exact mul_nonneg_of_nonpos_of_nonpos hx_np hy_np
    linarith
  rcases hxn : x.negative_ with _ | _ <;> rcases hyn : y.negative_ with _ | _
  all_goals first
    | rfl
    | (exfalso; apply h_xy_diff_sign; rw [hxn, hyn])

/-- With the product strictly positive the factors agree in sign:
`zn = (x.negative_ != y.negative_) = false`. -/
lemma mul_zn_false_of_truth_pos (x y : Number)
    (h_truth_pos : 0 < x.toRat * y.toRat) :
    (x.negative_ != y.negative_) = false := by
  have h_xy_same_sign : x.negative_ = y.negative_ := by
    by_contra h_diff
    have h_xy_np : x.toRat * y.toRat ≤ 0 := by
      rcases hxn : x.negative_ with _ | _
      · rcases hyn : y.negative_ with _ | _
        · exfalso; apply h_diff; rw [hxn, hyn]
        · have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
          have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
          exact mul_nonpos_of_nonneg_of_nonpos hx_nn hy_np
      · rcases hyn : y.negative_ with _ | _
        · have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
          have hy_nn : (0 : ℚ) ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
          exact mul_nonpos_of_nonpos_of_nonneg hx_np hy_nn
        · exfalso; apply h_diff; rw [hxn, hyn]
    linarith
  rw [h_xy_same_sign]; simp [bne]

/-- With the product strictly negative, the result of `operator_mul` carries the
negative sign (the factors disagree in sign, so `zn = true`). -/
lemma mul_result_nonpos_of_truth_neg (x y result : Number)
    (h_sign : result.negative_ = (x.negative_ != y.negative_))
    (h_truth_neg : x.toRat * y.toRat < 0) :
    result.toRat ≤ 0 :=
  Number.toRat_nonpos_of_negative result
    (by rw [h_sign, mul_zn_true_of_truth_neg x y h_truth_neg])

set_option maxHeartbeats 1600000 in
-- Product-frame derivation + per-mode invariant dispatch.
theorem operator_mul_underflow_truth_small (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y mode = .ok result)
    (hres0 : result.mantissa_ = 0) :
    |x.toRat * y.toRat| < (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_mant_pos := mantissa_toNat_pos_of_bounds hx_bounds
  have hy_mant_pos := mantissa_toNat_pos_of_bounds hy_bounds
  have hx_ne_zero := Number.not_operator_eq_zero_of_mantissa_ne hx_mant_ne
  have hy_ne_zero := Number.not_operator_eq_zero_of_mantissa_ne hy_mant_ne
  unfold Number.operator_mul at hok
  simp only [hx_ne_zero, hy_ne_zero, Bool.false_eq_true, if_false] at hok
  set zn : Bool := x.negative_ != y.negative_ with hzn_def
  set zm128 : UInt128 := toUInt128 x.mantissa_ * toUInt128 y.mantissa_ with hzm128_def
  set ze : Int := x.exponent_ + y.exponent_ with hze_def
  set g0 : Guard := if zn then Guard.new.set_negative else Guard.new with hg0_def
  set sd_result : UInt64 × Int × Guard := scaleDown128 zm128 ze g0 with hsd_def
  set zm : UInt64 := sd_result.1 with hzm_def
  set ze' : Int := sd_result.2.1 with hze'_def
  set g : Guard := sd_result.2.2 with hg_def
  have hg0_rep : represents g0 0 := by
    change represents (if zn then Guard.new.set_negative else Guard.new) 0
    exact g0_represents_zero zn
  have h_sd_correct := scaleDown128_correct zm128 ze g0 0 hg0_rep
  simp only at h_sd_correct
  rw [← hsd_def] at h_sd_correct
  have h_sd_split : sd_result = (zm, ze', g) := by rw [hzm_def, hze'_def, hg_def]
  rw [h_sd_split] at h_sd_correct
  simp only at h_sd_correct
  obtain ⟨k, hk_eq, hzm_le_maxRep, hzm128_decomp, hg_rep, _hfloor⟩ := h_sd_correct
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hx_bounds).2
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hy_bounds).2
  have h_prod_fit : x.mantissa_.toNat * y.mantissa_.toNat < 2 ^ 128 := by
    exact nat_mul_lt_two_pow_128 hx_mant_bound hy_mant_bound
  have hzm128_toNat : zm128.toNat = x.mantissa_.toNat * y.mantissa_.toNat := by
    rw [hzm128_def]
    exact uint128_of_uint64_mul_toNat _ _ h_prod_fit
  have habs_xy_eq : |x.toRat * y.toRat|
      = (x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ) * 10 ^ ze := by
    rw [abs_mul, abs_toRat_eq, abs_toRat_eq, hze_def]
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    ring
  set f : ℚ := ((zm128.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k with hf_def
  have h10k_pos : (0 : ℚ) < 10 ^ k := by positivity
  have habs_xy_eq2 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    rw [habs_xy_eq, hk_eq]
    have hzm128_q : ((x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ))
        = (zm.toNat : ℚ) * 10 ^ k + ((zm128.toNat % 10 ^ k : ℕ) : ℚ) := by
      have hq : ((zm128.toNat : ℕ) : ℚ)
          = ((zm.toNat * 10 ^ k + zm128.toNat % 10 ^ k : ℕ) : ℚ) := by
        exact_mod_cast hzm128_decomp
      have hcast : (zm128.toNat : ℚ) = (x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ) := by
        have := hzm128_toNat
        exact_mod_cast this
      rw [← hcast]; rw [hq]; push_cast; ring
    rw [hzm128_q, hf_def]
    have h_zpow_add : (10 : ℚ) ^ (ze + (k : ℤ)) = 10 ^ ze * 10 ^ k := by
      rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    rw [h_zpow_add]
    field_simp
  have hf_lt : f < 1 := by
    rw [hf_def]
    rw [div_lt_one h10k_pos]
    have h_mod_lt : zm128.toNat % 10 ^ k < 10 ^ k := Nat.mod_lt _ (by positivity)
    exact_mod_cast h_mod_lt
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := (mantissaBounds_nat_of hx_bounds).1
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := (mantissaBounds_nat_of hy_bounds).1
  have hzm128_ge : (10 : ℕ) ^ 36 ≤ zm128.toNat := by
    rw [hzm128_toNat]
    calc (10 : ℕ) ^ 36 = 10 ^ 18 * 10 ^ 18 := by ring
      _ ≤ x.mantissa_.toNat * y.mantissa_.toNat := Nat.mul_le_mul hx_min hy_min
  have hmaxRep_lt : maxRepUp.toNat < zm128.toNat := by
    have h_mr_val : maxRepUp.toNat = maxRepUpNat := rfl
    rw [h_mr_val]
    calc (9223372036854775810 : ℕ) < 10 ^ 36 := by norm_num
      _ ≤ zm128.toNat := hzm128_ge
  have h_zm_lb := scaleDown128_lower_bound zm128 ze g0 hmaxRep_lt
  rw [← hsd_def] at h_zm_lb
  rw [h_sd_split] at h_zm_lb
  simp only at h_zm_lb
  have hzm_ge : zm.toNat ≥ mantissaFloor := by
    have : (maxRepUp.toNat + 1) / 10 = mantissaFloorSucc := by
      rw [show maxRepUp.toNat = maxRepUpNat from rfl]
    omega
  -- Extract the doRoundUp result.
  have h_rup_exists : ∃ res : RoundResult,
      g.doRoundUp zn zm ze' largeRange.min largeRange.max mode "Number::multiplication overflow" = .ok res := by
    match hg : g.doRoundUp zn zm ze' largeRange.min largeRange.max mode "Number::multiplication overflow" with
    | .error e => simp only [hg, reduceCtorEq] at hok
    | .ok r => exact ⟨r, rfl⟩
  obtain ⟨res, h_rup⟩ := h_rup_exists
  simp only [h_rup] at hok
  -- The truth sits strictly below the next grid step.
  have h_truth_lt : |x.toRat * y.toRat| < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
    rw [habs_xy_eq2]
    exact mul_lt_mul_of_pos_right (by linarith) (zpow_pos (by norm_num) _)
  by_cases hres_mant : res.mantissa_ = 0
  · -- The mul-stage doRoundUp flushed: the frame is at the representable floor.
    have h_small := doRoundUp_flush_value_small g zn zm ze' mode hzm_ge hzm_le_maxRep
      "Number::multiplication overflow" res h_rup hres_mant
    linarith
  · -- No flush: normalize is the identity on the invariant range — contradiction.
    exfalso
    have h_inv : largeRange.min.toNat ≤ res.mantissa_.toNat ∧
        res.mantissa_.toNat ≤ largeRange.max.toNat ∧
        minExponent ≤ res.exponent_ ∧
        (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
      cases mode with
      | to_nearest =>
        exact doRoundUp_output_invariants_to_nearest_upTo_maxRepUp g zn zm ze' hzm_ge hzm_le_maxRep
          "Number::multiplication overflow" res h_rup hres_mant
      | towards_zero =>
        exact doRoundUp_output_invariants_towards_zero_upTo_maxRepUp g zn zm ze' hzm_ge
          hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant
      | downward =>
        exact doRoundUp_output_invariants_downward_upTo_maxRepUp g zn zm ze' hzm_ge
          hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant
      | upward =>
        exact doRoundUp_output_invariants_upward_upTo_maxRepUp g zn zm ze' hzm_ge
          hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant
    obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
    have h_result_eq_res : result = res.toNumber :=
      Number.normalize_eq_of_invariants h_res_min h_res_max h_res_exp h_res_mod hok
    rw [h_result_eq_res] at hres0
    exact hres_mant hres0

end XRPL.Model.Protocol

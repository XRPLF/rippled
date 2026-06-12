import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.Add.TowardsZero.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Add.TowardsZero.DiffSignTight
import XRPL.Properties.Protocol.Number.Common.Helpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Relative-error bound for `Number.operator_add` under `.towards_zero` rounding,
restricted to the **same-sign** branch. The bound `10/(2^63 + 2)` is the tight
supremum (see `operator_add_rounding_bound_towards_zero_same_sign_attained`).

For same-sign operands `.towards_zero` is equivalent to `.downward` for positives
and `.upward` for negatives — it always truncates toward zero. -/
theorem operator_add_rounding_bound_same_sign_towards_zero (x y : Number) (result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat| ≤ |x.toRat + y.toRat| ∧
    |x.toRat + y.toRat| - |result.toRat| < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, _h_sign⟩ :=
    operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok hresult
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hzm_q_le : (zm.toNat : ℚ) ≤ maxRepNat := by
    have : (zm.toNat : ℚ) ≤ ((maxRep.toNat : ℕ) : ℚ) := by exact_mod_cast hzm_le_maxRep
    have hmrq : ((maxRep.toNat : ℕ) : ℚ) = maxRepNat := by
      rw [maxRep_val]; norm_num
    rw [hmrq] at this; exact this
  have h_abs_truth_nn : 0 ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    apply mul_nonneg _ h10ze'_nn
    have : (0 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast Nat.zero_le _
    linarith
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 2 : ℚ)) := by rw [h_denom_val]; norm_num
  have h_tr_val := doRoundUp_value_towards_zero_truncate g false zm ze' "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
  simp only at h_tr_val
  have h_result_abs_eq : |result.toRat| = (zm.toNat : ℚ) * 10 ^ ze' := by
    rw [h_result_abs]; exact h_tr_val
  have h_direction : |result.toRat| ≤ |x.toRat + y.toRat| := by
    rw [habs_xy_eq, h_result_abs_eq]
    nlinarith [h10ze'_nn, hf_nn]
  have h_magnitude : |x.toRat + y.toRat| - |result.toRat|
      < |x.toRat + y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
    rw [habs_xy_eq, h_result_abs_eq]
    have h_lhs_eq : ((zm.toNat : ℚ) + f) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
        = f * 10 ^ ze' := by ring
    rw [h_lhs_eq]
    have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
      rw [show (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ)))
            = 10 * ((zm.toNat : ℚ) + f) / (2 ^ 63 + 2 : ℚ) by ring]
      rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < 2 ^ 63 + 2)]
      have h_key : f * (mantissaFloor : ℚ) < (zm.toNat : ℚ) := by
        nlinarith [hf_lt1, hzm_q_ge]
      nlinarith [h_key]
    calc f * 10 ^ ze'
        < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right h_inner h10ze'_pos
      _ = (((zm.toNat : ℚ) + f)) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
  exact ⟨h_direction, h_magnitude⟩

/-! ## Relative-error form (same-sign, towards_zero)

The conjunction-form bound above is the natural shape for `.towards_zero`
rounding; the relative-error form `|result - truth| ≤ |truth| * ε` follows
by sign alignment (same-sign operands give a result whose sign matches the
truth's sign, so `|result - truth| = ||result| - |truth|| = |truth| - |result|`). -/

/-- Relative-error form of the same-sign towards_zero bound. -/
theorem operator_add_rounding_bound_same_sign_towards_zero_rel (x y : Number) (result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨_zm, _ze', _f, _g, _res_pos, _hzm_ge, _hzm_le_maxRep, _hf_nn, _hf_lt1,
          _habs_xy_eq, _h_rup_pos, _h_result_abs, _hres_pos_mant_ne, _hf_rep, h_sign⟩ :=
    operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok hresult
  obtain ⟨h_dir, h_mag⟩ := operator_add_rounding_bound_same_sign_towards_zero x y result hx hy
    hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok hresult
  -- result and x+y have the same sign, so |result - (x+y)| = ||result| - |x+y||.
  have h_abs_diff_eq : |result.toRat - (x.toRat + y.toRat)|
      = |(|result.toRat| - |x.toRat + y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat + y.toRat)
    · intro h_neg
      have hx_neg : x.negative_ = true := h_sign ▸ h_neg
      have hy_neg : y.negative_ = true := h_same_sign ▸ hx_neg
      have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hx_neg
      have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy_neg
      linarith
    · intro h_pos
      have hx_nn : x.negative_ = false := h_sign ▸ h_pos
      have hy_nn : y.negative_ = false := h_same_sign ▸ hx_nn
      have hx_nn' : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hx_nn
      have hy_nn' : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy_nn
      linarith
  rw [h_abs_diff_eq]
  -- |truth| ≥ |result|, so ||result| - |truth|| = |truth| - |result|
  have h_diff_nn : 0 ≤ |x.toRat + y.toRat| - |result.toRat| := by linarith
  rw [show |result.toRat| - |x.toRat + y.toRat| = -(|x.toRat + y.toRat| - |result.toRat|) from by ring,
      abs_neg, abs_of_nonneg h_diff_nn]
  exact h_mag

/-- Combined **tight** relative-error bound for `Number.operator_add` under
`.towards_zero` rounding, covering both same-sign and diff-sign branches with the
uniform ε-scale `11/(2^63 - 18)`. Mirrors `operator_add_rounding_bound_downward_tight`
for `.downward`. -/
theorem operator_add_rounding_bound_towards_zero_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  by_cases h_sign : x.negative_ = y.negative_
  · have hss := operator_add_rounding_bound_same_sign_towards_zero_rel x y result hx hy hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok hresult
    have htruth_nn : 0 ≤ |x.toRat + y.toRat| := abs_nonneg _
    have hconst : (10 / (2 ^ 63 + 2 : ℚ)) ≤ (11 / (2 ^ 63 - 18 : ℚ)) := by norm_num
    calc |result.toRat - (x.toRat + y.toRat)|
        < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := hss
      _ ≤ |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
          mul_le_mul_of_nonneg_left hconst htruth_nn
  · exact operator_add_rounding_bound_diff_sign_towards_zero_tight x y result hx hy hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok hresult

end XRPL.Model.Protocol

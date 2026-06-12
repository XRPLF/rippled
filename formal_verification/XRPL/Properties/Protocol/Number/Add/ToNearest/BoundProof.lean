import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Add.ToNearest.DiffSignTight
import XRPL.Properties.Protocol.Number.Common.Helpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Relative-error bound for `Number.operator_add` under `.to_nearest` rounding,
restricted to the **same-sign** branch. The bound `5/(2^63 + 7)` is the tight
supremum (see `operator_add_rounding_bound_attained`). -/
theorem operator_add_rounding_bound_same_sign (x y : Number) (result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| ≤ |x.toRat + y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, _hf_nn, _hf_lt, h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    operator_add_algorithmic_facts_same_sign x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok hresult
  -- For same-sign add, x.toRat + y.toRat has the same sign as both x.toRat and y.toRat.
  -- And result.negative_ = x.negative_ from `h_sign`. So `result.toRat` aligns with `x+y`.
  have h_abs_diff_eq : |result.toRat - (x.toRat + y.toRat)|
      = |(|result.toRat| - |x.toRat + y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat + y.toRat)
    · -- result.negative_ = true → x.toRat + y.toRat ≤ 0
      intro h_neg
      have hx_neg : x.negative_ = true := h_sign ▸ h_neg
      have hy_neg : y.negative_ = true := h_same_sign ▸ hx_neg
      have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hx_neg
      have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy_neg
      linarith
    · -- result.negative_ = false → 0 ≤ x.toRat + y.toRat
      intro h_pos
      have hx_nn : x.negative_ = false := h_sign ▸ h_pos
      have hy_nn : y.negative_ = false := h_same_sign ▸ hx_nn
      have hx_nn' : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hx_nn
      have hy_nn' : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy_nn
      linarith
  rw [h_abs_diff_eq, h_result_abs, habs_xy_eq]
  have h_bound_eq : ((2 ^ 63 + 7 : ℕ) : ℚ) = (2 ^ 63 + 7 : ℚ) := by norm_num
  rw [← h_bound_eq]
  exact doRoundUp_rounds_to_nearest_supTight g zm ze' f hf_rep hzm_ge hzm_le_maxRep
    h_floor_constraint "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
/-- Unified tight `to_nearest` addition bound. For ALL normalized operands —
same-sign or different-sign — the relative rounding error of `operator_add` is at
most `6/(2^63 - 3) ≈ 6.5·10⁻¹⁹`, with **no** input-magnitude hypothesis. The
same-sign branch achieves `5/(2^63 + 7)` and the different-sign branch
`6/(2^63 - 3)`; the uniform constant is the larger of the two. -/
theorem operator_add_rounding_bound_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| ≤ |x.toRat + y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) := by
  by_cases h_sign : x.negative_ = y.negative_
  · have hss := operator_add_rounding_bound_same_sign x y result hx hy hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok hresult
    have htruth_nn : 0 ≤ |x.toRat + y.toRat| := abs_nonneg _
    have hconst : (5 / (2 ^ 63 + 7 : ℚ)) ≤ (6 / (2 ^ 63 - 3 : ℚ)) := by norm_num
    calc |result.toRat - (x.toRat + y.toRat)|
        ≤ |x.toRat + y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := hss
      _ ≤ |x.toRat + y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) :=
          mul_le_mul_of_nonneg_left hconst htruth_nn
  · exact operator_add_rounding_bound_diff_sign_tight x y result hx hy hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok hresult

end XRPL.Model.Protocol

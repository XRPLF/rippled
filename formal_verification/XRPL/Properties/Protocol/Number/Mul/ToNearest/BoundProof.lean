import XRPL.Properties.Protocol.Number.Mul.ToNearest.AlgorithmicFacts

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Relative-error bound for `Number.operator_mul` under `.to_nearest` rounding.
The bound `5/(2^63 + 7)` is the tight supremum (see `operator_mul_rounding_bound_attained`). -/
theorem operator_mul_rounding_bound (x y : Number) (result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - x.toRat * y.toRat| ≤ |x.toRat * y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, _hf_nn, _hf_lt, h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    operator_mul_algorithmic_facts x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_sign_xy := toRat_mul_sign x y
  have h_abs_diff_eq : |result.toRat - x.toRat * y.toRat|
      = |(|result.toRat| - |x.toRat * y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat * y.toRat)
    · intro h_neg
      apply h_sign_xy.2
      intro h_eq
      have h_zn_false : (x.negative_ != y.negative_) = false := by simp [h_eq]
      rw [h_zn_false] at h_sign
      exact Bool.noConfusion (h_neg.symm.trans h_sign)
    · intro h_pos
      apply h_sign_xy.1
      by_contra h_ne
      have h_zn_true : (x.negative_ != y.negative_) = true := by
        simp only [bne_iff_ne, ne_eq]; exact h_ne
      rw [h_zn_true] at h_sign
      exact Bool.noConfusion (h_pos.symm.trans h_sign)
  rw [h_abs_diff_eq, h_result_abs, habs_xy_eq]
  have h_bound_eq : ((2 ^ 63 + 7 : ℕ) : ℚ) = (2 ^ 63 + 7 : ℚ) := by norm_num
  rw [← h_bound_eq]
  exact doRoundUp_rounds_to_nearest_supTight g zm ze' f hf_rep hzm_ge hzm_le_maxRep
    h_floor_constraint "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne

end XRPL.Model.Protocol

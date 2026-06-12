import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Div.Common

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

set_option maxHeartbeats 1600000 in
-- large existential destructuring + case-split over doRoundUp branches
/-- Relative-error bound for `Number.operator_div` under `.downward` rounding.
The bound `10/(2^63 + 2)` matches the multiplication directed bound.

The directional property (`result ≤ truth`) holds for `.downward` division:
truncation always undershoots for positive truth (`zm ≤ zm + f + δ`), and
rounding away from zero always overshoots in magnitude for negative truth
(`zm + 1 ≥ zm + f + δ` since `f + δ < 1`). The invisible division remainder
`δ` does NOT break this — it goes in the safe direction for `.downward`.

(The mode where `δ` DOES break the directional property is `.upward`:
when `f = 0` and `δ > 0`, the algorithm doesn't round up because the guard
looks zero, giving `result < truth` by ~10⁻²¹ relative.) -/
theorem operator_div_rounding_bound_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - x.toRat / y.toRat| < |x.toRat / y.toRat| * (10 / ((2 : ℚ) ^ 63 + 2)) := by
  obtain ⟨zm, ze', f, δ, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, hδ_nn, hfδ_lt,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_floor_constraint,
          _hδ_lt_tenth, _h_g_sbit⟩ :=
    operator_div_algorithmic_facts_downward x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have hy_toRat_ne : y.toRat ≠ 0 := by
    intro h; rw [Number.toRat_eq_zero_iff] at h; exact hy_mant_ne h
  have h_abs_diff_eq : |result.toRat - x.toRat / y.toRat|
      = |(|result.toRat| - |x.toRat / y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat / y.toRat)
    · intro h_neg
      have h_div_sign := toRat_div_sign x y hy_toRat_ne
      apply h_div_sign.2; intro h_eq
      have h_zn_false : (x.negative_ != y.negative_) = false := by simp [h_eq]
      rw [h_zn_false] at h_sign
      exact Bool.noConfusion (h_neg.symm.trans h_sign)
    · intro h_pos
      have h_div_sign := toRat_div_sign x y hy_toRat_ne
      apply h_div_sign.1; by_contra h_ne
      have h_zn_true : (x.negative_ != y.negative_) = true := by
        simp only [bne_iff_ne, ne_eq]; exact h_ne
      rw [h_zn_true] at h_sign
      exact Bool.noConfusion (h_pos.symm.trans h_sign)
  rw [h_abs_diff_eq, h_result_abs, habs_xy_eq]
  exact div_directed_magnitude_bound_strict zm ze' f δ _ _ hzm_ge hf_nn hf_lt1 hδ_nn hfδ_lt rfl
    (doRoundUp_value_trichotomy_downward_strict g zm ze' f δ hf_rep hf_nn hδ_nn h_floor_constraint "Number::operator_div overflow" res_pos h_rup_pos hres_pos_mant_ne hzm_le_maxRep)

end XRPL.Model.Protocol

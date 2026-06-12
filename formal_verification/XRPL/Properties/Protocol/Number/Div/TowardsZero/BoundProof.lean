import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Div.Common

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Relative-error bound for `Number.operator_div` under `.towards_zero` rounding. -/
theorem operator_div_rounding_bound_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat| ≤ |x.toRat / y.toRat| ∧
    |x.toRat / y.toRat| - |result.toRat| < |x.toRat / y.toRat| * (10 / ((2 : ℚ) ^ 63 + 2)) := by
  obtain ⟨zm, ze', f, δ, g, res_pos, hzm_ge, _hzm_le, hf_nn, hf_lt1, hδ_nn, hfδ_lt,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, _h_sign, _h_floor,
          _hδ_tenth, _h_sbit⟩ :=
    operator_div_algorithmic_facts_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_tr_val := doRoundUp_value_towards_zero_truncate g false zm ze' "Number::operator_div overflow" res_pos h_rup_pos hres_pos_mant_ne
  simp only at h_tr_val
  have h_result_abs_eq : |result.toRat| = (zm.toNat : ℚ) * 10 ^ ze' := by
    rw [h_result_abs]; exact h_tr_val
  have hfd_nn : (0 : ℚ) ≤ f + δ := by linarith
  have h_direction : |result.toRat| ≤ |x.toRat / y.toRat| := by
    rw [habs_xy_eq, h_result_abs_eq]
    nlinarith [h10ze'_nn, hfd_nn]
  have h_magnitude : |x.toRat / y.toRat| - |result.toRat|
      < |x.toRat / y.toRat| * (10 / ((2 : ℚ) ^ 63 + 2)) := by
    rw [habs_xy_eq, h_result_abs_eq]
    have h_lhs_eq : ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
        = (f + δ) * 10 ^ ze' := by ring
    rw [h_lhs_eq]
    -- Reduce to: (f+δ)(2^63+2) < 10(zm+f+δ).
    -- f+δ < 1 (strict) gives (f+δ)(2^63-8) < 2^63-8 = 10·floor ≤ 10·zm.
    have h_inner : (f + δ) < (((zm.toNat : ℚ) + f + δ)) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
      rw [show ((2 : ℚ) ^ 63 + 2) = (maxRepCuspTarget : ℚ) from by norm_num]
      rw [show (((zm.toNat : ℚ) + f + δ)) * (10 / (maxRepCuspTarget : ℚ))
            = 10 * ((zm.toNat : ℚ) + f + δ) / maxRepCuspTarget by ring]
      rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
      have h10floor : (10 : ℚ) * mantissaFloor = (2 : ℚ) ^ 63 - 8 := by norm_num
      nlinarith [hzm_q_ge, hf_nn, hδ_nn, hfδ_lt]
    have h_strict : (f + δ) * 10 ^ ze'
        < (((zm.toNat : ℚ) + f + δ)) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by
      calc (f + δ) * 10 ^ ze'
          < (((zm.toNat : ℚ) + f + δ)) * (10 / ((2 : ℚ) ^ 63 + 2)) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_inner h10ze'_pos
        _ = (((zm.toNat : ℚ) + f + δ)) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring
    linarith
  exact ⟨h_direction, h_magnitude⟩

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Mul.TowardsZero.AlgorithmicFacts

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Relative-error bound for `Number.operator_mul` under `.towards_zero` rounding.
The bound `10/(2^63 + 2)` matches `.downward` (twice the `.to_nearest` bound). -/
theorem operator_mul_rounding_bound_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat| ≤ |x.toRat * y.toRat| ∧
    |x.toRat * y.toRat| - |result.toRat| < |x.toRat * y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, _h_sign⟩ :=
    operator_mul_algorithmic_facts_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
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
  have h_tr_val := doRoundUp_value_towards_zero_truncate g false zm ze' "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
  simp only at h_tr_val
  have h_result_abs_eq : |result.toRat| = (zm.toNat : ℚ) * 10 ^ ze' := by
    rw [h_result_abs]; exact h_tr_val
  have h_direction : |result.toRat| ≤ |x.toRat * y.toRat| := by
    rw [habs_xy_eq, h_result_abs_eq]
    nlinarith [h10ze'_nn, hf_nn]
  have h_magnitude : |x.toRat * y.toRat| - |result.toRat|
      < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
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

end XRPL.Model.Protocol

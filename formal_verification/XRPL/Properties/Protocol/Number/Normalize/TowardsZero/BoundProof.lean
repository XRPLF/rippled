import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.TowardsZero.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Common.Approx

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## towards_zero -/

/-- Relative-error bound for `Number.normalize` under `.towards_zero` rounding.

`normalize` accepts an arbitrary `(mantissa, exponent)` pair (the input need not
be normalized). Its final stage is `doRoundUp`, which under `.towards_zero`
always truncates toward zero. The error matches the same-sign `.towards_zero`
truncation supremum `10/(2^63 + 2)`. -/
theorem normalize_rounding_bound_towards_zero (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat| ≤ |n.toRat| ∧
    |n.toRat| - |result.toRat| < |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze, f, g, res_pos, hzm_ge, hzm_le_max, hf_nn, hf_lt1,
          habs_n_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, _h_sign⟩ :=
    normalize_algorithmic_facts_towards_zero n result hn_mant_ne hok hresult
  have h10ze_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  -- towards_zero truncates: |result| = zm·10^ze
  have h_tr_val := doRoundUp_value_towards_zero_truncate g false zm ze
    "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne
  have h_result_abs_eq : |result.toRat| = (zm.toNat : ℚ) * 10 ^ ze := by
    rw [h_result_abs]; exact h_tr_val
  -- |result| ≤ |n|
  have h_direction : |result.toRat| ≤ |n.toRat| := by
    rw [habs_n_eq, h_result_abs_eq]
    nlinarith [h10ze_nn, hf_nn]
  -- magnitude shortfall < |n| · ε
  have h_magnitude : |n.toRat| - |result.toRat| < |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
    rw [habs_n_eq, h_result_abs_eq]
    have h_lhs_eq : ((zm.toNat : ℚ) + f) * 10 ^ ze - (zm.toNat : ℚ) * 10 ^ ze
        = f * 10 ^ ze := by ring
    rw [h_lhs_eq]
    have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
      rw [show (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ)))
            = 10 * ((zm.toNat : ℚ) + f) / (2 ^ 63 + 2 : ℚ) by ring]
      rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < 2 ^ 63 + 2)]
      have h_key : f * (mantissaFloor : ℚ) < (zm.toNat : ℚ) := by
        nlinarith [hf_lt1, hzm_q_ge]
      nlinarith [h_key]
    calc f * 10 ^ ze
        < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze :=
          mul_lt_mul_of_pos_right h_inner h10ze_pos
      _ = (((zm.toNat : ℚ) + f)) * 10 ^ ze * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
  exact ⟨h_direction, h_magnitude⟩

end XRPL.Model.Protocol

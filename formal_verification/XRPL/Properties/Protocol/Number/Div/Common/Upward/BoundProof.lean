import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Div.Common.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128
import XRPL.Properties.Protocol.Number.Common.Helpers


namespace XRPL.Model.Protocol

/-- Rounding bound for `operator_div` under `.upward`: the algorithmic facts
package the staged-quotient state as `doNormalize128` keystone inputs, and
`doNormalize128_rounds_any` delivers the any-round bound `11/(2^63 − 8)` on
`|result.toRat|`; the strict constant `11/(2^63 − 18)` follows from the
positive truth. -/
theorem operator_div_rounding_bound_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - x.toRat / y.toRat| < |x.toRat / y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, hsign, _, _⟩ :=
    operator_div_algorithmic_facts_represents x y result .upward hx hy
      hx_mant_ne hy_mant_ne hok
  obtain ⟨hbound, hneg⟩ :=
    doNormalize128_rounds_any zn M ze' δ sticky .upward hδ_low hδ_le hsticky_zero
      hM_pos hM_lt hδM result hok128 hresult
  have h_abs_diff_eq : |result.toRat - x.toRat / y.toRat|
      = |(|result.toRat| - |x.toRat / y.toRat|)| :=
    abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat / y.toRat)
      (fun h_neg => hsign.1 (hneg ▸ h_neg))
      (fun h_pos => hsign.2 (hneg ▸ h_pos))
  have hM1 : (1 : ℚ) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_pos
  have h_truth_pos : 0 < ((M.toNat : ℚ) + δ) * 10 ^ ze' := by
    apply mul_pos _ (zpow_pos (by norm_num) _)
    linarith
  calc |result.toRat - x.toRat / y.toRat|
      = |(|result.toRat| - |x.toRat / y.toRat|)| := h_abs_diff_eq
    _ ≤ |x.toRat / y.toRat| * (11 / (2 ^ 63 - 8 : ℚ)) := by rw [htruth]; exact hbound
    _ < |x.toRat / y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
        rw [htruth]
        exact mul_lt_mul_of_pos_left (by norm_num) h_truth_pos

/-- Direction for `.upward`: the truth never exceeds the result. The staged
sticky flag is exact (`dropped ↔ r ≠ 0`), so the `doNormalize128` direction
keystone applies — the pre-330 pipeline broke this very property when the
residual was invisible to the guard (`f = 0`, `δ > 0`). -/
theorem operator_div_rounding_bound_upward_dir (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    x.toRat / y.toRat ≤ result.toRat := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, hsign, _, _⟩ :=
    operator_div_algorithmic_facts_represents x y result .upward hx hy
      hx_mant_ne hy_mant_ne hok
  obtain ⟨_, hdir, _⟩ :=
    doNormalize128_rounds_direction zn M ze' δ sticky .upward hδ_low hδ_le hsticky_zero
      hM_pos hM_lt hδM result hok128 hresult
  have h := hdir rfl
  cases hzn : zn
  · rw [hzn, if_neg Bool.false_ne_true] at h
    have h_nn : 0 ≤ x.toRat / y.toRat := hsign.2 hzn
    rw [abs_of_nonneg h_nn] at htruth
    rw [← htruth] at h
    exact h
  · rw [hzn, if_pos rfl] at h
    have h_np : x.toRat / y.toRat ≤ 0 := hsign.1 hzn
    rw [abs_of_nonpos h_np] at htruth
    linarith

end XRPL.Model.Protocol

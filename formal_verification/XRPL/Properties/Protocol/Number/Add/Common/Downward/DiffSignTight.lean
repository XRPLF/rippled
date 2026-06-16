import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.AlgorithmicFacts.DiffSignRepresents
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128
import XRPL.Properties.Protocol.Number.Common.Helpers


namespace XRPL.Model.Protocol

theorem operator_add_rounding_bound_diff_sign_downward_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
      htruth, hok128, hsign, _, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .downward hx hy
      hx_mant_ne hy_mant_ne h_diff_sign h_not_zero hok
  obtain ⟨hbound, hneg⟩ :=
    doNormalize128_rounds_any zn M ze' δ sticky .downward hδ_low hδ_le hsticky_zero
      hM_pos (lt_trans hM_lt (by norm_num))
      (fun hst => by
        have h2 : ((10 : ℚ) ^ 20) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_big hst
        linarith [hδ_le])
      result hok128 hresult
  have h_abs_diff_eq : |result.toRat - (x.toRat + y.toRat)|
      = |(|result.toRat| - |x.toRat + y.toRat|)| :=
    abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat + y.toRat)
      (fun h_neg => hsign.1 (hneg ▸ h_neg))
      (fun h_pos => hsign.2 (hneg ▸ h_pos))
  have hM1 : (1 : ℚ) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_pos
  have h_truth_pos : 0 < ((M.toNat : ℚ) + δ) * 10 ^ ze' := by
    apply mul_pos _ (zpow_pos (by norm_num) _)
    linarith
  calc |result.toRat - (x.toRat + y.toRat)|
      = |(|result.toRat| - |x.toRat + y.toRat|)| := h_abs_diff_eq
    _ ≤ |x.toRat + y.toRat| * (11 / (2 ^ 63 - 8 : ℚ)) := by rw [htruth]; exact hbound
    _ < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
        rw [htruth]
        exact mul_lt_mul_of_pos_left (by norm_num) h_truth_pos

theorem operator_add_rounding_bound_diff_sign_downward_dir (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.toRat ≤ x.toRat + y.toRat := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
      htruth, hok128, hsign, _, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .downward hx hy
      hx_mant_ne hy_mant_ne h_diff_sign h_not_zero hok
  obtain ⟨hdir, _, _⟩ :=
    doNormalize128_rounds_direction zn M ze' δ sticky .downward hδ_low hδ_le hsticky_zero
      hM_pos (lt_trans hM_lt (by norm_num))
      (fun hst => by
        have h2 : ((10 : ℚ) ^ 20) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_big hst
        linarith [hδ_le])
      result hok128 hresult
  have h := hdir rfl
  cases hzn : zn
  · rw [hzn, if_neg Bool.false_ne_true] at h
    have h_nn : 0 ≤ x.toRat + y.toRat := hsign.2 hzn
    rw [abs_of_nonneg h_nn] at htruth
    rw [← htruth] at h
    exact h
  · rw [hzn, if_pos rfl] at h
    have h_np : x.toRat + y.toRat ≤ 0 := hsign.1 hzn
    rw [abs_of_nonpos h_np] at htruth
    linarith

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.AlgorithmicFacts.DiffSignRepresents
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128
import XRPL.Properties.Protocol.Number.Common.Helpers


namespace XRPL.Model.Protocol

theorem operator_add_rounding_bound_diff_sign_towards_zero_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
      htruth, hok128, hsign, _, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .towards_zero hx hy
      hx_mant_ne hy_mant_ne h_diff_sign h_not_zero hok
  obtain ⟨hbound, hneg⟩ :=
    doNormalize128_rounds_any zn M ze' δ sticky .towards_zero hδ_low hδ_le hsticky_zero
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

theorem operator_add_rounding_bound_diff_sign_towards_zero_dir (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat| ≤ |x.toRat + y.toRat| := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
      htruth, hok128, _, _, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .towards_zero hx hy
      hx_mant_ne hy_mant_ne h_diff_sign h_not_zero hok
  obtain ⟨_, _, hdir⟩ :=
    doNormalize128_rounds_direction zn M ze' δ sticky .towards_zero hδ_low hδ_le hsticky_zero
      hM_pos (lt_trans hM_lt (by norm_num))
      (fun hst => by
        have h2 : ((10 : ℚ) ^ 20) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_big hst
        linarith [hδ_le])
      result hok128 hresult
  have h := hdir rfl
  rw [← htruth] at h
  exact h

end XRPL.Model.Protocol

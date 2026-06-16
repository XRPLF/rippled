import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.AlgorithmicFacts.DiffSignRepresents
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128
import XRPL.Properties.Protocol.Number.Common.Helpers


namespace XRPL.Model.Protocol

theorem operator_add_rounding_bound_diff_sign_to_nearest_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| ≤ |x.toRat + y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
      htruth, hok128, hsign, _, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .to_nearest hx hy
      hx_mant_ne hy_mant_ne h_diff_sign h_not_zero hok
  obtain ⟨hbound, hneg⟩ :=
    doNormalize128_rounds_to_nearest zn M ze' δ sticky hδ_low hδ_le hsticky_zero
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
  rw [h_abs_diff_eq, htruth]
  exact hbound

end XRPL.Model.Protocol

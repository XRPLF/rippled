import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.SameSignDecompose

namespace XRPL.Model.Protocol

theorem operator_add_algorithmic_facts_same_sign_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRepUp.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |x.toRat + y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest "Number::addition overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = x.negative_ ∧
      result.mantissa_ ≠ 0 ∧
      result.isNormalized ∧
      |x.toRat + y.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
  obtain ⟨zm, ze', f, g, res_pos, h_floor_le, h_le_maxRepUp, hf_nn, hf_lt, h_floor_con,
      h_value, h_rup, h_abs_result, h_respos_ne, h_rep, h_neg, _h_sbit, _h_floor_succ,
      h_isNorm, h_truth_top, h_result_ne⟩ :=
    operator_add_algorithmic_facts_same_sign_anyMode x y result .to_nearest
      hx hy hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok
  exact ⟨zm, ze', f, g, res_pos, h_floor_le, h_le_maxRepUp, hf_nn, hf_lt, h_floor_con,
    h_value, h_rup, h_abs_result, h_respos_ne, h_rep, h_neg, h_result_ne, h_isNorm,
    h_truth_top⟩

end XRPL.Model.Protocol

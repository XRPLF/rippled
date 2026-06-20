import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Mul.Common.Decompose

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## Algorithmic decomposition for `.upward`-mode `operator_mul`. -/

theorem operator_mul_algorithmic_facts_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      MulFactsSpec x y result .upward zm ze' f g res_pos := by
  obtain ⟨zn, zm, ze', f, g, res, hzn, hzm_succ, hzm_le_maxRep, hf_nn, hf_lt, hfloor,
      habs, hf_rep, hsbit, h_rup, hok'⟩ :=
    operator_mul_decompose x y result .upward hx hy hx_mant_ne hy_mant_ne hok
  have hzm_ge : zm.toNat ≥ mantissaFloor := by omega
  have hres_mant_ne : res.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result hok' hresult
  have h_inv := doRoundUp_output_invariants_upward_upTo_maxRepUp g zn zm ze' hzm_ge hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant_ne
  obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
  set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
  have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
  have h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .upward "Number::multiplication overflow" = .ok res_pos :=
    doRoundUp_false_from_ok g zn zm ze' .upward "Number::multiplication overflow" res h_rup
  have h_result_eq_res : result = res.toNumber :=
    Number.normalize_eq_of_invariants h_res_min h_res_max h_res_exp h_res_mod hok'
  have h_result_abs : |result.toRat|
      = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
  have h_res_neg_eq_zn : result.negative_ = zn := by
    rw [h_result_eq_res]
    exact doRoundUp_negative_of_mant_ne g zn zm ze' _ _ _ "Number::multiplication overflow" res h_rup hres_mant_ne
  exact ⟨zm, ze', f, g, res_pos,
    ⟨hzm_ge, hzm_le_maxRep, hf_nn, hf_lt, hfloor, habs, h_rup_pos, h_result_abs,
     hres_pos_mant_ne, hf_rep, h_res_neg_eq_zn.trans hzn, hsbit, hzm_succ⟩⟩


end XRPL.Model.Protocol

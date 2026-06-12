import XRPL.Properties.Protocol.Number.Div.ToNearest.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

local syntax "sd_step" : tactic
local macro_rules | `(tactic| sd_step) => `(tactic|
  (conv_lhs => rw [scaleDown128]; rw [dif_pos (by decide)]; rfl))

lemma scaleDown128_div_witness :
    scaleDown128 (922337203685477582500000000000000000 : UInt128) (-36 : Int) Guard.new
        = (922337203685477582, -18,
           { digits_ := 5764607523034234880, xbit_ := false, sbit_ := false }) := by
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step
  conv_lhs => rw [scaleDown128]; rw [dif_neg (by decide)]
  rfl

lemma operator_div_witness :
    Number.operator_div
        ⟨false, 2880280442173067684, 0⟩
        ⟨false, 3122806312771549303, 0⟩
        .to_nearest =
      .ok ⟨false, 9223372036854775820, -19⟩ := by
  unfold Number.operator_div
  have hy_ne : (⟨false, 3122806312771549303, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 2880280442173067684, 0⟩ : Number).operator_eq Number.zero = false := by decide
  simp only [hy_ne, hx_ne, Bool.false_eq_true, if_false]
  have hdq : divQuotient128 (2880280442173067684 : UInt64) (3122806312771549303 : UInt64) 0 0
      = ((922337203685477582500000000000000000 : UInt128), ((-36) : Int)) := by decide
  simp only [hdq]
  simp only [show (922337203685477582500000000000000000 : UInt128) = 0 ↔ False from by decide, if_false]
  simp only [show (false != false) = false from rfl, Bool.false_eq_true, if_false]
  rw [scaleDown128_div_witness]
  simp only [show ((-18 : Int) < minExponent) = false from by decide, Bool.false_eq_true, if_false]
  have h_rup : ({ digits_ := 5764607523034234880, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (922337203685477582 : UInt64) (-18 : Int) largeRange.min largeRange.max .to_nearest
      "Number::operator_div overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854775820, exponent_ := -19 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  rfl

end XRPL.Model.Protocol

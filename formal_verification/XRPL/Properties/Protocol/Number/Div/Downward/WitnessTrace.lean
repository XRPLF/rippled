import XRPL.Properties.Protocol.Number.Div.Downward.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

local syntax "sd_step" : tactic
local macro_rules | `(tactic| sd_step) => `(tactic|
  (conv_lhs => rw [scaleDown128]; rw [dif_pos (by decide)]; rfl))

lemma operator_div_downward_witness :
    Number.operator_div
        ⟨false, 1425000000000000000, 0⟩
        ⟨false, 1544988095791843793, 0⟩
        .downward =
      .ok ⟨false, 9223372036854775950, -19⟩ := by
  unfold Number.operator_div
  have hy_ne : (⟨false, 1544988095791843793, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 1425000000000000000, 0⟩ : Number).operator_eq Number.zero = false := by decide
  simp only [hy_ne, hx_ne, Bool.false_eq_true, if_false]
  have hdq : divQuotient128 (1425000000000000000 : UInt64) (1544988095791843793 : UInt64) 0 0
      = ((922337203685477595999649331516121518 : UInt128), ((-36) : Int)) := by decide
  simp only [hdq]
  simp only [show (922337203685477595999649331516121518 : UInt128) = 0 ↔ False from by decide, if_false]
  simp only [show (false != false) = false from rfl, Bool.false_eq_true, if_false]
  rw [scaleDown128_div_witness_dw]
  simp only [show ((-18 : Int) < minExponent) = false from by decide, Bool.false_eq_true, if_false]
  have h_rup : ({ digits_ := 11067113618055500309, xbit_ := true, sbit_ := false } : Guard).doRoundUp
      false (922337203685477595 : UInt64) (-18 : Int) largeRange.min largeRange.max .downward
      "Number::operator_div overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854775950, exponent_ := -19 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  rfl

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.Number.Div.Upward.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

local syntax "sd_step" : tactic
local macro_rules | `(tactic| sd_step) => `(tactic|
  (conv_lhs => rw [scaleDown128]; rw [dif_pos (by decide)]; rfl))

-- scaleDown128 witness for xm=1340·10^15, ym=1452830911130575894 (upward)
lemma scaleDown128_div_witness_up :
    scaleDown128 (922337203685477611000051654412165808 : UInt128) (-36 : Int) Guard.new
        = (922337203685477611, -18,
           { digits_ := 89495375582808, xbit_ := true, sbit_ := false }) := by
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step
  conv_lhs => rw [scaleDown128]; rw [dif_neg (by decide)]
  rfl

lemma operator_div_upward_witness :
    Number.operator_div
        ⟨false, 1340000000000000000, 0⟩
        ⟨false, 1452830911130575894, 0⟩
        .upward =
      .ok ⟨false, 9223372036854776120, -19⟩ := by
  unfold Number.operator_div
  have hy_ne : (⟨false, 1452830911130575894, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 1340000000000000000, 0⟩ : Number).operator_eq Number.zero = false := by decide
  simp only [hy_ne, hx_ne, Bool.false_eq_true, if_false]
  have hdq : divQuotient128 (1340000000000000000 : UInt64) (1452830911130575894 : UInt64) 0 0
      = ((922337203685477611000051654412165808 : UInt128), ((-36) : Int)) := by decide
  simp only [hdq]
  simp only [show (922337203685477611000051654412165808 : UInt128) = 0 ↔ False from by decide, if_false]
  simp only [show (false != false) = false from rfl, Bool.false_eq_true, if_false]
  rw [scaleDown128_div_witness_up]
  simp only [show ((-18 : Int) < minExponent) = false from by decide, Bool.false_eq_true, if_false]
  have h_rup : ({ digits_ := 89495375582808, xbit_ := true, sbit_ := false } : Guard).doRoundUp
      false (922337203685477611 : UInt64) (-18 : Int) largeRange.min largeRange.max .upward
      "Number::operator_div overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854776120, exponent_ := -19 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  rfl

end XRPL.Model.Protocol

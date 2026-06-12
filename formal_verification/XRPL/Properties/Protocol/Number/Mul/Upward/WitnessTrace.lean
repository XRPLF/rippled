import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.Upward.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `operator_mul_rounding_bound_upward`

The bound `10/(2^63+2)` is tight: the relative error at
`x = maxRepNat`, `y = 1000000000000000009` exceeds `9/(2^63+2)`.
The guard is nonzero so `.upward` rounds up, producing a mantissa > maxRep;
`normalize` fires `capAtMaxRep` to reach `mulUpwardWitness * 10^18`. -/

local syntax "sd_step" : tactic
local macro_rules | `(tactic| sd_step) => `(tactic|
  (conv_lhs => rw [scaleDown128]; rw [dif_pos (by decide)]; rfl))

/-- `scaleDown128` trace for the upward witness inputs. -/
lemma scaleDown128_upward_witness :
    scaleDown128 (toUInt128 (maxRepNat : UInt64) * toUInt128 (1000000000000000009 : UInt64))
                 (0 : Int) Guard.new
        = (922337203685477589, 19,
           { digits_ := 4561337701706114, xbit_ := true, sbit_ := false }) := by
  have hprod : toUInt128 (maxRepNat : UInt64) * toUInt128 (1000000000000000009 : UInt64)
        = (9223372036854775890010348331692982263 : UInt128) := by decide
  rw [hprod]
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step
  conv_lhs => rw [scaleDown128]; rw [dif_neg (by decide)]
  rfl

/-- `doNormalize` trace for the upward witness result.
The mantissa `mulUpwardWitness > maxRep`, so `capAtMaxRep` fires. -/
lemma doNormalize_upward_witness :
    doNormalize false (mulUpwardWitness : UInt64) (18 : Int)
        largeRange.min largeRange.max .upward =
        .ok { negative_ := false, mantissa_ := mulUpwardWitness, exponent_ := 18 } := by
  unfold doNormalize
  rw [show ((mulUpwardWitness : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (mulUpwardWitness : UInt64) (18 : Int)
        = ((mulUpwardWitness : UInt64), (18 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (mulUpwardWitness : UInt64) (18 : Int) Guard.new
        = .ok ((mulUpwardWitness : UInt64), (18 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((18 : Int) < minExponent || (mulUpwardWitness : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  have h_divu : divu10 (mulUpwardWitness : UInt64) = (922337203685477590, 0) := by decide
  rw [show doNormalize_capAtMaxRep (mulUpwardWitness : UInt64) (18 : Int) Guard.new
        = .ok ((922337203685477590 : UInt64), (19 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep
      rw [if_pos (by decide), if_neg (by decide), h_divu]; simp only [guard_new_push_zero]; norm_num]
  simp only []
  have h_rup2 : Guard.new.doRoundUp false (922337203685477590 : UInt64) (19 : Int)
      largeRange.min largeRange.max .upward "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := mulUpwardWitness, exponent_ := 18 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup2]
  simp only [RoundResult.toNumber]

/-- `operator_mul` trace for the upward witness inputs. -/
lemma operator_mul_upward_witness :
    Number.operator_mul
        ⟨false, maxRepNat, 0⟩
        ⟨false, 1000000000000000009, 0⟩
        .upward =
      .ok ⟨false, mulUpwardWitness, 18⟩ := by
  unfold Number.operator_mul
  have hx_ne : (⟨false, maxRepNat, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hy_ne : (⟨false, 1000000000000000009, 0⟩ : Number).operator_eq Number.zero = false := by decide
  simp only [hx_ne, hy_ne, Bool.false_eq_true, if_false, bne_self_eq_false, add_zero]
  rw [scaleDown128_upward_witness]
  have h_rup : ({ digits_ := 4561337701706114, xbit_ := true, sbit_ := false } : Guard).doRoundUp
      false (922337203685477589 : UInt64) (19 : Int) largeRange.min largeRange.max .upward
      "Number::multiplication overflow" =
      .ok { negative_ := false, mantissa_ := mulUpwardWitness, exponent_ := 18 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize
  rw [doNormalize_upward_witness]

end XRPL.Model.Protocol

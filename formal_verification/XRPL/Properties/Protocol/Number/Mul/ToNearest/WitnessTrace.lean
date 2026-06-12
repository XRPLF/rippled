import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.ToNearest.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `operator_mul_rounding_bound`

The bound `5/(2^63 + 7)` is attained at `x = 5*10^18`, `y = (2^63 + 7)/5`. -/

local syntax "sd_step" : tactic
local macro_rules | `(tactic| sd_step) => `(tactic|
  (conv_lhs => rw [scaleDown128]; rw [dif_pos (by decide)]; rfl))

/-- `scaleDown128` trace for the witness inputs. -/
lemma scaleDown128_witness :
    scaleDown128 (toUInt128 (5000000000000000000 : UInt64) * toUInt128 (1844674407370955163 : UInt64))
                 (0 : Int) Guard.new
        = (mantissaFloorSucc, 19,
           { digits_ := 5764607523034234880, xbit_ := false, sbit_ := false }) := by
  have hprod : toUInt128 (5000000000000000000 : UInt64) * toUInt128 (1844674407370955163 : UInt64)
        = (9223372036854775815000000000000000000 : UInt128) := by decide
  rw [hprod]
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step
  conv_lhs => rw [scaleDown128]; rw [dif_neg (by decide)]
  rfl

/-- `doNormalize` trace for the witness result. -/
lemma doNormalize_witness :
    doNormalize false (9223372036854775820 : UInt64) (18 : Int)
        largeRange.min largeRange.max .to_nearest =
        .ok { negative_ := false, mantissa_ := 9223372036854775820, exponent_ := 18 } := by
  unfold doNormalize
  rw [show ((9223372036854775820 : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (9223372036854775820 : UInt64) (18 : Int)
        = ((9223372036854775820 : UInt64), (18 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (9223372036854775820 : UInt64) (18 : Int) Guard.new
        = .ok ((9223372036854775820 : UInt64), (18 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((18 : Int) < minExponent || (9223372036854775820 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  have h_divu : divu10 (9223372036854775820 : UInt64) = (922337203685477582, 0) := by decide
  rw [show doNormalize_capAtMaxRep (9223372036854775820 : UInt64) (18 : Int) Guard.new
        = .ok ((922337203685477582 : UInt64), (19 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep
      rw [if_pos (by decide), if_neg (by decide), h_divu]; simp only [guard_new_push_zero]; norm_num]
  simp only []
  have h_rup : Guard.new.doRoundUp false (922337203685477582 : UInt64) (19 : Int)
      largeRange.min largeRange.max .to_nearest "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 9223372036854775820, exponent_ := 18 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]

/-- `operator_mul` trace for the witness inputs. -/
lemma operator_mul_witness :
    Number.operator_mul
        ⟨false, 5000000000000000000, 0⟩
        ⟨false, 1844674407370955163, 0⟩
        .to_nearest =
      .ok ⟨false, 9223372036854775820, 18⟩ := by
  unfold Number.operator_mul
  have hx_ne : (⟨false, 5000000000000000000, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hy_ne : (⟨false, 1844674407370955163, 0⟩ : Number).operator_eq Number.zero = false := by decide
  simp only [hx_ne, hy_ne, Bool.false_eq_true, if_false, bne_self_eq_false, add_zero]
  rw [scaleDown128_witness]
  -- Now the goal has doRoundUp { digits_ := ..., xbit_ := false, sbit_ := false } false mantissaFloorSucc 19 ...
  have h_rup : ({ digits_ := 5764607523034234880, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (mantissaFloorSucc : UInt64) (19 : Int) largeRange.min largeRange.max .to_nearest
      "Number::multiplication overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854775820, exponent_ := 18 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize
  rw [doNormalize_witness]

end XRPL.Model.Protocol

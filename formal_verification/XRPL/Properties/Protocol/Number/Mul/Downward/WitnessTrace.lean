import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.Downward.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `operator_mul_rounding_bound_downward`

The bound `10/(2^63+2)` is tight: the relative error at
`x = 9223372036854775800`, `y = 1000000000000000001` exceeds `9/(2^63+2)`,
proving that no bound of the form `k/(2^63+2)` with `k ≤ 9` suffices. -/

local syntax "sd_step" : tactic
local macro_rules | `(tactic| sd_step) => `(tactic|
  (conv_lhs => rw [scaleDown128]; rw [dif_pos (by decide)]; rfl))

/-- `scaleDown128` trace for the downward witness inputs. -/
lemma scaleDown128_downward_witness :
    scaleDown128 (toUInt128 (9223372036854775800 : UInt64) * toUInt128 (1000000000000000001 : UInt64))
                 (0 : Int) Guard.new
        = (mantissaFloor, 19,
           { digits_ := 10530320965215537013, xbit_ := true, sbit_ := false }) := by
  have hprod : toUInt128 (9223372036854775800 : UInt64) * toUInt128 (1000000000000000001 : UInt64)
        = (9223372036854775809223372036854775800 : UInt128) := by decide
  rw [hprod]
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step
  conv_lhs => rw [scaleDown128]; rw [dif_neg (by decide)]
  rfl

/-- `operator_mul` trace for the downward witness inputs. -/
lemma operator_mul_downward_witness :
    Number.operator_mul
        ⟨false, 9223372036854775800, 0⟩
        ⟨false, 1000000000000000001, 0⟩
        .downward =
      .ok ⟨false, 9223372036854775800, 18⟩ := by
  unfold Number.operator_mul
  have hx_ne : (⟨false, 9223372036854775800, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hy_ne : (⟨false, 1000000000000000001, 0⟩ : Number).operator_eq Number.zero = false := by decide
  simp only [hx_ne, hy_ne, Bool.false_eq_true, if_false, bne_self_eq_false, add_zero]
  rw [scaleDown128_downward_witness]
  have h_rup : ({ digits_ := 10530320965215537013, xbit_ := true, sbit_ := false } : Guard).doRoundUp
      false (mantissaFloor : UInt64) (19 : Int) largeRange.min largeRange.max .downward
      "Number::multiplication overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 18 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize
  rw [show doNormalize false (9223372036854775800 : UInt64) (18 : Int)
        largeRange.min largeRange.max .downward
        = .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 18 } from by
    unfold doNormalize
    rw [show ((9223372036854775800 : UInt64) == 0) = false from rfl]
    simp only [Bool.false_eq_true, if_false]
    rw [show doNormalize_scaleUp largeRange.min (9223372036854775800 : UInt64) (18 : Int)
          = ((9223372036854775800 : UInt64), (18 : Int)) by
          unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
    rw [show doNormalize_scaleDown largeRange.max (9223372036854775800 : UInt64) (18 : Int) Guard.new
          = .ok ((9223372036854775800 : UInt64), (18 : Int), Guard.new) by
          unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
    simp only []
    rw [show ((18 : Int) < minExponent || (9223372036854775800 : UInt64) < largeRange.min) = false from by decide]
    simp only [Bool.false_eq_true, if_false]
    rw [show doNormalize_capAtMaxRep (9223372036854775800 : UInt64) (18 : Int) Guard.new
          = .ok ((9223372036854775800 : UInt64), (18 : Int), Guard.new) by
        unfold doNormalize_capAtMaxRep; rw [if_neg (by decide)]]
    simp only []
    have h_rup2 : Guard.new.doRoundUp false (9223372036854775800 : UInt64) (18 : Int)
        largeRange.min largeRange.max .downward "Number::normalize 2" =
        .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 18 } := by
      unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
    rw [h_rup2]
    simp only [RoundResult.toNumber]]

end XRPL.Model.Protocol

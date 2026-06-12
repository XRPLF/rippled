import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `operator_add_rounding_bound_same_sign_downward`

The bound `10/(2^63 + 2)` is tight: at `x = 5000000000000000001` and
`y = 4223372036854775808` (both exp 0), the exact sum `9223372036854775809
= 2^63 + 1` exceeds `maxRep`, triggering the 128-bit drop. After the drop
the mantissa is `mantissaFloor` at exp 1 with guard digit 9. For
`.downward` with positive operands (`sbit_=false`), no round-up fires;
`bringIntoRange` rescales `m < min` to `9223372036854775800 * 10^0`. Truncation
error is `9`, relative error `9/9223372036854775809 > 9/(2^63 + 2)`. -/

/-- `operator_add` trace for the downward witness inputs. -/
lemma operator_add_downward_witness :
    Number.operator_add
        ⟨false, 5000000000000000001, 0⟩
        ⟨false, 4223372036854775808, 0⟩
        .downward =
      .ok ⟨false, 9223372036854775800, 0⟩ := by
  unfold Number.operator_add
  have hy_ne : (⟨false, 4223372036854775808, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 5000000000000000001, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hxy_neg_ne : (⟨false, 5000000000000000001, 0⟩ : Number).operator_eq
      (⟨false, 4223372036854775808, 0⟩ : Number).operator_neg = false := by decide
  simp only [hy_ne, hx_ne, hxy_neg_ne, Bool.false_eq_true, if_false]
  rw [if_neg (by decide : ¬ ((0 : Int) < 0))]
  rw [if_neg (by decide : ¬ ((0 : Int) > 0))]
  rw [show ((false : Bool) == (false : Bool)) = true from rfl]
  simp only [if_true]
  have hsum : toUInt128 (5000000000000000001 : UInt64) + toUInt128 (4223372036854775808 : UInt64)
      = (9223372036854775809 : UInt128) := by decide
  rw [hsum]
  rw [show (decide ((9223372036854775809 : UInt128) > toUInt128 largeRange.max) ||
           decide ((9223372036854775809 : UInt128) > toUInt128 maxRep)) = true from by decide]
  simp only [if_true]
  have hdrop : Guard.new.doDropDigit128 (9223372036854775809 : UInt128) 0
      = ({ digits_ := 10376293541461622784, xbit_ := false, sbit_ := false },
         (mantissaFloor : UInt128), (1 : Int)) := by
    unfold Guard.doDropDigit128 Guard.new Guard.push; rfl
  rw [hdrop]
  simp only []
  have htoUInt64 : toUInt64 (mantissaFloor : UInt128) = (mantissaFloor : UInt64) := by decide
  rw [htoUInt64]
  have h_rup : ({ digits_ := 10376293541461622784, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (mantissaFloor : UInt64) (1 : Int) largeRange.min largeRange.max .downward
      "Number::addition overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit
    rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize doNormalize
  rw [show (((9223372036854775800 : UInt64) : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (9223372036854775800 : UInt64) 0
        = ((9223372036854775800 : UInt64), (0 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (9223372036854775800 : UInt64) (0 : Int) Guard.new
        = .ok ((9223372036854775800 : UInt64), (0 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((0 : Int) < minExponent || (9223372036854775800 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (9223372036854775800 : UInt64) (0 : Int) Guard.new
        = .ok ((9223372036854775800 : UInt64), (0 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep
      rw [if_neg (by decide)]]
  simp only []
  have h_rup2 : Guard.new.doRoundUp false (9223372036854775800 : UInt64) (0 : Int)
      largeRange.min largeRange.max .downward "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup2]
  simp only [RoundResult.toNumber]

end XRPL.Model.Protocol

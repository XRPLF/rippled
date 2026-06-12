import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Downward.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `normalize_rounding_bound_downward`

The bound `10/(2^63 + 2)` is a tight **supremum** but is **NOT attained**:
`.downward` truncates a positive magnitude toward `-∞` (i.e. toward zero), so the
shortfall behaves exactly like `.towards_zero` on positive inputs and is strictly
below the bound (the guard fraction `f < 1` strictly). We state sharpness the way
the operator does: the relative error strictly exceeds the next-smaller numerator
`9`.

Witness value `2^63 + 1 = twoPow63Add1` (positive mantissa, exponent 0).
`scaleUp`/`scaleDown` are no-ops; `capAtMaxRep` divides by ten (the value exceeds
`maxRep`), peeling guard digit `9` and producing the floor mantissa
`mantissaFloor` at exponent 1. With `sbit_ = false` (positive) the `.downward`
`round = -1`, so `doRoundUp` does NOT round up; `bringIntoRange` rescales
(`mantissaFloor < 10^18`) by `*10` back to `twoPow63Sub8` at exponent 0.
Error = 9. -/

/-- `Number.normalize` trace for the downward witness input `2^63 + 1`. -/
lemma normalize_downward_witness :
    Number.normalize ⟨false, twoPow63Add1, 0⟩
        largeRange.min largeRange.max .downward =
      .ok ⟨false, twoPow63Sub8, 0⟩ := by
  unfold Number.normalize doNormalize
  rw [show (((twoPow63Add1 : UInt64) : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (twoPow63Add1 : UInt64) 0
        = ((twoPow63Add1 : UInt64), (0 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (twoPow63Add1 : UInt64) (0 : Int) Guard.new
        = .ok ((twoPow63Add1 : UInt64), (0 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((0 : Int) < minExponent || (twoPow63Add1 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  have h_divu : divu10 (twoPow63Add1 : UInt64) = (mantissaFloor, 9) := by decide
  rw [show doNormalize_capAtMaxRep (twoPow63Add1 : UInt64) (0 : Int) Guard.new
        = .ok ((mantissaFloor : UInt64), (1 : Int), Guard.new.push 9) by
      unfold doNormalize_capAtMaxRep
      rw [if_pos (by decide), if_neg (by decide), h_divu]
      norm_num]
  simp only []
  have h_rup : (Guard.new.push 9).doRoundUp false (mantissaFloor : UInt64) (1 : Int)
      largeRange.min largeRange.max .downward "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := twoPow63Sub8, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.push Guard.new; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]

end XRPL.Model.Protocol

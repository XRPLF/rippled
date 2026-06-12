import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Upward.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `normalize_rounding_bound_upward`

The bound `10/(2^63 + 2)` is a tight **supremum** but is **NOT attained**. For a
**positive** value `.upward` rounds the magnitude up by at most one ULP-at-scale,
and the 64-bit input mantissa prevents the residue `f` from being driven below
the granularity reachable by the (at most two) dropped digits. The worst relative
case is instead a **negative** value, for which `.upward` truncates the magnitude
toward zero (away from `-∞`), exactly like `.towards_zero` on positive magnitudes,
exhibiting a shortfall of up to (just below) `1` ULP at the floor. We state
sharpness the way the operator does: the relative error strictly exceeds the
next-smaller numerator `9`.

Witness value `-(2^63 + 1) = -twoPow63Add1` (negative mantissa, exponent 0).
`scaleUp`/`scaleDown` are no-ops; `capAtMaxRep` divides by ten (the magnitude
exceeds `maxRep`), peeling guard digit `9` and producing the floor mantissa
`mantissaFloor` at exponent 1. With `sbit_ = true` (negative) the `.upward`
`round = -1`, so `doRoundUp` does NOT round the magnitude up; `bringIntoRange`
rescales (`mantissaFloor < 10^18`) by `*10` back to magnitude `twoPow63Sub8` at
exponent 0, keeping the negative sign. Magnitude error = 9. -/

/-- `Number.normalize` trace for the upward witness input `-(2^63 + 1)`. -/
lemma normalize_upward_witness :
    Number.normalize ⟨true, twoPow63Add1, 0⟩
        largeRange.min largeRange.max .upward =
      .ok ⟨true, twoPow63Sub8, 0⟩ := by
  unfold Number.normalize doNormalize
  rw [show (((twoPow63Add1 : UInt64) : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (twoPow63Add1 : UInt64) 0
        = ((twoPow63Add1 : UInt64), (0 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  simp only [if_true]
  rw [show doNormalize_scaleDown largeRange.max (twoPow63Add1 : UInt64) (0 : Int) Guard.new.set_negative
        = .ok ((twoPow63Add1 : UInt64), (0 : Int), Guard.new.set_negative) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((0 : Int) < minExponent || (twoPow63Add1 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  have h_divu : divu10 (twoPow63Add1 : UInt64) = (mantissaFloor, 9) := by decide
  rw [show doNormalize_capAtMaxRep (twoPow63Add1 : UInt64) (0 : Int) Guard.new.set_negative
        = .ok ((mantissaFloor : UInt64), (1 : Int), Guard.new.set_negative.push 9) from by
      unfold doNormalize_capAtMaxRep
      rw [if_pos (by decide), if_neg (by decide), h_divu]
      norm_num]
  simp only []
  have h_rup : (Guard.new.set_negative.push 9).doRoundUp true (mantissaFloor : UInt64) (1 : Int)
      largeRange.min largeRange.max .upward "Number::normalize 2" =
      .ok { negative_ := true, mantissa_ := twoPow63Sub8, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.push Guard.set_negative Guard.new; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]

end XRPL.Model.Protocol

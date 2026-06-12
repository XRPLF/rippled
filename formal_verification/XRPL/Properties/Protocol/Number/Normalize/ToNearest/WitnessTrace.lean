import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.ToNearest.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `normalize_rounding_bound_to_nearest`

The bound `5/(2^63 + 7)` is attained at the value `2^63 + 7 = twoPow63Add7`
(mantissa, exponent 0). `scaleUp`/`scaleDown` are no-ops; `capAtMaxRep` divides by
ten (the value exceeds `maxRep`), pushing guard digit `5` and producing the odd
mantissa `mantissaFloorSucc` at exponent 1. The to-nearest tie (guard digit `5`,
odd mantissa) rounds up to `twoPow63Add12`, then `bringIntoRange` scales it
back up to `twoPow63Add12` at exponent 0. Error = 5. -/

/-- `Number.normalize` trace for the to_nearest witness input `2^63 + 7`. -/
lemma normalize_to_nearest_witness :
    Number.normalize ⟨false, twoPow63Add7, 0⟩
        largeRange.min largeRange.max .to_nearest =
      .ok ⟨false, twoPow63Add12, 0⟩ := by
  unfold Number.normalize doNormalize
  rw [show (((twoPow63Add7 : UInt64) : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (twoPow63Add7 : UInt64) 0
        = ((twoPow63Add7 : UInt64), (0 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (twoPow63Add7 : UInt64) (0 : Int) Guard.new
        = .ok ((twoPow63Add7 : UInt64), (0 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((0 : Int) < minExponent || (twoPow63Add7 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  have h_divu : divu10 (twoPow63Add7 : UInt64) = (mantissaFloorSucc, 5) := by decide
  rw [show doNormalize_capAtMaxRep (twoPow63Add7 : UInt64) (0 : Int) Guard.new
        = .ok ((mantissaFloorSucc : UInt64), (1 : Int), Guard.new.push 5) from by
      unfold doNormalize_capAtMaxRep
      rw [if_pos (by decide), if_neg (by decide), h_divu]
      norm_num]
  simp only []
  have h_rup : (Guard.new.push 5).doRoundUp false (mantissaFloorSucc : UInt64) (1 : Int)
      largeRange.min largeRange.max .to_nearest "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := twoPow63Add12, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.push Guard.new; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]

end XRPL.Model.Protocol

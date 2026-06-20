import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.BoundProof


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


/-- The `5/(2^63+7)` to-nearest normalize bound is attained: normalizing the
    value `2^63+7` rounds up by exactly 5 ULP-at-scale (tie on an odd mantissa),
    giving relative error exactly `5/(2^63+7)`. So 5 is the optimal numerator. -/
theorem normalize_to_nearest_attained :
    ∃ (n result : Number),
      Number.normalize n largeRange.min largeRange.max .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - n.toRat| = |n.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  refine ⟨⟨false, twoPow63Add7, 0⟩,
          ⟨false, twoPow63Add12, 0⟩,
          ?_, ?_, ?_⟩
  · exact normalize_to_nearest_witness
  · decide
  · have hn_rat : (⟨false, twoPow63Add7, 0⟩ : Number).toRat = twoPow63Add7 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, twoPow63Add12, 0⟩ : Number).toRat = twoPow63Add12 := by
      unfold Number.toRat; rfl
    rw [hn_rat, hr_rat]
    change |(twoPow63Add12 : ℚ) - twoPow63Add7|
       = |(twoPow63Add7 : ℚ)| * (5 / (2 ^ 63 + 7 : ℚ))
    rw [show ((2 : ℚ) ^ 63 + 7) = twoPow63Add7 by norm_num]
    rw [show (twoPow63Add12 : ℚ) - twoPow63Add7 = 5 by norm_num]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 5)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < twoPow63Add7)]
    field_simp

end XRPL.Model.Protocol

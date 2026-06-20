import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.TowardsZero.BoundProof


namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `normalize_rounding_bound_towards_zero`

Witness value `10^19 + 9` (positive mantissa, exponent 0). `scaleUp` is a no-op;
`scaleDown` fires once (the value exceeds `largeRange.max`), peeling guard digit
`9` and producing mantissa `10^18` at exponent 1; `capAtMaxRep` is the identity.
`.towards_zero` truncates (`round = -1`). Error = 9, relative error
`9/(10^19 + 9) > 8/(2^63 + 2)`. -/

/-- `Number.normalize` trace for the towards_zero witness input `10^19 + 9`. -/
lemma normalize_towards_zero_witness :
    Number.normalize ⟨false, 10000000000000000009, 0⟩
        largeRange.min largeRange.max .towards_zero =
      .ok ⟨false, 1000000000000000000, 1⟩ := by
  unfold Number.normalize doNormalize
  rw [show (((10000000000000000009 : UInt64) : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (10000000000000000009 : UInt64) 0
        = ((10000000000000000009 : UInt64), (0 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (10000000000000000009 : UInt64) (0 : Int) Guard.new
        = .ok ((1000000000000000000 : UInt64), (1 : Int), Guard.new.push 9) by
        rw [doNormalize_scaleDown, dif_pos (by decide), if_neg (by decide)]
        rw [show (10000000000000000009 : UInt64) / 10 = (1000000000000000000 : UInt64) from by decide,
            show (10000000000000000009 : UInt64) % 10 = (9 : UInt64) from by decide,
            show ((0 : Int) + 1) = (1 : Int) from by norm_num]
        rw [doNormalize_scaleDown, dif_neg (by decide)]]
  simp only []
  rw [show ((1 : Int) < minExponent || (1000000000000000000 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (1000000000000000000 : UInt64) (1 : Int) (Guard.new.push 9)
        = .ok ((1000000000000000000 : UInt64), (1 : Int), Guard.new.push 9) from by
      unfold doNormalize_capAtMaxRep
      rw [if_neg (by decide)]]
  simp only []
  have h_rup : (Guard.new.push 9).doRoundUp false (1000000000000000000 : UInt64) (1 : Int)
      largeRange.min largeRange.max .towards_zero "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 1000000000000000000, exponent_ := 1 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.push Guard.new
    rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]


/-- Sharpness witness: the relative error exceeds `8/(2^63 + 2)`. (The truncation
supremum after the f765ab1 cusp threshold is `9·10⁻¹⁹` — witnessed by 19-digit
inputs with guard digit 9 — slightly below the stated `10/(2^63 + 2)` scale.) -/
theorem normalize_towards_zero_attained :
    ∃ (n result : Number),
      Number.normalize n largeRange.min largeRange.max .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - n.toRat| > |n.toRat| * (8 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 10000000000000000009, 0⟩,
          ⟨false, 1000000000000000000, 1⟩,
          ?_, ?_, ?_⟩
  · exact normalize_towards_zero_witness
  · decide
  · have hn_rat : (⟨false, 10000000000000000009, 0⟩ : Number).toRat = 10000000000000000009 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 1000000000000000000, 1⟩ : Number).toRat
        = 1000000000000000000 * (10 : ℚ) ^ (1 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hn_rat, hr_rat]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [show (1000000000000000000 : ℚ) * (10 : ℚ) ^ (1 : ℕ) - 10000000000000000009
          = -9 by norm_num]
    rw [abs_neg, abs_of_pos (by norm_num : (0 : ℚ) < 9)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 10000000000000000009)]
    rw [gt_iff_lt, show (10000000000000000009 : ℚ) * (8 / maxRepCuspTarget)
                       = 8 * 10000000000000000009 / maxRepCuspTarget by ring]
    rw [div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.ProofTactics
import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize


namespace XRPL.Model.Protocol

lemma operator_add_to_nearest_witness :
    Number.operator_add
        ⟨false, 5000000000000000000, 0⟩
        ⟨false, 4223372036854775815, 0⟩
        .to_nearest =
      .ok ⟨false, 9223372036854775820, 0⟩ := by
  unfold Number.operator_add
  have hy_ne : (⟨false, 4223372036854775815, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 5000000000000000000, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hxy_neg_ne : (⟨false, 5000000000000000000, 0⟩ : Number).operator_eq
      (⟨false, 4223372036854775815, 0⟩ : Number).operator_neg = false := by decide
  simp only [hy_ne, hx_ne, hxy_neg_ne, Bool.false_eq_true, if_false]
  rw [if_neg (by decide : ¬ ((0 : Int) < 0))]
  rw [if_neg (by decide : ¬ ((0 : Int) > 0))]
  rw [show ((false : Bool) == (false : Bool)) = true from rfl]
  simp only [if_true]
  have hsum : toUInt128 (5000000000000000000 : UInt64) + toUInt128 (4223372036854775815 : UInt64)
      = (9223372036854775815 : UInt128) := by decide
  rw [hsum]
  rw [show (decide ((9223372036854775815 : UInt128) > toUInt128 largeRange.max) ||
           decide ((9223372036854775815 : UInt128) > toUInt128 maxRepUp)) = true from by decide]
  simp only [if_true]
  have hdrop : Guard.new.doDropDigit128 (9223372036854775815 : UInt128) 0
      = ({ digits_ := 5764607523034234880, xbit_ := false, sbit_ := false },
         (mantissaFloorSucc : UInt128), (1 : Int)) := by
    unfold Guard.doDropDigit128 Guard.new Guard.push; rfl
  rw [hdrop]
  simp only
  have htoUInt64 : toUInt64 (mantissaFloorSucc : UInt128) = (mantissaFloorSucc : UInt64) := by decide
  rw [htoUInt64]
  have h_rup : ({ digits_ := 5764607523034234880, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (mantissaFloorSucc : UInt64) (1 : Int) largeRange.min largeRange.max .to_nearest
      "Number::addition overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854775820, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit
    rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize doNormalize
  rw [show (((9223372036854775820 : UInt64) : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (9223372036854775820 : UInt64) 0
        = ((9223372036854775820 : UInt64), (0 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (9223372036854775820 : UInt64) (0 : Int) Guard.new
        = .ok ((9223372036854775820 : UInt64), (0 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((0 : Int) < minExponent || (9223372036854775820 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  have h_divu : divu10 (9223372036854775820 : UInt64) = (922337203685477582, 0) := by decide
  rw [show doNormalize_capAtMaxRep (9223372036854775820 : UInt64) (0 : Int) Guard.new
        = .ok ((922337203685477582 : UInt64), (1 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep
      rw [if_pos (by decide), if_neg (by decide), h_divu]
      simp only [guard_new_push_zero]; norm_num]
  simp only []
  have h_rup2 : Guard.new.doRoundUp false (922337203685477582 : UInt64) (1 : Int)
      largeRange.min largeRange.max .to_nearest "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 9223372036854775820, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup2]
  simp only [RoundResult.toNumber]

/-- The bound `5/(2^63 + 7)` is attained exactly, so it is the tight supremum.
Witness: `x = 5e18`, `y = 4223372036854775815`. Sum = `9223372036854775815 = 2^63 + 7`.
After scaleDown: mantissa at `floor + 1` (odd), guard digit = 5 → tie-to-even rounds up.
Error = 5, relative = `5/(2^63 + 7)`. -/
theorem operator_add_to_nearest_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_add x y .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        = |x.toRat + y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  refine ⟨⟨false, 5000000000000000000, 0⟩,
          ⟨false, 4223372036854775815, 0⟩,
          ⟨false, 9223372036854775820, 0⟩,
          ?_, ?_, ?_, ?_, ?_⟩
  · norm_isNormalized
  · norm_isNormalized
  · exact operator_add_to_nearest_witness
  · decide
  · have hx_rat : (⟨false, 5000000000000000000, 0⟩ : Number).toRat = 5000000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 4223372036854775815, 0⟩ : Number).toRat = 4223372036854775815 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775820, 0⟩ : Number).toRat = 9223372036854775820 := by
      unfold Number.toRat; rfl
    rw [hx_rat, hy_rat, hr_rat]
    change |(9223372036854775820 : ℚ) - (5000000000000000000 + 4223372036854775815)|
       = |(5000000000000000000 : ℚ) + 4223372036854775815| * (5 / (2 ^ 63 + 7 : ℚ))
    rw [show ((2 : ℚ) ^ 63 + 7) = 9223372036854775815 by norm_num]
    rw [show (5000000000000000000 : ℚ) + 4223372036854775815 = 9223372036854775815 by norm_num]
    rw [show (9223372036854775820 : ℚ) - 9223372036854775815 = 5 by norm_num]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 5)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9223372036854775815)]
    field_simp

end XRPL.Model.Protocol

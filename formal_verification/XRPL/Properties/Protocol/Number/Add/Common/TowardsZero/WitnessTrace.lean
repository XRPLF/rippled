import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.ProofTactics
import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize


namespace XRPL.Model.Protocol

lemma operator_add_towards_zero_witness :
    Number.operator_add
        ⟨false, 5000000000000000004, 0⟩
        ⟨false, 5000000000000000005, 0⟩
        .towards_zero =
      .ok ⟨false, 1000000000000000000, 1⟩ := by
  unfold Number.operator_add
  have hy_ne : (⟨false, 5000000000000000005, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 5000000000000000004, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hxy_neg_ne : (⟨false, 5000000000000000004, 0⟩ : Number).operator_eq
      (⟨false, 5000000000000000005, 0⟩ : Number).operator_neg = false := by decide
  simp only [hy_ne, hx_ne, hxy_neg_ne, Bool.false_eq_true, if_false]
  rw [if_neg (by decide : ¬ ((0 : Int) < 0))]
  rw [if_neg (by decide : ¬ ((0 : Int) > 0))]
  rw [show ((false : Bool) == (false : Bool)) = true from rfl]
  simp only [if_true]
  have hsum : toUInt128 (5000000000000000004 : UInt64) + toUInt128 (5000000000000000005 : UInt64)
      = (10000000000000000009 : UInt128) := by decide
  rw [hsum]
  rw [show (decide ((10000000000000000009 : UInt128) > toUInt128 largeRange.max) ||
           decide ((10000000000000000009 : UInt128) > toUInt128 maxRepUp)) = true from by decide]
  simp only [if_true]
  have hdrop : Guard.new.doDropDigit128 (10000000000000000009 : UInt128) 0
      = ({ digits_ := 10376293541461622784, xbit_ := false, sbit_ := false },
         (1000000000000000000 : UInt128), (1 : Int)) := by
    unfold Guard.doDropDigit128 Guard.new Guard.push; rfl
  rw [hdrop]
  simp only []
  have htoUInt64 : toUInt64 (1000000000000000000 : UInt128) = (1000000000000000000 : UInt64) := by decide
  rw [htoUInt64]
  have h_rup : ({ digits_ := 10376293541461622784, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (1000000000000000000 : UInt64) (1 : Int) largeRange.min largeRange.max .towards_zero
      "Number::addition overflow" =
      .ok { negative_ := false, mantissa_ := 1000000000000000000, exponent_ := 1 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit
    rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize doNormalize
  rw [show (((1000000000000000000 : UInt64) : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (1000000000000000000 : UInt64) 1
        = ((1000000000000000000 : UInt64), (1 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (1000000000000000000 : UInt64) (1 : Int) Guard.new
        = .ok ((1000000000000000000 : UInt64), (1 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((1 : Int) < minExponent || (1000000000000000000 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (1000000000000000000 : UInt64) (1 : Int) Guard.new
        = .ok ((1000000000000000000 : UInt64), (1 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep
      rw [if_neg (by decide)]]
  simp only []
  have h_rup2 : Guard.new.doRoundUp false (1000000000000000000 : UInt64) (1 : Int)
      largeRange.min largeRange.max .towards_zero "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 1000000000000000000, exponent_ := 1 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup2]
  simp only [RoundResult.toNumber]

theorem operator_add_towards_zero_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_add x y .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        > |x.toRat + y.toRat| * (8 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 5000000000000000004, 0⟩,
          ⟨false, 5000000000000000005, 0⟩,
          ⟨false, 1000000000000000000, 1⟩,
          ?_, ?_, ?_, ?_, ?_⟩
  · norm_isNormalized
  · norm_isNormalized
  · exact operator_add_towards_zero_witness
  · decide
  · have hx_rat : (⟨false, 5000000000000000004, 0⟩ : Number).toRat = 5000000000000000004 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 5000000000000000005, 0⟩ : Number).toRat = 5000000000000000005 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 1000000000000000000, 1⟩ : Number).toRat
        = 1000000000000000000 * (10 : ℚ) ^ (1 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [show (5000000000000000004 : ℚ) + 5000000000000000005 = 10000000000000000009 by norm_num]
    rw [show (1000000000000000000 : ℚ) * (10 : ℚ) ^ (1 : ℕ) - 10000000000000000009
          = -9 by norm_num]
    rw [abs_neg]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 10000000000000000009)]
    rw [gt_iff_lt, show (10000000000000000009 : ℚ) * (8 / maxRepCuspTarget)
                       = 8 * 10000000000000000009 / maxRepCuspTarget by ring]
    rw [div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol

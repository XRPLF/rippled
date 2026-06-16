import XRPL.Properties.Protocol.Number.Add.Common.Decompose
import XRPL.Properties.Protocol.Number.Common.ProofTactics
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize


namespace XRPL.Model.Protocol

set_option maxRecDepth 1024 in
/-- `alignDown` trace from `(10^18, 0)` to exponent `20`: produces mantissa `0`
and a guard whose `digits_ = 2^56` (one packed `1`-nibble at slot 14). -/
lemma alignDown_upward_addwitness :
    Number.operator_add.alignDown (1000000000000000000 : UInt64) (0 : Int) Guard.new (20 : Int)
      = ((0 : UInt64), (20 : Int),
         { digits_ := 72057594037927936, xbit_ := false, sbit_ := false }) := by
  repeat (first | rw [alignDown_step (by decide)] | rw [alignDown_noop (by decide)])
  rfl

/-- `doNormalize` trace for the upward witness post-rounding state. -/
lemma doNormalize_upward_addwitness :
    doNormalize false (1000000000000000001 : UInt64) (20 : Int)
        largeRange.min largeRange.max .upward =
        .ok { negative_ := false, mantissa_ := 1000000000000000001, exponent_ := 20 } := by
  unfold doNormalize
  rw [show ((1000000000000000001 : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (1000000000000000001 : UInt64) 20
        = ((1000000000000000001 : UInt64), (20 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (1000000000000000001 : UInt64) (20 : Int) Guard.new
        = .ok ((1000000000000000001 : UInt64), (20 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((20 : Int) < minExponent || (1000000000000000001 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (1000000000000000001 : UInt64) (20 : Int) Guard.new
        = .ok ((1000000000000000001 : UInt64), (20 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep; rw [if_neg (by decide)]]
  simp only []
  rw [show Guard.new.doRoundUp false (1000000000000000001 : UInt64) (20 : Int)
        largeRange.min largeRange.max .upward "Number::normalize 2" =
        .ok { negative_ := false, mantissa_ := 1000000000000000001, exponent_ := 20 } from by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl]
  rfl

/-- `operator_add` trace for the upward witness inputs. -/
lemma operator_add_upward_witness :
    Number.operator_add
        ⟨false, 1000000000000000000, 0⟩
        ⟨false, 1000000000000000000, 20⟩
        .upward =
      .ok ⟨false, 1000000000000000001, 20⟩ := by
  unfold Number.operator_add
  conv_lhs =>
    rw [show (⟨false, 1000000000000000000, 0⟩ : Number).operator_eq Number.zero = false from by decide,
        show (⟨false, 1000000000000000000, 20⟩ : Number).operator_eq Number.zero = false from by decide,
        show (⟨false, 1000000000000000000, 0⟩ : Number).operator_eq
            (⟨false, 1000000000000000000, 20⟩ : Number).operator_neg = false from by decide,
        if_neg (by decide : ¬ false = true),
        if_neg (by decide : ¬ false = true),
        if_neg (by decide : ¬ false = true)]
  dsimp only
  rw [if_pos (by decide : (0 : Int) < 20)]
  rw [show (if (false : Bool) = true then Guard.new.set_negative else Guard.new) = Guard.new from if_neg (by decide)]
  rw [alignDown_upward_addwitness]
  dsimp only
  rw [show ((false : Bool) == false) = true from rfl]
  rw [if_pos rfl]
  rw [show toUInt128 (0 : UInt64) + toUInt128 (1000000000000000000 : UInt64)
        = (1000000000000000000 : UInt128) from by decide]
  rw [if_neg (by decide : ¬ ((decide ((1000000000000000000 : UInt128) > toUInt128 largeRange.max) ||
           decide ((1000000000000000000 : UInt128) > toUInt128 maxRepUp)) = true))]
  rw [show toUInt64 (1000000000000000000 : UInt128) = (1000000000000000000 : UInt64) from by decide]
  rw [show ({ digits_ := 72057594037927936, xbit_ := false, sbit_ := false } : Guard).doRoundUp
        false (1000000000000000000 : UInt64) (20 : Int) largeRange.min largeRange.max .upward
        "Number::addition overflow" =
        .ok { negative_ := false, mantissa_ := 1000000000000000001, exponent_ := 20 } from by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit
    rfl]
  simp only [RoundResult.toNumber]
  unfold Number.normalize
  exact doNormalize_upward_addwitness

theorem operator_add_upward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_add x y .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        > |x.toRat + y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 1000000000000000000, 0⟩,
          ⟨false, 1000000000000000000, 20⟩,
          ⟨false, 1000000000000000001, 20⟩,
          ?_, ?_, ?_, ?_, ?_⟩
  · norm_isNormalized
  · norm_isNormalized
  · exact operator_add_upward_witness
  · decide
  · have hx_rat : (⟨false, 1000000000000000000, 0⟩ : Number).toRat = 1000000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1000000000000000000, 20⟩ : Number).toRat
                = 1000000000000000000 * (10 : ℚ) ^ (20 : ℕ) := by
      unfold Number.toRat; simp; ring
    have hr_rat : (⟨false, 1000000000000000001, 20⟩ : Number).toRat
                = 1000000000000000001 * (10 : ℚ) ^ (20 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [show (1000000000000000000 : ℚ) + 1000000000000000000 * (10 : ℚ) ^ (20 : ℕ)
          = 100000000000000000001000000000000000000 by norm_num]
    rw [show (1000000000000000001 : ℚ) * (10 : ℚ) ^ (20 : ℕ)
            - 100000000000000000001000000000000000000
          = 99000000000000000000 by norm_num]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 99000000000000000000)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 100000000000000000001000000000000000000)]
    rw [show (100000000000000000001000000000000000000 : ℚ) * (9 / (maxRepCuspTarget : ℚ))
          = 9 * 100000000000000000001000000000000000000 / maxRepCuspTarget by ring]
    rw [gt_iff_lt, div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol

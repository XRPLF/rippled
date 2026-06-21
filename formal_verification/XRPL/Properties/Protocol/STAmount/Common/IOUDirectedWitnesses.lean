import XRPL.Properties.Protocol.STAmount.Common.IOUWitnessTraces
import XRPL.Properties.Protocol.STAmount.Mul.Common.IOU
import XRPL.Properties.Protocol.Number.Common.Rounding.SmallRange

/-! # Tightness witnesses for the directed-mode IOU `RoundsWithin` headlines

Companions to `operator_{add,sub,mul}_rounds_iou_{downward,upward,towards_zero}`: concrete
canonical operands whose directed rounding error exceeds `|truth|·4/10¹⁶` (the same
half-ULP construction as the `to_nearest` witnesses; `downward`/`towards_zero` truncate,
`upward` rounds up, all err 5 of a 10-ULP). The inner `Number` op is exact for these
operands, so its trace is mode-generic (`*_lift_trace_g`); the directed re-round is the
proven `normalizeToRange_16_per_mode`. -/

namespace XRPL.Model.Protocol

lemma add_lift_trace_g (mode : rounding_mode) :
    Number.operator_add ⟨false, 5000000000000003000, -3⟩ ⟨false, 5000000000000002000, -3⟩ mode
      = .ok ⟨false, 1000000000000000500, -2⟩ := by
  unfold Number.operator_add
  have hy_ne : (⟨false, 5000000000000002000, -3⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 5000000000000003000, -3⟩ : Number).operator_eq Number.zero = false := by decide
  have hxy : (⟨false, 5000000000000003000, -3⟩ : Number).operator_eq
      (⟨false, 5000000000000002000, -3⟩ : Number).operator_neg = false := by decide
  simp only [hy_ne, hx_ne, hxy, Bool.false_eq_true, if_false]
  rw [if_neg (by decide : ¬ ((-3 : Int) < -3)), if_neg (by decide : ¬ ((-3 : Int) > -3))]
  rw [show ((false : Bool) == (false : Bool)) = true from rfl]
  simp only [if_true]
  rw [show toUInt128 (5000000000000003000 : UInt64) + toUInt128 (5000000000000002000 : UInt64)
      = (10000000000000005000 : UInt128) from by decide]
  rw [show (decide ((10000000000000005000 : UInt128) > toUInt128 largeRange.max) ||
           decide ((10000000000000005000 : UInt128) > toUInt128 maxRepUp)) = true from by decide]
  simp only [if_true]
  rw [show Guard.new.doDropDigit128 (10000000000000005000 : UInt128) (-3)
      = ({ digits_ := 0, xbit_ := false, sbit_ := false }, (1000000000000000500 : UInt128), (-2 : Int))
      from by unfold Guard.doDropDigit128 Guard.new Guard.push; rfl]
  simp only
  rw [show toUInt64 (1000000000000000500 : UInt128) = (1000000000000000500 : UInt64) from by decide]
  rw [show ({ digits_ := 0, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (1000000000000000500 : UInt64) (-2 : Int) largeRange.min largeRange.max mode
      "Number::addition overflow" =
      .ok { negative_ := false, mantissa_ := 1000000000000000500, exponent_ := -2 }
      from by unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl]
  simp only [RoundResult.toNumber]
  unfold Number.normalize doNormalize
  rw [show (((1000000000000000500 : UInt64)) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (1000000000000000500 : UInt64) (-2)
        = ((1000000000000000500 : UInt64), (-2 : Int)) by unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (1000000000000000500 : UInt64) (-2 : Int) Guard.new
        = .ok ((1000000000000000500 : UInt64), (-2 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((-2 : Int) < minExponent || (1000000000000000500 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (1000000000000000500 : UInt64) (-2 : Int) Guard.new
        = .ok ((1000000000000000500 : UInt64), (-2 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep; rw [if_neg (by decide)]]
  simp only []
  rw [show Guard.new.doRoundUp false (1000000000000000500 : UInt64) (-2 : Int)
      largeRange.min largeRange.max mode "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 1000000000000000500, exponent_ := -2 }
      from by unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.new; rfl]
  simp only [RoundResult.toNumber]


lemma ofnum_dir (mode : rounding_mode) (m16 : UInt64)
    (hm16 : m16 = (if mode = .upward then 1000000000000001 else 1000000000000000))
    (hmode : mode = .downward ∨ mode = .upward ∨ mode = .towards_zero) :
    IOUAmount.ofNumber ⟨false, 1000000000000000500, -2⟩ mode = .ok ⟨m16.toInt64, 1⟩ := by
  obtain ⟨k, hnorm, _, _, htz, hdown, hup, _⟩ :=
    normalizeToRange_16_per_mode ⟨false, 1000000000000000500, -2⟩ mode (by decide) (by decide) (by decide) (by decide)
  have hk : k = m16 := by
    rcases hmode with h | h | h <;> subst h <;> rw [hm16]
    · have := hdown rfl; apply UInt64.toNat_inj.mp; simpa using this
    · have := hup rfl; apply UInt64.toNat_inj.mp; simpa using this
    · have := htz rfl; apply UInt64.toNat_inj.mp; simpa using this
  subst hk
  unfold IOUAmount.ofNumber IOUAmount.fromNumber
  rw [show (⟨false, 1000000000000000500, -2⟩ : Number).normalizeToRange cMinValue cMaxValue mode
        = .ok (k.toInt64, 1) by rw [hnorm]; norm_num]
  simp only []
  rw [if_neg (by decide : ¬ ((1 : Int) > cMaxOffset)), if_neg (by decide : ¬ ((1 : Int) < cMinOffset))]

lemma operator_add_dir_eq (mode : rounding_mode) (sumI : IOUAmount)
    (hofn : IOUAmount.ofNumber ⟨false, 1000000000000000500, -2⟩ mode = .ok sumI) :
    STAmount.operator_add ⟨.issue noIssue, 5000000000000003, 0, false⟩
        ⟨.issue noIssue, 5000000000000002, 0, false⟩ mode
      = STAmount.ofIOUAmount sumI noIssue mode := by
  have hc1 : (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have hc2 : (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have hiou1 := STAmount.iou_canonical_id ⟨.issue noIssue, 5000000000000003, 0, false⟩ mode hc1
  have hiou2 := STAmount.iou_canonical_id ⟨.issue noIssue, 5000000000000002, 0, false⟩ mode hc2
  have htn1 : (⟨(⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).signedDrops.toInt64,
      (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).toNumber mode
      = .ok ⟨false, 5000000000000003000, -3⟩ :=
    STAmount.iou_toNumber_canonical ⟨.issue noIssue, 5000000000000003, 0, false⟩ mode hc1
  have htn2 : (⟨(⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).signedDrops.toInt64,
      (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).toNumber mode
      = .ok ⟨false, 5000000000000002000, -3⟩ :=
    STAmount.iou_toNumber_canonical ⟨.issue noIssue, 5000000000000002, 0, false⟩ mode hc2
  have hadd_inner : IOUAmount.operator_add
      ⟨(⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).signedDrops.toInt64,
        (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mOffset⟩
      ⟨(⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).signedDrops.toInt64,
        (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mOffset⟩ mode = .ok sumI := by
    unfold IOUAmount.operator_add
    rw [if_neg (by decide : ¬ (((⟨(⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).signedDrops.toInt64,
          (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).mantissa_ == 0) = true)),
        if_neg (by decide : ¬ (((⟨(⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).signedDrops.toInt64,
          (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).mantissa_ == 0) = true))]
    rw [htn1]; simp only []
    rw [htn2]; simp only []
    rw [add_lift_trace_g mode]; simp only []
    exact hofn
  unfold STAmount.operator_add
  rw [if_neg (by decide : ¬ ((!STAmount.areComparable ⟨.issue noIssue, 5000000000000003, 0, false⟩
        ⟨.issue noIssue, 5000000000000002, 0, false⟩) = true)),
      if_neg (by decide : ¬ (((⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mValue == 0) = true)),
      if_neg (by decide : ¬ (((⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mValue == 0) = true))]
  simp only []
  rw [if_neg (by decide : ¬ ((noIssue.isXRP) = true))]
  rw [hiou1]; simp only []
  rw [hiou2]; simp only []
  rw [hadd_inner]

-- explicit per-mode add witnesses (downward/towards_zero → m16=10^15; upward → 10^15+1; all error 5)
lemma add_dir_wit_core (mode : rounding_mode) (m16 : UInt64)
    (hm16 : m16 = (if mode = .upward then 1000000000000001 else 1000000000000000))
    (hmode : mode = .downward ∨ mode = .upward ∨ mode = .towards_zero) :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat + v2.toRat ≠ 0 ∧
      STAmount.operator_add v1 v2 mode = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat + v2.toRat) (4 / 10 ^ 16 : ℚ) := by
  have hmv : m16.toNat = (if mode = .upward then 1000000000000001 else 1000000000000000) := by
    rw [hm16]; split <;> decide
  have hofn := ofnum_dir mode m16 hm16 hmode
  have hnat : (m16.toInt64).toInt.natAbs = m16.toNat := by rw [hm16]; split <;> decide
  have hsr : (⟨m16.toInt64, 1⟩ : IOUAmount).InRange16 :=
    ⟨by show 10 ^ 15 ≤ (m16.toInt64).toInt.natAbs; rw [hnat, hmv]; split <;> decide,
     by show (m16.toInt64).toInt.natAbs < 10 ^ 16; rw [hnat, hmv]; split <;> decide,
     by show (-96 : ℤ) ≤ 1; decide, by show (1 : ℤ) ≤ 80; decide⟩
  have hop := operator_add_dir_eq mode ⟨m16.toInt64, 1⟩ hofn
  rw [STAmount.ofIOUAmount_canonical ⟨m16.toInt64, 1⟩ noIssue mode (by decide) hsr] at hop
  have hmv_uint : (m16.toInt64).toInt.natAbs.toUInt64 = m16 := by rw [hm16]; split <;> decide
  have hneg : decide ((m16.toInt64) < 0) = false := by rw [hm16]; split <;> decide
  rw [hmv_uint, hneg] at hop
  have h1 : (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).toRat = 5000000000000003 := by
    rw [STAmount.toRat_signed]; norm_num [show ((5000000000000003 : UInt64).toNat : ℚ) = 5000000000000003 by norm_cast]
  have h2 : (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).toRat = 5000000000000002 := by
    rw [STAmount.toRat_signed]; norm_num [show ((5000000000000002 : UInt64).toNat : ℚ) = 5000000000000002 by norm_cast]
  have hr : (⟨.issue noIssue, m16, 1, false⟩ : STAmount).toRat = 10 * (m16.toNat : ℚ) := by
    rw [STAmount.toRat_signed]; push_cast; ring
  refine ⟨⟨.issue noIssue, 5000000000000003, 0, false⟩, ⟨.issue noIssue, 5000000000000002, 0, false⟩,
          ⟨.issue noIssue, m16, 1, false⟩, noIssue, rfl, rfl, by decide,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩, ?_, hop, ?_, ?_⟩
  · rw [h1, h2]; norm_num
  · show m16 ≠ 0; rw [hm16]; split <;> decide
  · unfold RoundsWithinWitness
    rw [show RatValued.toRat (⟨.issue noIssue, m16, 1, false⟩ : STAmount)
          = (⟨.issue noIssue, m16, 1, false⟩ : STAmount).toRat from rfl, hr, h1, h2, hmv]
    split <;> norm_num

lemma mul_lift_trace_g (mode : rounding_mode) :
    Number.operator_mul ⟨false, 2000000000000001000, -3⟩ ⟨false, 5000000000000000000, -18⟩ mode
      = .ok ⟨false, 1000000000000000500, -2⟩ := by
  unfold Number.operator_mul
  have hx_ne : (⟨false, 2000000000000001000, -3⟩ : Number).operator_eq Number.zero = false := by decide
  have hy_ne : (⟨false, 5000000000000000000, -18⟩ : Number).operator_eq Number.zero = false := by decide
  simp only [hx_ne, hy_ne, Bool.false_eq_true, if_false]
  rw [show ((false:Bool) != false) = false from by decide]
  rw [if_neg (show ¬ ((false:Bool) = true) by decide)]
  rw [show toUInt128 (2000000000000001000:UInt64) * toUInt128 (5000000000000000000:UInt64) = (10000000000000005000000000000000000000:UInt128) from by decide]
  rw [show (-3 : Int) + -18 = (-21:Int) from by decide]
  have hsd : scaleDown128 (10000000000000005000000000000000000000 : UInt128) (-21) Guard.new
      = ((1000000000000000500 : UInt64), (-2 : Int), Guard.new) := by
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (10000000000000005000000000000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (10000000000000005000000000000000000000 : UInt128) / 10 = (1000000000000000500000000000000000000 : UInt128) from by decide,
        show (-21 : Int) + 1 = (-20:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (1000000000000000500000000000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (1000000000000000500000000000000000000 : UInt128) / 10 = (100000000000000050000000000000000000 : UInt128) from by decide,
        show (-20 : Int) + 1 = (-19:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (100000000000000050000000000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (100000000000000050000000000000000000 : UInt128) / 10 = (10000000000000005000000000000000000 : UInt128) from by decide,
        show (-19 : Int) + 1 = (-18:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (10000000000000005000000000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (10000000000000005000000000000000000 : UInt128) / 10 = (1000000000000000500000000000000000 : UInt128) from by decide,
        show (-18 : Int) + 1 = (-17:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (1000000000000000500000000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (1000000000000000500000000000000000 : UInt128) / 10 = (100000000000000050000000000000000 : UInt128) from by decide,
        show (-17 : Int) + 1 = (-16:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (100000000000000050000000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (100000000000000050000000000000000 : UInt128) / 10 = (10000000000000005000000000000000 : UInt128) from by decide,
        show (-16 : Int) + 1 = (-15:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (10000000000000005000000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (10000000000000005000000000000000 : UInt128) / 10 = (1000000000000000500000000000000 : UInt128) from by decide,
        show (-15 : Int) + 1 = (-14:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (1000000000000000500000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (1000000000000000500000000000000 : UInt128) / 10 = (100000000000000050000000000000 : UInt128) from by decide,
        show (-14 : Int) + 1 = (-13:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (100000000000000050000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (100000000000000050000000000000 : UInt128) / 10 = (10000000000000005000000000000 : UInt128) from by decide,
        show (-13 : Int) + 1 = (-12:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (10000000000000005000000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (10000000000000005000000000000 : UInt128) / 10 = (1000000000000000500000000000 : UInt128) from by decide,
        show (-12 : Int) + 1 = (-11:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (1000000000000000500000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (1000000000000000500000000000 : UInt128) / 10 = (100000000000000050000000000 : UInt128) from by decide,
        show (-11 : Int) + 1 = (-10:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (100000000000000050000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (100000000000000050000000000 : UInt128) / 10 = (10000000000000005000000000 : UInt128) from by decide,
        show (-10 : Int) + 1 = (-9:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (10000000000000005000000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (10000000000000005000000000 : UInt128) / 10 = (1000000000000000500000000 : UInt128) from by decide,
        show (-9 : Int) + 1 = (-8:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (1000000000000000500000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (1000000000000000500000000 : UInt128) / 10 = (100000000000000050000000 : UInt128) from by decide,
        show (-8 : Int) + 1 = (-7:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (100000000000000050000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (100000000000000050000000 : UInt128) / 10 = (10000000000000005000000 : UInt128) from by decide,
        show (-7 : Int) + 1 = (-6:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (10000000000000005000000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (10000000000000005000000 : UInt128) / 10 = (1000000000000000500000 : UInt128) from by decide,
        show (-6 : Int) + 1 = (-5:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (1000000000000000500000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (1000000000000000500000 : UInt128) / 10 = (100000000000000050000 : UInt128) from by decide,
        show (-5 : Int) + 1 = (-4:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (100000000000000050000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (100000000000000050000 : UInt128) / 10 = (10000000000000005000 : UInt128) from by decide,
        show (-4 : Int) + 1 = (-3:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_pos (by decide : (10000000000000005000 : UInt128) > toUInt128 maxRepUp)]
    simp only []
    rw [show (10000000000000005000 : UInt128) / 10 = (1000000000000000500 : UInt128) from by decide,
        show (-3 : Int) + 1 = (-2:Int) from by decide]
    conv_lhs => rw [scaleDown128]
    rw [dif_neg (by decide : ¬ ((1000000000000000500 : UInt128) > toUInt128 maxRepUp))]
    rfl
  rw [hsd]
  have h_rup : Guard.new.doRoundUp
      false (1000000000000000500 : UInt64) (-2 : Int) largeRange.min largeRange.max mode
      "Number::multiplication overflow" =
      .ok { negative_ := false, mantissa_ := 1000000000000000500, exponent_ := -2 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.new; rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize doNormalize
  rw [show (((1000000000000000500 : UInt64)) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (1000000000000000500 : UInt64) (-2)
        = ((1000000000000000500 : UInt64), (-2 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (1000000000000000500 : UInt64) (-2 : Int) Guard.new
        = .ok ((1000000000000000500 : UInt64), (-2 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((-2 : Int) < minExponent || (1000000000000000500 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (1000000000000000500 : UInt64) (-2 : Int) Guard.new
        = .ok ((1000000000000000500 : UInt64), (-2 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep; rw [if_neg (by decide)]]
  simp only []
  have h_rup2 : Guard.new.doRoundUp false (1000000000000000500 : UInt64) (-2 : Int)
      largeRange.min largeRange.max mode "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 1000000000000000500, exponent_ := -2 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.new; rfl
  rw [h_rup2]
  simp only [RoundResult.toNumber]

lemma ofnum_st_dir (mode : rounding_mode) (m16 : UInt64)
    (hm16 : m16 = (if mode = .upward then 1000000000000001 else 1000000000000000))
    (hmode : mode = .downward ∨ mode = .upward ∨ mode = .towards_zero) :
    STAmount.ofNumber (.issue noIssue) ⟨false, 1000000000000000500, -2⟩ mode
      = .ok ⟨.issue noIssue, m16, 1, false⟩ := by
  obtain ⟨k, hnorm, _, _, htz, hdown, hup, _⟩ :=
    normalizeToRange_16_per_mode ⟨false, 1000000000000000500, -2⟩ mode (by decide) (by decide) (by decide) (by decide)
  have hk : k = m16 := by
    rcases hmode with h | h | h <;> subst h <;> rw [hm16]
    · apply UInt64.toNat_inj.mp; simpa using hdown rfl
    · apply UInt64.toNat_inj.mp; simpa using hup rfl
    · apply UInt64.toNat_inj.mp; simpa using htz rfl
  subst hk
  have hktu : (k.toInt64).toUInt64 = k := by rcases hmode with h | h | h <;> subst h <;> simp only [hm16] <;> decide
  have hcr : (⟨.issue noIssue, (k.toInt64).toUInt64, 1, false⟩ : STAmount).IOUCanonical := by
    rw [hktu]; rcases hmode with h | h | h <;> subst h <;> simp only [hm16] <;>
      exact ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  unfold STAmount.ofNumber
  simp only []
  rw [show decide ((⟨false, 1000000000000000500, -2⟩ : Number).signum < 0) = false from by decide]
  rw [if_neg (show ¬ ((Asset.issue noIssue).integral = true) by decide)]
  simp only [Bool.false_eq_true, if_false]
  rw [show kMinValue = cMinValue from rfl, show kMaxValue = cMaxValue from rfl]
  rw [show (⟨false, 1000000000000000500, -2⟩ : Number).normalizeToRange cMinValue cMaxValue mode
        = .ok (k.toInt64, 1) by rw [hnorm]; norm_num]
  simp only []
  rw [STAmount.checked,
      show STAmount.unchecked (.issue noIssue) ((k.toInt64).toUInt64) 1 false
        = (⟨.issue noIssue, (k.toInt64).toUInt64, 1, false⟩ : STAmount) from rfl,
      STAmount.canonicalize_canonical_id _ mode hcr, hktu]

lemma operator_mul_dir_eq (mode : rounding_mode) :
    STAmount.multiply ⟨.issue noIssue, 2000000000000001, 0, false⟩
        ⟨.issue noIssue, 5000000000000000, -15, false⟩ (.issue noIssue) mode
      = STAmount.ofNumber (.issue noIssue) ⟨false, 1000000000000000500, -2⟩ mode := by
  have hc1 : (⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have hc2 : (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have htn1 : (⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).toNumber mode
      = .ok ⟨false, 2000000000000001000, -3⟩ :=
    STAmount.toNumber_iou_canonical ⟨.issue noIssue, 2000000000000001, 0, false⟩ noIssue mode rfl (by decide) hc1
  have htn2 : (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).toNumber mode
      = .ok ⟨false, 5000000000000000000, -18⟩ :=
    STAmount.toNumber_iou_canonical ⟨.issue noIssue, 5000000000000000, -15, false⟩ noIssue mode rfl (by decide) hc2
  unfold STAmount.multiply
  rw [if_neg (by decide : ¬ (((⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).isZero
        || (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).isZero) = true)),
      if_neg (by decide : ¬ (((⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).native
        && (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).native
        && (Asset.issue noIssue).isNative) = true)),
      if_neg (by decide : ¬ (((⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).holdsMPTIssue
        && (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).holdsMPTIssue
        && (Asset.issue noIssue).holdsMPTIssue) = true))]
  rw [htn1]; simp only []
  rw [htn2]; simp only []
  rw [mul_lift_trace_g mode]

theorem mul_dir_wit_core (mode : rounding_mode) (m16 : UInt64)
    (hm16 : m16 = (if mode = .upward then 1000000000000001 else 1000000000000000))
    (hmode : mode = .downward ∨ mode = .upward ∨ mode = .towards_zero) :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      (Asset.issue iss).holdsIssue = true ∧ (Asset.issue iss).isNative = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧
      STAmount.multiply v1 v2 (.issue iss) mode = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat * v2.toRat) (4 / 10 ^ 16 : ℚ) := by
  have hmv : m16.toNat = (if mode = .upward then 1000000000000001 else 1000000000000000) := by
    rw [hm16]; split <;> decide
  have hop : STAmount.multiply ⟨.issue noIssue, 2000000000000001, 0, false⟩
      ⟨.issue noIssue, 5000000000000000, -15, false⟩ (.issue noIssue) mode = .ok ⟨.issue noIssue, m16, 1, false⟩ := by
    rw [operator_mul_dir_eq mode, ofnum_st_dir mode m16 hm16 hmode]
  have h1 : (⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).toRat = 2000000000000001 := by
    rw [STAmount.toRat_signed]; norm_num [show ((2000000000000001 : UInt64).toNat : ℚ) = 2000000000000001 by norm_cast]
  have h2 : (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).toRat = 5 := by
    rw [STAmount.toRat_signed]; norm_num [show ((5000000000000000 : UInt64).toNat : ℚ) = 5000000000000000 by norm_cast]
  have hr : (⟨.issue noIssue, m16, 1, false⟩ : STAmount).toRat = 10 * (m16.toNat : ℚ) := by
    rw [STAmount.toRat_signed]; push_cast; ring
  refine ⟨⟨.issue noIssue, 2000000000000001, 0, false⟩, ⟨.issue noIssue, 5000000000000000, -15, false⟩,
          ⟨.issue noIssue, m16, 1, false⟩, noIssue, rfl, rfl, by decide, by decide, by decide,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩, hop, ?_, ?_⟩
  · show m16 ≠ 0; rw [hm16]; split <;> decide
  · unfold RoundsWithinWitness
    rw [show RatValued.toRat (⟨.issue noIssue, m16, 1, false⟩ : STAmount)
          = (⟨.issue noIssue, m16, 1, false⟩ : STAmount).toRat from rfl, hr, h1, h2, hmv]
    split <;> norm_num

/-- Directed-mode IOU subtraction witness core: `v1 − v2 = v1 + (−v2)` with `−v2` the
addition witness operand, so the addition chain applies. -/
theorem sub_dir_wit_core (mode : rounding_mode) (m16 : UInt64)
    (hm16 : m16 = (if mode = .upward then 1000000000000001 else 1000000000000000))
    (hmode : mode = .downward ∨ mode = .upward ∨ mode = .towards_zero) :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat - v2.toRat ≠ 0 ∧
      STAmount.operator_sub v1 v2 mode = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat - v2.toRat) (4 / 10 ^ 16 : ℚ) := by
  have hmv : m16.toNat = (if mode = .upward then 1000000000000001 else 1000000000000000) := by
    rw [hm16]; split <;> decide
  have hofn := ofnum_dir mode m16 hm16 hmode
  have hnat : (m16.toInt64).toInt.natAbs = m16.toNat := by rw [hm16]; split <;> decide
  have hsr : (⟨m16.toInt64, 1⟩ : IOUAmount).InRange16 :=
    ⟨by show 10 ^ 15 ≤ (m16.toInt64).toInt.natAbs; rw [hnat, hmv]; split <;> decide,
     by show (m16.toInt64).toInt.natAbs < 10 ^ 16; rw [hnat, hmv]; split <;> decide,
     by show (-96 : ℤ) ≤ 1; decide, by show (1 : ℤ) ≤ 80; decide⟩
  -- operator_sub v1 v2 = operator_add v1 (−v2); −v2 is the addition witness operand
  have hop : STAmount.operator_sub ⟨.issue noIssue, 5000000000000003, 0, false⟩
      ⟨.issue noIssue, 5000000000000002, 0, true⟩ mode
      = .ok ⟨.issue noIssue, (m16.toInt64).toInt.natAbs.toUInt64, 1, decide ((m16.toInt64) < 0)⟩ := by
    have hadd := operator_add_dir_eq mode ⟨m16.toInt64, 1⟩ hofn
    rw [STAmount.ofIOUAmount_canonical ⟨m16.toInt64, 1⟩ noIssue mode (by decide) hsr] at hadd
    exact hadd
  rw [show (m16.toInt64).toInt.natAbs.toUInt64 = m16 by rw [hm16]; split <;> decide,
      show decide ((m16.toInt64) < 0) = false by rw [hm16]; split <;> decide] at hop
  have h1 : (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).toRat = 5000000000000003 := by
    rw [STAmount.toRat_signed]; norm_num [show ((5000000000000003 : UInt64).toNat : ℚ) = 5000000000000003 by norm_cast]
  have h2 : (⟨.issue noIssue, 5000000000000002, 0, true⟩ : STAmount).toRat = -5000000000000002 := by
    rw [STAmount.toRat_signed]; norm_num [show ((5000000000000002 : UInt64).toNat : ℚ) = 5000000000000002 by norm_cast]
  have hr : (⟨.issue noIssue, m16, 1, false⟩ : STAmount).toRat = 10 * (m16.toNat : ℚ) := by
    rw [STAmount.toRat_signed]; push_cast; ring
  refine ⟨⟨.issue noIssue, 5000000000000003, 0, false⟩, ⟨.issue noIssue, 5000000000000002, 0, true⟩,
          ⟨.issue noIssue, m16, 1, false⟩, noIssue, rfl, rfl, by decide,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩, ?_, hop, ?_, ?_⟩
  · rw [h1, h2]; norm_num
  · show m16 ≠ 0; rw [hm16]; split <;> decide
  · unfold RoundsWithinWitness
    rw [show RatValued.toRat (⟨.issue noIssue, m16, 1, false⟩ : STAmount)
          = (⟨.issue noIssue, m16, 1, false⟩ : STAmount).toRat from rfl, hr, h1, h2, hmv]
    split <;> norm_num

end XRPL.Model.Protocol

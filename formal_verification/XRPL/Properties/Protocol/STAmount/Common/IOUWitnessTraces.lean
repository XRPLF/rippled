import XRPL.Properties.Protocol.STAmount.Add.Common.IOU
import XRPL.Properties.Protocol.STAmount.Mul.Common.IOU

/-! # Symbolic computation traces for the IOU tightness witnesses

The IOU `RoundsWithin` headlines (`operator_{add,sub,mul}_rounds_iou`) bound the
relative error by `≈10⁻¹⁵`. The companion `…_witness` theorems exhibit concrete
canonical operands whose error exceeds `|truth|·4/10¹⁶`, a half-ULP re-rounding
case, proving the bound's numerator is optimal up to a small constant.

Establishing `operator_* … = .ok result` for the concrete witnesses requires
evaluating the model's well-founded recursions (`Number.operator_add/_mul`,
`normalizeToRange`), which `rfl`/`decide` cannot reduce. This file provides those
evaluations as `unfold`+`decide` step traces (std-axioms, no `native_decide`):

* `add_lift_trace` / `mul_lift_trace`: the inner `Number` op on the 19-digit lifts.
* `reround_trace`: the 16-digit canonical re-round (`10¹⁶+5 → 10¹⁶`).
* `ofnum_trace` / `ofnum_st_trace`: `IOUAmount`/`STAmount` `ofNumber` wrappers.
* `operator_add_iou_witness_eq` / `multiply_iou_witness_eq`: the full `STAmount`
  operations on the witnesses, assembled from the above plus the canonical
  conversion lemmas. The witness theorems live next to their headlines. -/

namespace XRPL.Model.Protocol

/-- Inner `Number` addition of the two same-exponent 19-digit lifts
(`5·10¹⁵+3` and `5·10¹⁵+2`, each `×1000` then exponent `−3`): same-sign add,
overflow, scale-down by one digit (dropped digit `0`, no rounding). -/
lemma add_lift_trace :
    Number.operator_add ⟨false, 5000000000000003000, -3⟩ ⟨false, 5000000000000002000, -3⟩ .to_nearest
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
  have hsum : toUInt128 (5000000000000003000 : UInt64) + toUInt128 (5000000000000002000 : UInt64)
      = (10000000000000005000 : UInt128) := by decide
  rw [hsum]
  rw [show (decide ((10000000000000005000 : UInt128) > toUInt128 largeRange.max) ||
           decide ((10000000000000005000 : UInt128) > toUInt128 maxRepUp)) = true from by decide]
  simp only [if_true]
  have hdrop : Guard.new.doDropDigit128 (10000000000000005000 : UInt128) (-3)
      = ({ digits_ := 0, xbit_ := false, sbit_ := false }, (1000000000000000500 : UInt128), (-2 : Int)) := by
    unfold Guard.doDropDigit128 Guard.new Guard.push; rfl
  rw [hdrop]
  simp only
  have htoUInt64 : toUInt64 (1000000000000000500 : UInt128) = (1000000000000000500 : UInt64) := by decide
  rw [htoUInt64]
  have h_rup : ({ digits_ := 0, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (1000000000000000500 : UInt64) (-2 : Int) largeRange.min largeRange.max .to_nearest
      "Number::addition overflow" =
      .ok { negative_ := false, mantissa_ := 1000000000000000500, exponent_ := -2 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
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
      largeRange.min largeRange.max .to_nearest "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 1000000000000000500, exponent_ := -2 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
  rw [h_rup2]
  simp only [RoundResult.toNumber]

/-- The 16-digit canonical re-round of `10¹⁶+5` (`= 1000000000000000500·10⁻²`):
scale down three digits (dropping `500`), `to_nearest` ties to even → `10¹⁶`. -/
lemma reround_trace :
    (⟨false, 1000000000000000500, -2⟩ : Number).normalizeToRange cMinValue cMaxValue .to_nearest
      = .ok (1000000000000000, 1) := by
  unfold Number.normalizeToRange doNormalize
  rw [show (((1000000000000000500 : UInt64)) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp cMinValue (1000000000000000500 : UInt64) (-2)
        = ((1000000000000000500 : UInt64), (-2 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown cMaxValue (1000000000000000500 : UInt64) (-2 : Int) Guard.new
        = .ok ((1000000000000000 : UInt64), (1 : Int), (Guard.new.push 0 |>.push 0 |>.push 5)) by
        unfold doNormalize_scaleDown
        rw [dif_pos (by decide), if_neg (by decide)]
        rw [show (1000000000000000500 : UInt64) / 10 = 100000000000000050 from by decide,
            show (1000000000000000500 : UInt64) % 10 = 0 from by decide, show (-2 : Int) + 1 = -1 from by decide]
        unfold doNormalize_scaleDown
        rw [dif_pos (by decide), if_neg (by decide)]
        rw [show (100000000000000050 : UInt64) / 10 = 10000000000000005 from by decide,
            show (100000000000000050 : UInt64) % 10 = 0 from by decide, show (-1 : Int) + 1 = 0 from by decide]
        unfold doNormalize_scaleDown
        rw [dif_pos (by decide), if_neg (by decide)]
        rw [show (10000000000000005 : UInt64) / 10 = 1000000000000000 from by decide,
            show (10000000000000005 : UInt64) % 10 = 5 from by decide, show (0 : Int) + 1 = 1 from by decide]
        unfold doNormalize_scaleDown
        rw [dif_neg (by decide)]]
  simp only []
  rw [show ((1 : Int) < minExponent || (1000000000000000 : UInt64) < cMinValue) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (1000000000000000 : UInt64) (1 : Int) (Guard.new.push 0 |>.push 0 |>.push 5)
        = .ok ((1000000000000000 : UInt64), (1 : Int), (Guard.new.push 0 |>.push 0 |>.push 5)) from by
      unfold doNormalize_capAtMaxRep; rw [if_neg (by decide)]]
  simp only []
  rw [show (Guard.new.push 0 |>.push 0 |>.push 5 : Guard).doRoundUp false (1000000000000000 : UInt64) (1 : Int)
        cMinValue cMaxValue .to_nearest "Number::normalize 2"
        = .ok { negative_ := false, mantissa_ := 1000000000000000, exponent_ := 1 } from by
      unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.push Guard.new; rfl]
  simp only [RoundResult.toNumber]
  rw [if_neg (show ¬ (false = true) by decide)]
  rfl

/-- Inner `Number` multiplication of the two 19-digit lifts
(`(2·10¹⁵+1)·1000·10⁻³` and `5·10¹⁵·1000·10⁻¹⁸`): the `10³⁷`-scale product
scales down 19 zero digits to `1000000000000000500·10⁻²`, identical to the add case. -/
lemma mul_lift_trace :
    Number.operator_mul ⟨false, 2000000000000001000, -3⟩ ⟨false, 5000000000000000000, -18⟩ .to_nearest
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
      false (1000000000000000500 : UInt64) (-2 : Int) largeRange.min largeRange.max .to_nearest
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
      largeRange.min largeRange.max .to_nearest "Number::normalize 2" =
      .ok { negative_ := false, mantissa_ := 1000000000000000500, exponent_ := -2 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit Guard.new; rfl
  rw [h_rup2]
  simp only [RoundResult.toNumber]

/-- `IOUAmount.ofNumber` re-round of the add result. -/
lemma ofnum_trace :
    IOUAmount.ofNumber ⟨false, 1000000000000000500, -2⟩ .to_nearest = .ok ⟨1000000000000000, 1⟩ := by
  unfold IOUAmount.ofNumber IOUAmount.fromNumber
  rw [reround_trace]
  simp only []
  rw [if_neg (by decide : ¬ ((1 : Int) > cMaxOffset)), if_neg (by decide : ¬ ((1 : Int) < cMinOffset))]

/-- `STAmount.ofNumber` re-round of the mul result, via the canonical-`checked` identity. -/
lemma ofnum_st_trace :
    STAmount.ofNumber (.issue noIssue) ⟨false, 1000000000000000500, -2⟩ .to_nearest
      = .ok ⟨.issue noIssue, 1000000000000000, 1, false⟩ := by
  have hc_result : (⟨.issue noIssue, (1000000000000000:Int64).toUInt64, 1, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  unfold STAmount.ofNumber
  simp only []
  rw [show decide ((⟨false, 1000000000000000500, -2⟩ : Number).signum < 0) = false from by decide]
  rw [if_neg (show ¬ ((Asset.issue noIssue).integral = true) by decide)]
  simp only [Bool.false_eq_true, if_false]
  rw [show kMinValue = cMinValue from rfl, show kMaxValue = cMaxValue from rfl, reround_trace]
  simp only []
  rw [STAmount.checked,
      show STAmount.unchecked (.issue noIssue) ((1000000000000000:Int64).toUInt64) 1 false
        = (⟨.issue noIssue, (1000000000000000:Int64).toUInt64, 1, false⟩ : STAmount) from rfl,
      STAmount.canonicalize_canonical_id _ .to_nearest hc_result]
  rfl

/-- **Full `STAmount.operator_add` evaluation on the add/sub witness operands.** -/
theorem STAmount.operator_add_iou_witness_eq :
    STAmount.operator_add ⟨.issue noIssue, 5000000000000003, 0, false⟩
        ⟨.issue noIssue, 5000000000000002, 0, false⟩ .to_nearest
      = .ok ⟨.issue noIssue, 1000000000000000, 1, false⟩ := by
  have hc1 : (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have hc2 : (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have hiou1 := STAmount.iou_canonical_id ⟨.issue noIssue, 5000000000000003, 0, false⟩ .to_nearest hc1
  have hiou2 := STAmount.iou_canonical_id ⟨.issue noIssue, 5000000000000002, 0, false⟩ .to_nearest hc2
  have htn1 : (⟨(⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).signedDrops.toInt64,
      (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).toNumber .to_nearest
      = .ok ⟨false, 5000000000000003000, -3⟩ :=
    STAmount.iou_toNumber_canonical ⟨.issue noIssue, 5000000000000003, 0, false⟩ .to_nearest hc1
  have htn2 : (⟨(⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).signedDrops.toInt64,
      (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).toNumber .to_nearest
      = .ok ⟨false, 5000000000000002000, -3⟩ :=
    STAmount.iou_toNumber_canonical ⟨.issue noIssue, 5000000000000002, 0, false⟩ .to_nearest hc2
  have hsumI_range : (⟨1000000000000000, 1⟩ : IOUAmount).InRange16 :=
    ⟨by decide, by decide, by decide, by decide⟩
  have hofiou := STAmount.ofIOUAmount_canonical ⟨1000000000000000, 1⟩ noIssue .to_nearest (by decide) hsumI_range
  have hadd_inner : IOUAmount.operator_add
      ⟨(⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).signedDrops.toInt64,
        (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mOffset⟩
      ⟨(⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).signedDrops.toInt64,
        (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mOffset⟩ .to_nearest
      = .ok ⟨1000000000000000, 1⟩ := by
    unfold IOUAmount.operator_add
    rw [if_neg (by decide : ¬ (((⟨(⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).signedDrops.toInt64,
          (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).mantissa_ == 0) = true)),
        if_neg (by decide : ¬ (((⟨(⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).signedDrops.toInt64,
          (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mOffset⟩ : IOUAmount).mantissa_ == 0) = true))]
    rw [htn1]; simp only []
    rw [htn2]; simp only []
    rw [add_lift_trace]; simp only []
    rw [ofnum_trace]
  unfold STAmount.operator_add
  rw [if_neg (by decide : ¬ ((!STAmount.areComparable ⟨.issue noIssue, 5000000000000003, 0, false⟩
        ⟨.issue noIssue, 5000000000000002, 0, false⟩) = true)),
      if_neg (by decide : ¬ (((⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).mValue == 0) = true)),
      if_neg (by decide : ¬ (((⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).mValue == 0) = true))]
  simp only []
  rw [if_neg (by decide : ¬ ((noIssue.isXRP) = true))]
  rw [hiou1]; simp only []
  rw [hiou2]; simp only []
  rw [hadd_inner]; simp only []
  rw [hofiou]; rfl

/-- **Full `STAmount.multiply` evaluation on the mul witness operands.** -/
theorem STAmount.multiply_iou_witness_eq :
    STAmount.multiply ⟨.issue noIssue, 2000000000000001, 0, false⟩
        ⟨.issue noIssue, 5000000000000000, -15, false⟩ (.issue noIssue) .to_nearest
      = .ok ⟨.issue noIssue, 1000000000000000, 1, false⟩ := by
  have hc1 : (⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have hc2 : (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).IOUCanonical :=
    ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩
  have htn1 : (⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).toNumber .to_nearest
      = .ok ⟨false, 2000000000000001000, -3⟩ :=
    STAmount.toNumber_iou_canonical ⟨.issue noIssue, 2000000000000001, 0, false⟩ noIssue .to_nearest rfl (by decide) hc1
  have htn2 : (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).toNumber .to_nearest
      = .ok ⟨false, 5000000000000000000, -18⟩ :=
    STAmount.toNumber_iou_canonical ⟨.issue noIssue, 5000000000000000, -15, false⟩ noIssue .to_nearest rfl (by decide) hc2
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
  rw [mul_lift_trace]; simp only []
  rw [ofnum_st_trace]

end XRPL.Model.Protocol

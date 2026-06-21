import Mathlib.Tactic
import XRPL.Model.Protocol.XRPAmount

/-! # Proof bodies for the `XRPAmount.signum` correctness headlines.

`XRPAmount.signum` returns the sign of the value: `1` if positive, `-1` if negative, `0` if
zero. Since `toRat` is the integer cast of `drops_`, the sign tests reduce to integer sign
tests on `drops_.toInt`.  -/

namespace XRPL.Model.Protocol

/-- `signum` rewritten with its sign tests on `drops_.toInt`. -/
private lemma XRPAmount.signum_eq_int (x : XRPAmount) :
    x.signum = if x.drops_.toInt < 0 then -1 else if 0 < x.drops_.toInt then 1 else 0 := by
  have hz : ((0 : Int64)).toInt = 0 := by decide
  have hneg : (x.drops_ < 0) ↔ (x.drops_.toInt < 0) := by rw [Int64.lt_iff_toInt_lt, hz]
  have hgt : (x.drops_ > 0) ↔ (0 < x.drops_.toInt) := by rw [gt_iff_lt, Int64.lt_iff_toInt_lt, hz]
  unfold XRPAmount.signum
  by_cases h1 : x.drops_ < 0
  · rw [if_pos h1, if_pos (hneg.mp h1)]
  · rw [if_neg h1, if_neg (fun h => h1 (hneg.mpr h))]
    by_cases h2 : x.drops_ > 0
    · rw [if_pos h2, if_pos (hgt.mp h2)]
    · rw [if_neg h2, if_neg (fun h => h2 (hgt.mpr h))]

/-- **`signum` returns the sign of `toRat`.** -/
theorem XRPAmount.signum_eq_proof (x : XRPAmount) :
    x.signum = if 0 < x.toRat then 1 else if x.toRat < 0 then -1 else 0 := by
  have hpos : (0 < x.toRat) ↔ (0 < x.drops_.toInt) := by unfold XRPAmount.toRat; exact Int.cast_pos
  have hnegR : (x.toRat < 0) ↔ (x.drops_.toInt < 0) := by unfold XRPAmount.toRat; exact Int.cast_lt_zero
  rw [XRPAmount.signum_eq_int]
  by_cases h1 : x.drops_.toInt < 0
  · rw [if_pos h1, if_neg (fun h => absurd (hpos.mp h) (by omega)), if_pos (hnegR.mpr h1)]
  · rw [if_neg h1]
    by_cases h2 : 0 < x.drops_.toInt
    · rw [if_pos h2, if_pos (hpos.mpr h2)]
    · rw [if_neg h2, if_neg (fun h => h2 (hpos.mp h)), if_neg (fun h => h1 (hnegR.mp h))]

/-- **`signum = 1 ↔ value is positive`.** -/
theorem XRPAmount.signum_eq_one_iff_proof (x : XRPAmount) : x.signum = 1 ↔ 0 < x.toRat := by
  rw [XRPAmount.signum_eq_int,
      show (0 < x.toRat) ↔ (0 < x.drops_.toInt) from by unfold XRPAmount.toRat; exact Int.cast_pos]
  constructor <;> intro h <;> split_ifs at h ⊢ <;> omega

/-- **`signum = -1 ↔ value is negative`.** -/
theorem XRPAmount.signum_eq_neg_one_iff_proof (x : XRPAmount) : x.signum = -1 ↔ x.toRat < 0 := by
  rw [XRPAmount.signum_eq_int,
      show (x.toRat < 0) ↔ (x.drops_.toInt < 0) from by unfold XRPAmount.toRat; exact Int.cast_lt_zero]
  constructor <;> intro h <;> split_ifs at h ⊢ <;> omega

/-- **`signum = 0 ↔ value is zero`.** -/
theorem XRPAmount.signum_eq_zero_iff_proof (x : XRPAmount) : x.signum = 0 ↔ x.toRat = 0 := by
  rw [XRPAmount.signum_eq_int,
      show (x.toRat = 0) ↔ (x.drops_.toInt = 0) from by unfold XRPAmount.toRat; exact Int.cast_eq_zero]
  constructor <;> intro h <;> split_ifs at h ⊢ <;> omega

end XRPL.Model.Protocol

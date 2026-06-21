import Mathlib.Tactic
import XRPL.Model.Protocol.XRPAmount


namespace XRPL.Model.Protocol

/-- Rational `<` on `toRat` is `Int64` `<` on `drops_`. -/
lemma XRPAmount.toRat_lt_iff (x y : XRPAmount) : x.toRat < y.toRat ↔ x.drops_ < y.drops_ := by
  unfold XRPAmount.toRat; rw [Int.cast_lt, ← Int64.lt_iff_toInt_lt]

/-- Rational `≤` on `toRat` is `Int64` `≤` on `drops_`. -/
lemma XRPAmount.toRat_le_iff (x y : XRPAmount) : x.toRat ≤ y.toRat ↔ x.drops_ ≤ y.drops_ := by
  unfold XRPAmount.toRat; rw [Int.cast_le, ← Int64.le_iff_toInt_le]

/-- `toRat` is injective (one-to-one function): equal values iff equal `drops_`. -/
lemma XRPAmount.toRat_inj (x y : XRPAmount) : x.toRat = y.toRat ↔ x.drops_ = y.drops_ := by
  unfold XRPAmount.toRat; rw [Int.cast_inj, Int64.toInt_inj]

/-- `toRat` equals an `Int64`'s cast iff `drops_` equals that `Int64`. -/
lemma XRPAmount.toRat_eq_int_iff (x : XRPAmount) (v : Int64) :
    x.toRat = (v.toInt : ℚ) ↔ x.drops_ = v := by
  unfold XRPAmount.toRat; rw [Int.cast_inj, Int64.toInt_inj]

end XRPL.Model.Protocol

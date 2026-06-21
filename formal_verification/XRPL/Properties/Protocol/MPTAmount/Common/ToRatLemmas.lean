import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount

/-! # `toRat` bridges for `MPTAmount` -/

namespace XRPL.Model.Protocol

/-- `toRat` order is the `Int64` `value_` order (`toRat` is the integer cast). -/
lemma MPTAmount.toRat_lt_iff (x y : MPTAmount) : x.toRat < y.toRat ↔ x.value_ < y.value_ := by
  unfold MPTAmount.toRat; rw [Int.cast_lt, ← Int64.lt_iff_toInt_lt]

lemma MPTAmount.toRat_le_iff (x y : MPTAmount) : x.toRat ≤ y.toRat ↔ x.value_ ≤ y.value_ := by
  unfold MPTAmount.toRat; rw [Int.cast_le, ← Int64.le_iff_toInt_le]

lemma MPTAmount.toRat_inj (x y : MPTAmount) : x.toRat = y.toRat ↔ x.value_ = y.value_ := by
  unfold MPTAmount.toRat; rw [Int.cast_inj, Int64.toInt_inj]

/-- `toRat` equals the cast of an `Int64` `v` iff `value_ = v`. -/
lemma MPTAmount.toRat_eq_int_iff (x : MPTAmount) (v : Int64) :
    x.toRat = (v.toInt : ℚ) ↔ x.value_ = v := by
  unfold MPTAmount.toRat; rw [Int.cast_inj, Int64.toInt_inj]

end XRPL.Model.Protocol

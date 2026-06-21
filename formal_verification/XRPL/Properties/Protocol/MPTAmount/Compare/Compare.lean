import XRPL.Properties.Protocol.MPTAmount.Compare.Common.Proofs

/-! # Correctness of the `MPTAmount` comparison operators

`MPTAmount` is a single signed `Int64` of token units, so every comparison decides the
rational order of `toRat` exactly and unconditionally. -/

namespace XRPL.Model.Protocol

/-- **`operator_lt` decides `<`.** -/
theorem MPTAmount.operator_lt_iff (x y : MPTAmount) :
    MPTAmount.operator_lt x y = true ↔ x.toRat < y.toRat :=
  MPTAmount.operator_lt_iff_proof x y

/-- **`operator_le` decides `≤`.** -/
theorem MPTAmount.operator_le_iff (x y : MPTAmount) :
    MPTAmount.operator_le x y = true ↔ x.toRat ≤ y.toRat :=
  MPTAmount.operator_le_iff_proof x y

/-- **`operator_gt` decides `>`.** -/
theorem MPTAmount.operator_gt_iff (x y : MPTAmount) :
    MPTAmount.operator_gt x y = true ↔ y.toRat < x.toRat :=
  MPTAmount.operator_gt_iff_proof x y

/-- **`operator_ge` decides `≥`.** -/
theorem MPTAmount.operator_ge_iff (x y : MPTAmount) :
    MPTAmount.operator_ge x y = true ↔ y.toRat ≤ x.toRat :=
  MPTAmount.operator_ge_iff_proof x y

/-- **`operator_eq` decides `=`.** -/
theorem MPTAmount.operator_eq_iff (x y : MPTAmount) :
    MPTAmount.operator_eq x y = true ↔ x.toRat = y.toRat :=
  MPTAmount.operator_eq_iff_proof x y

/-- **`operator_ne` decides `≠`.** -/
theorem MPTAmount.operator_ne_iff (x y : MPTAmount) :
    MPTAmount.operator_ne x y = true ↔ x.toRat ≠ y.toRat :=
  MPTAmount.operator_ne_iff_proof x y

/-- **`operator_eq_int` decides equality with an `Int64`.** -/
theorem MPTAmount.operator_eq_int_iff (x : MPTAmount) (v : Int64) :
    MPTAmount.operator_eq_int x v = true ↔ x.toRat = (v.toInt : ℚ) :=
  MPTAmount.operator_eq_int_iff_proof x v

/-- **`operator_ne_int` decides inequality with an `Int64`.** -/
theorem MPTAmount.operator_ne_int_iff (x : MPTAmount) (v : Int64) :
    MPTAmount.operator_ne_int x v = true ↔ x.toRat ≠ (v.toInt : ℚ) :=
  MPTAmount.operator_ne_int_iff_proof x v

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.XRPAmount.Compare.Common.Proofs

/-! # Correctness of the `XRPAmount` comparison operators

`XRPAmount` is a single signed `Int64` of drops, so every comparison decides the rational
order of `toRat` exactly and unconditionally. -/

namespace XRPL.Model.Protocol

/-- **`operator_lt` decides `<`.** -/
theorem XRPAmount.operator_lt_iff (x y : XRPAmount) :
    XRPAmount.operator_lt x y = true ↔ x.toRat < y.toRat :=
  XRPAmount.operator_lt_iff_proof x y

/-- **`operator_le` decides `≤`.** -/
theorem XRPAmount.operator_le_iff (x y : XRPAmount) :
    XRPAmount.operator_le x y = true ↔ x.toRat ≤ y.toRat :=
  XRPAmount.operator_le_iff_proof x y

/-- **`operator_gt` decides `>`.** -/
theorem XRPAmount.operator_gt_iff (x y : XRPAmount) :
    XRPAmount.operator_gt x y = true ↔ y.toRat < x.toRat :=
  XRPAmount.operator_gt_iff_proof x y

/-- **`operator_ge` decides `≥`.** -/
theorem XRPAmount.operator_ge_iff (x y : XRPAmount) :
    XRPAmount.operator_ge x y = true ↔ y.toRat ≤ x.toRat :=
  XRPAmount.operator_ge_iff_proof x y

/-- **`operator_eq` decides `=`.** -/
theorem XRPAmount.operator_eq_iff (x y : XRPAmount) :
    XRPAmount.operator_eq x y = true ↔ x.toRat = y.toRat :=
  XRPAmount.operator_eq_iff_proof x y

/-- **`operator_ne` decides `≠`.** -/
theorem XRPAmount.operator_ne_iff (x y : XRPAmount) :
    XRPAmount.operator_ne x y = true ↔ x.toRat ≠ y.toRat :=
  XRPAmount.operator_ne_iff_proof x y

/-- **`operator_eq_int` decides equality with an `Int64`.** -/
theorem XRPAmount.operator_eq_int_iff (x : XRPAmount) (v : Int64) :
    XRPAmount.operator_eq_int x v = true ↔ x.toRat = (v.toInt : ℚ) :=
  XRPAmount.operator_eq_int_iff_proof x v

/-- **`operator_ne_int` decides inequality with an `Int64`.** -/
theorem XRPAmount.operator_ne_int_iff (x : XRPAmount) (v : Int64) :
    XRPAmount.operator_ne_int x v = true ↔ x.toRat ≠ (v.toInt : ℚ) :=
  XRPAmount.operator_ne_int_iff_proof x v

end XRPL.Model.Protocol

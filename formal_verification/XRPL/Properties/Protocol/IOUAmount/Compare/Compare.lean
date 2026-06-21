import XRPL.Properties.Protocol.IOUAmount.Compare.Common.Proofs

/-! # Correctness of the `IOUAmount` comparison operators. -/

namespace XRPL.Model.Protocol

/-- **`operator_eq` decides equality.** -/
theorem IOUAmount.operator_eq_iff (x y : IOUAmount) :
    IOUAmount.operator_eq x y = true ↔ x = y :=
  IOUAmount.operator_eq_iff_proof x y

/-- **`operator_ne` decides inequality.** -/
theorem IOUAmount.operator_ne_iff (x y : IOUAmount) :
    IOUAmount.operator_ne x y = true ↔ x ≠ y :=
  IOUAmount.operator_ne_iff_proof x y

/-- **`operator_lt` decides `<`** -/
theorem IOUAmount.operator_lt_iff (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_lt x y mode = .ok b) :
    b = true ↔ x.toRat < y.toRat :=
  IOUAmount.operator_lt_iff_proof x y mode b hx hy h

/-- **`operator_le` decides `≤`** -/
theorem IOUAmount.operator_le_iff (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_le x y mode = .ok b) :
    b = true ↔ x.toRat ≤ y.toRat :=
  IOUAmount.operator_le_iff_proof x y mode b hx hy h

/-- **`operator_gt` decides `>`** -/
theorem IOUAmount.operator_gt_iff (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_gt x y mode = .ok b) :
    b = true ↔ y.toRat < x.toRat :=
  IOUAmount.operator_gt_iff_proof x y mode b hx hy h

/-- **`operator_ge` decides `≥`** -/
theorem IOUAmount.operator_ge_iff (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_ge x y mode = .ok b) :
    b = true ↔ y.toRat ≤ x.toRat :=
  IOUAmount.operator_ge_iff_proof x y mode b hx hy h

end XRPL.Model.Protocol

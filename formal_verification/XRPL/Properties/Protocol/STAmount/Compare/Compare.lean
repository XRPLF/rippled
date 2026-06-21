import XRPL.Properties.Protocol.STAmount.Compare.Common.Proofs

/-! # Correctness of the `STAmount` comparison operators -/

namespace XRPL.Model.Protocol

/-- **Correctness of `operator_lt`.** -/
theorem STAmount.operator_lt_eq (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_lt lhs rhs = .ok (decide (lhs.toRat < rhs.toRat)) :=
  STAmount.operator_lt_eq_proof lhs rhs h

/-- **Correctness of `operator_le`.** -/
theorem STAmount.operator_le_eq (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_le lhs rhs = .ok (decide (lhs.toRat ≤ rhs.toRat)) :=
  STAmount.operator_le_eq_proof lhs rhs h

/-- **Correctness of `operator_gt`.** -/
theorem STAmount.operator_gt_eq (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_gt lhs rhs = .ok (decide (rhs.toRat < lhs.toRat)) :=
  STAmount.operator_gt_eq_proof lhs rhs h

/-- **Correctness of `operator_ge`.** -/
theorem STAmount.operator_ge_eq (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_ge lhs rhs = .ok (decide (rhs.toRat ≤ lhs.toRat)) :=
  STAmount.operator_ge_eq_proof lhs rhs h

/-- **Correctness of `operator_eq`.** On comparable operands of the **same asset**, field
equality decides rational equality. -/
theorem STAmount.operator_eq_eq (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs)
    (hasset : lhs.mAsset = rhs.mAsset) :
    STAmount.operator_eq lhs rhs = decide (lhs.toRat = rhs.toRat) :=
  STAmount.operator_eq_eq_proof lhs rhs h hasset

/-- **Correctness of `operator_ne`.** -/
theorem STAmount.operator_ne_eq (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs)
    (hasset : lhs.mAsset = rhs.mAsset) :
    STAmount.operator_ne lhs rhs = decide (lhs.toRat ≠ rhs.toRat) :=
  STAmount.operator_ne_eq_proof lhs rhs h hasset

end XRPL.Model.Protocol

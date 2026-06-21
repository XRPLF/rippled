import Mathlib.Tactic
import XRPL.Model.Protocol.XRPAmount
import XRPL.Properties.Protocol.XRPAmount.Common.ToRatLemmas

/-! # Proof bodies for the `XRPAmount` comparison headlines (`Compare.Compare`).

Each operator unfolds to its `Int64` `drops_` comparison, which the `Common.ToRatLemmas`
bridges then identify with the `toRat` order. -/

namespace XRPL.Model.Protocol

/-- **Correctness of `operator_lt`.** -/
theorem XRPAmount.operator_lt_iff_proof (x y : XRPAmount) :
    XRPAmount.operator_lt x y = true ↔ x.toRat < y.toRat := by
  rw [XRPAmount.toRat_lt_iff]; unfold XRPAmount.operator_lt; exact decide_eq_true_iff

/-- **Correctness of `operator_le`.** -/
theorem XRPAmount.operator_le_iff_proof (x y : XRPAmount) :
    XRPAmount.operator_le x y = true ↔ x.toRat ≤ y.toRat := by
  rw [XRPAmount.toRat_le_iff]; unfold XRPAmount.operator_le; exact decide_eq_true_iff

/-- **Correctness of `operator_gt`.** -/
theorem XRPAmount.operator_gt_iff_proof (x y : XRPAmount) :
    XRPAmount.operator_gt x y = true ↔ y.toRat < x.toRat := by
  rw [XRPAmount.toRat_lt_iff]; unfold XRPAmount.operator_gt; exact decide_eq_true_iff

/-- **Correctness of `operator_ge`.** -/
theorem XRPAmount.operator_ge_iff_proof (x y : XRPAmount) :
    XRPAmount.operator_ge x y = true ↔ y.toRat ≤ x.toRat := by
  rw [XRPAmount.toRat_le_iff]; unfold XRPAmount.operator_ge; exact decide_eq_true_iff

/-- **Correctness of `operator_eq`.** -/
theorem XRPAmount.operator_eq_iff_proof (x y : XRPAmount) :
    XRPAmount.operator_eq x y = true ↔ x.toRat = y.toRat := by
  unfold XRPAmount.operator_eq; rw [beq_iff_eq]; exact (XRPAmount.toRat_inj x y).symm

/-- **Correctness of `operator_ne`.** -/
theorem XRPAmount.operator_ne_iff_proof (x y : XRPAmount) :
    XRPAmount.operator_ne x y = true ↔ x.toRat ≠ y.toRat := by
  unfold XRPAmount.operator_ne; rw [bne_iff_ne]; exact not_congr (XRPAmount.toRat_inj x y).symm

/-- **Correctness of `operator_eq_int`.** -/
theorem XRPAmount.operator_eq_int_iff_proof (x : XRPAmount) (v : Int64) :
    XRPAmount.operator_eq_int x v = true ↔ x.toRat = (v.toInt : ℚ) := by
  unfold XRPAmount.operator_eq_int; rw [beq_iff_eq]; exact (XRPAmount.toRat_eq_int_iff x v).symm

/-- **Correctness of `operator_ne_int`.** -/
theorem XRPAmount.operator_ne_int_iff_proof (x : XRPAmount) (v : Int64) :
    XRPAmount.operator_ne_int x v = true ↔ x.toRat ≠ (v.toInt : ℚ) := by
  unfold XRPAmount.operator_ne_int; rw [bne_iff_ne]
  exact not_congr (XRPAmount.toRat_eq_int_iff x v).symm

end XRPL.Model.Protocol

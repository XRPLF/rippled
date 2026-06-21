import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount
import XRPL.Properties.Protocol.MPTAmount.Common.ToRatLemmas

/-! # Proof bodies for the `MPTAmount` comparison-operator correctness headlines.

`MPTAmount` is a single signed `Int64` of token units, so every comparison is exact: it
agrees with the rational order of `toRat` unconditionally (via the `Common.ToRatLemmas`
bridges). The thin headlines live in `MPTAmount.Compare.Compare`. -/

namespace XRPL.Model.Protocol

/-- **Correctness of `operator_lt`.** -/
theorem MPTAmount.operator_lt_iff_proof (x y : MPTAmount) :
    MPTAmount.operator_lt x y = true ↔ x.toRat < y.toRat := by
  rw [MPTAmount.toRat_lt_iff]; unfold MPTAmount.operator_lt; exact decide_eq_true_iff

/-- **Correctness of `operator_le`.** -/
theorem MPTAmount.operator_le_iff_proof (x y : MPTAmount) :
    MPTAmount.operator_le x y = true ↔ x.toRat ≤ y.toRat := by
  rw [MPTAmount.toRat_le_iff]; unfold MPTAmount.operator_le; exact decide_eq_true_iff

/-- **Correctness of `operator_gt`.** -/
theorem MPTAmount.operator_gt_iff_proof (x y : MPTAmount) :
    MPTAmount.operator_gt x y = true ↔ y.toRat < x.toRat := by
  rw [MPTAmount.toRat_lt_iff]; unfold MPTAmount.operator_gt; exact decide_eq_true_iff

/-- **Correctness of `operator_ge`.** -/
theorem MPTAmount.operator_ge_iff_proof (x y : MPTAmount) :
    MPTAmount.operator_ge x y = true ↔ y.toRat ≤ x.toRat := by
  rw [MPTAmount.toRat_le_iff]; unfold MPTAmount.operator_ge; exact decide_eq_true_iff

/-- **Correctness of `operator_eq`.** -/
theorem MPTAmount.operator_eq_iff_proof (x y : MPTAmount) :
    MPTAmount.operator_eq x y = true ↔ x.toRat = y.toRat := by
  unfold MPTAmount.operator_eq; rw [beq_iff_eq]; exact (MPTAmount.toRat_inj x y).symm

/-- **Correctness of `operator_ne`.** -/
theorem MPTAmount.operator_ne_iff_proof (x y : MPTAmount) :
    MPTAmount.operator_ne x y = true ↔ x.toRat ≠ y.toRat := by
  unfold MPTAmount.operator_ne; rw [bne_iff_ne]; exact not_congr (MPTAmount.toRat_inj x y).symm

/-- **Correctness of `operator_eq_int`.** -/
theorem MPTAmount.operator_eq_int_iff_proof (x : MPTAmount) (v : Int64) :
    MPTAmount.operator_eq_int x v = true ↔ x.toRat = (v.toInt : ℚ) := by
  unfold MPTAmount.operator_eq_int; rw [beq_iff_eq]; exact (MPTAmount.toRat_eq_int_iff x v).symm

/-- **Correctness of `operator_ne_int`.** -/
theorem MPTAmount.operator_ne_int_iff_proof (x : MPTAmount) (v : Int64) :
    MPTAmount.operator_ne_int x v = true ↔ x.toRat ≠ (v.toInt : ℚ) := by
  unfold MPTAmount.operator_ne_int; rw [bne_iff_ne]
  exact not_congr (MPTAmount.toRat_eq_int_iff x v).symm

end XRPL.Model.Protocol

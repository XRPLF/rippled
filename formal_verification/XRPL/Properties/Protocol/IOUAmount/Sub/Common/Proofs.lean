import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.IOUAmount.Neg.Common.Proofs
import XRPL.Properties.Protocol.IOUAmount.Add.Common.Proofs

/-! # Proof bodies for the `IOUAmount` subtraction correctness headlines.

`operator_sub x y = operator_add x (-y)`: a successful subtraction reduces to an addition of
`x` and the (canonical, exact) negation of `y`, so the rounding bounds are inherited from
`Add.Common.Proofs` and `Neg.Common.Proofs`. The thin headlines live in
`IOUAmount.Sub.RoundsWithin`. -/

namespace XRPL.Model.Protocol

/-- `operator_sub x y = operator_add x (-y)`: reduce a successful subtraction to an
addition of `x` and the (canonical, exact) negation of `y`. -/
private lemma IOUAmount.sub_eq_add_neg (x y result : IOUAmount) (mode : rounding_mode)
    (hy : y.InRange16)
    (hok : IOUAmount.operator_sub x y mode = .ok result) :
    IOUAmount.operator_add x ⟨-y.mantissa_, y.exponent_⟩ mode = .ok result := by
  obtain ⟨ny, hny_ok, _, _⟩ := IOUAmount.operator_neg_exact_proof y mode hy
  have hny_eq : ny = ⟨-y.mantissa_, y.exponent_⟩ := by
    have := IOUAmount.normalize_canonical_id ⟨-y.mantissa_, y.exponent_⟩ mode
      (IOUAmount.neg_InRange16 y hy)
    rw [IOUAmount.operator_neg] at hny_ok
    rw [this] at hny_ok; exact (Except.ok.inj hny_ok).symm
  unfold IOUAmount.operator_sub at hok
  rw [hny_ok, hny_eq] at hok
  exact hok

/-- **`operator_sub` rounds within the IOU ULP bound (every mode).** -/
theorem IOUAmount.operator_sub_rounds_proof (x y result : IOUAmount) (mode : rounding_mode)
    (hx : x.InRange16) (hy : y.InRange16)
    (h_truth_ne : x.toRat - y.toRat ≠ 0)
    (hok : IOUAmount.operator_sub x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) mode IOUAmount.εDirected := by
  have hny : (⟨-y.mantissa_, y.exponent_⟩ : IOUAmount).InRange16 := IOUAmount.neg_InRange16 y hy
  have hny_val : (⟨-y.mantissa_, y.exponent_⟩ : IOUAmount).toRat = -y.toRat := by
    obtain ⟨r, hr_ok, hr_val, _⟩ := IOUAmount.operator_neg_exact_proof y mode hy
    have : r = ⟨-y.mantissa_, y.exponent_⟩ := by
      rw [IOUAmount.operator_neg,
        IOUAmount.normalize_canonical_id ⟨-y.mantissa_, y.exponent_⟩ mode hny] at hr_ok
      exact (Except.ok.inj hr_ok).symm
    rw [← this]; exact hr_val
  have hadd := IOUAmount.sub_eq_add_neg x y result mode hy hok
  have htr : x.toRat + (⟨-y.mantissa_, y.exponent_⟩ : IOUAmount).toRat = x.toRat - y.toRat := by
    rw [hny_val]; ring
  have key := IOUAmount.operator_add_rounds_proof x ⟨-y.mantissa_, y.exponent_⟩ result mode hx hny
    (by rw [htr]; exact h_truth_ne) hadd hresult
  rwa [htr] at key

/-- **`operator_sub` rounds within a half-ULP (relative) under `to_nearest`.** -/
theorem IOUAmount.operator_sub_rounds_to_nearest_proof (x y result : IOUAmount)
    (hx : x.InRange16) (hy : y.InRange16)
    (h_truth_ne : x.toRat - y.toRat ≠ 0)
    (hok : IOUAmount.operator_sub x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat - y.toRat)| ≤ |x.toRat - y.toRat| * IOUAmount.εToNearest := by
  have hny : (⟨-y.mantissa_, y.exponent_⟩ : IOUAmount).InRange16 := IOUAmount.neg_InRange16 y hy
  have hny_val : (⟨-y.mantissa_, y.exponent_⟩ : IOUAmount).toRat = -y.toRat := by
    obtain ⟨r, hr_ok, hr_val, _⟩ := IOUAmount.operator_neg_exact_proof y .to_nearest hy
    have : r = ⟨-y.mantissa_, y.exponent_⟩ := by
      rw [IOUAmount.operator_neg,
        IOUAmount.normalize_canonical_id ⟨-y.mantissa_, y.exponent_⟩ .to_nearest hny] at hr_ok
      exact (Except.ok.inj hr_ok).symm
    rw [← this]; exact hr_val
  have hadd := IOUAmount.sub_eq_add_neg x y result .to_nearest hy hok
  have htr : x.toRat + (⟨-y.mantissa_, y.exponent_⟩ : IOUAmount).toRat = x.toRat - y.toRat := by
    rw [hny_val]; ring
  have key := IOUAmount.operator_add_rounds_to_nearest_proof x ⟨-y.mantissa_, y.exponent_⟩ result hx hny
    (by rw [htr]; exact h_truth_ne) hadd hresult
  rwa [htr] at key

end XRPL.Model.Protocol

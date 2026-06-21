import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.Number.Compare.Compare
import XRPL.Properties.Protocol.IOUAmount.Common.Defs
import XRPL.Properties.Protocol.IOUAmount.ToNumber.ToNumber

/-! # Proof bodies for the `IOUAmount` comparison-operator correctness headlines.

`IOUAmount` is a `(mantissa, exponent)` decimal float. Equality is structural (compares
both fields). The ordering operators lift each operand into the `Number` layer via
`toNumber`, which is **value-exact** under `ToNumberExact` (mantissa `≠ Int64.minValue` with
exponent room; far weaker than canonical `InRange16`). So they decide the rational order of
`toRat`. The thin headlines live in `IOUAmount.Compare.Compare`. -/

namespace XRPL.Model.Protocol

/-- `operator_lt` succeeds on `ToNumberExact` inputs and decides `<` on `toRat`. -/
lemma IOUAmount.operator_lt_ok (x y : IOUAmount) (mode : rounding_mode)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact) :
    ∃ b, IOUAmount.operator_lt x y mode = .ok b ∧ (b = true ↔ x.toRat < y.toRat) := by
  obtain ⟨xn, hxok, hxval, hxnorm⟩ := IOUAmount.toNumber_exact x mode hx
  obtain ⟨yn, hyok, hyval, hynorm⟩ := IOUAmount.toNumber_exact y mode hy
  refine ⟨Number.operator_lt xn yn, ?_, ?_⟩
  · unfold IOUAmount.operator_lt; rw [hxok, hyok]
  · rw [operator_lt_iff xn yn hxnorm hynorm, hxval, hyval]

/-- **Correctness of `operator_lt`.** -/
theorem IOUAmount.operator_lt_iff_proof (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_lt x y mode = .ok b) :
    b = true ↔ x.toRat < y.toRat := by
  obtain ⟨b', hok, hiff⟩ := IOUAmount.operator_lt_ok x y mode hx hy
  rw [hok] at h; rw [(Except.ok.inj h).symm]; exact hiff

/-- **Correctness of `operator_gt`** (`gt x y = lt y x`). -/
theorem IOUAmount.operator_gt_iff_proof (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_gt x y mode = .ok b) :
    b = true ↔ y.toRat < x.toRat := by
  unfold IOUAmount.operator_gt at h
  exact IOUAmount.operator_lt_iff_proof y x mode b hy hx h

/-- **Correctness of `operator_le`** (`le x y = !(lt y x)`). -/
theorem IOUAmount.operator_le_iff_proof (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_le x y mode = .ok b) :
    b = true ↔ x.toRat ≤ y.toRat := by
  obtain ⟨lt0, hltok, hltiff⟩ := IOUAmount.operator_lt_ok y x mode hy hx
  unfold IOUAmount.operator_le at h
  simp only [hltok] at h
  rw [(Except.ok.inj h).symm, Bool.not_eq_true', ← Bool.not_eq_true, hltiff, not_lt]

/-- **Correctness of `operator_ge`** (`ge x y = !(lt x y)`). -/
theorem IOUAmount.operator_ge_iff_proof (x y : IOUAmount) (mode : rounding_mode) (b : Bool)
    (hx : x.ToNumberExact) (hy : y.ToNumberExact)
    (h : IOUAmount.operator_ge x y mode = .ok b) :
    b = true ↔ y.toRat ≤ x.toRat := by
  obtain ⟨lt0, hltok, hltiff⟩ := IOUAmount.operator_lt_ok x y mode hx hy
  unfold IOUAmount.operator_ge at h
  simp only [hltok] at h
  rw [(Except.ok.inj h).symm, Bool.not_eq_true', ← Bool.not_eq_true, hltiff, not_lt]

/-- **Correctness of `operator_eq`**. Structural equality of both fields. -/
theorem IOUAmount.operator_eq_iff_proof (x y : IOUAmount) :
    IOUAmount.operator_eq x y = true ↔ x = y := by
  unfold IOUAmount.operator_eq
  rw [Bool.and_eq_true, beq_iff_eq, beq_iff_eq]
  constructor
  · rintro ⟨he, hm⟩; obtain ⟨xm, xe⟩ := x; obtain ⟨ym, ye⟩ := y; simp_all
  · rintro rfl; exact ⟨rfl, rfl⟩

/-- **Correctness of `operator_ne`**. Structural inequality. -/
theorem IOUAmount.operator_ne_iff_proof (x y : IOUAmount) :
    IOUAmount.operator_ne x y = true ↔ x ≠ y := by
  unfold IOUAmount.operator_ne
  rw [ne_eq, ← IOUAmount.operator_eq_iff_proof x y]
  cases IOUAmount.operator_eq x y <;> simp

end XRPL.Model.Protocol

import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.STAmount.Common.RoundToScaleHelpers

/-! # Shared definitions for the `IOUAmount` proof tree

Mirrors `Number/Common/Defs.lean`: the type-specific predicates the per-area proofs build
on. `IOUAmount` is a `(mantissa, exponent)` decimal float that rounds through the `Number`
layer; the canonical-form predicate `InRange16` lives in the STAmount tree (shared), and
this file adds the comparison precondition `ToNumberExact` and the directed-rounding error
budget `εDirected`. -/

namespace XRPL.Model.Protocol

/-- Directed-mode IOU add/sub relative-error bound (`≈2·10⁻¹⁵`): the `Number` op
(`11/(2⁶³−18)`) composed with the 16-digit canonical re-round (`10⁻¹⁵`). -/
abbrev IOUAmount.εDirected : ℚ :=
  11 / (2 ^ 63 - 18 : ℚ) + (10 : ℚ) ^ (-15 : ℤ) + 11 / (2 ^ 63 - 18 : ℚ) * (10 : ℚ) ^ (-15 : ℤ)

/-- `to_nearest` IOU add/sub relative-error bound (`≈½·10⁻¹⁵`): the `Number` op
(`ε₁ = 6/(2⁶³−3)`) composed with the 16-digit canonical re-round (`ε₂ = ½·10⁻¹⁵`).
Both round-to-nearest (each a half-ULP), so the composed bound is `ε₁ + ε₂ + ε₁·ε₂`.
Half the directed `εDirected`. -/
abbrev IOUAmount.εToNearest : ℚ :=
  6 / (2 ^ 63 - 3 : ℚ) + (1 / 2) * (10 : ℚ) ^ (-15 : ℤ)
    + 6 / (2 ^ 63 - 3 : ℚ) * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ))

/-- Precondition under which `IOUAmount.toNumber` is value-exact, hence the ordering
operators are faithful: the mantissa is not the lone non-representable `Int64.minValue`,
and the exponent leaves room for the `largeRange` scale-up. Far weaker than `InRange16`. -/
structure IOUAmount.ToNumberExact (a : IOUAmount) : Prop where
  mant_ne_min : a.mantissa_ ≠ Int64.minValue
  exp_lo : minExponent + 18 ≤ a.exponent_
  exp_hi : a.exponent_ ≤ maxExponent - 1

/-- A canonical 16-digit amount satisfies `ToNumberExact` (with enormous margin). -/
lemma IOUAmount.InRange16.toNumberExact {a : IOUAmount} (h : a.InRange16) :
    a.ToNumberExact := by
  refine ⟨?_, ?_, ?_⟩
  · intro hmin
    have hh := h.mant_hi
    rw [hmin, show Int64.minValue.toInt.natAbs = 9223372036854775808 from by decide] at hh
    omega
  · have := h.exp_lo; have hm : minExponent = -32768 := rfl; omega
  · have := h.exp_hi; have hM : maxExponent = 32768 := rfl; omega

end XRPL.Model.Protocol

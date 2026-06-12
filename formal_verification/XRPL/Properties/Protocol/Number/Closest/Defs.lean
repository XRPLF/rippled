import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Data.Int.Log

import XRPL.Properties.Protocol.Number.Closest.Helpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## Number.upper and Number.lower (positive case helpers) -/

/-- For positive `q`, the candidate mantissa is `q · 10^(-(Int.log 10 q - mantissaLog))`.
This lands in `[10^18, 10^19)` by construction. The integer ceiling is then
bumped to a valid mantissa.

Corner cases:
- `e_q < minExponent`: q is below the smallest positive representable. Return
  the smallest positive representable (`largeRange.min` at `minExponent`).
- `e_q > maxExponent`: q is above the largest positive representable. Return
  `none` (genuinely no representable ≥ q). -/
noncomputable def upperPosAux (q : ℚ) : Option Number :=
  let e_q : ℤ := Int.log 10 q - mantissaLog
  let m_real : ℚ := q * (10 : ℚ)^(-e_q)
  let m_ceil_nat : ℕ := ⌈m_real⌉₊
  match bumpToValidMantissa m_ceil_nat with
  | some m =>
    if h_exp : minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m < 2^64 then
      some ⟨false, ⟨m, by omega⟩, e_q⟩
    else if e_q < minExponent then
        some ⟨false, largeRange.min, minExponent⟩
    else none
  | none =>
    if h_exp : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent then
      some ⟨false, largeRange.min, e_q + 1⟩
    else if e_q + 1 < minExponent then
      some ⟨false, largeRange.min, minExponent⟩
    else none

/-- For positive `q`, the largest representable Number ≤ q.

Corner cases:
- `e_q < minExponent`: no positive normalized representable ≤ q exists, but
  `Number.zero.toRat = 0 ≤ q` (since `q > 0`), so return `Number.zero`.
- `e_q > maxExponent`: q is above the largest positive representable. Return
  `none` (handled separately by the maxExponent corner). -/
noncomputable def lowerPosAux (q : ℚ) : Option Number :=
  let e_q : ℤ := Int.log 10 q - mantissaLog
  let m_real : ℚ := q * (10 : ℚ)^(-e_q)
  let m_floor_nat : ℕ := ⌊m_real⌋₊
  let m := truncToValidMantissa m_floor_nat
  if h_pos : m ≥ largeRange.min.toNat ∧ m < 2^64 then
    if minExponent ≤ e_q ∧ e_q ≤ maxExponent then
      some ⟨false, ⟨m, by omega⟩, e_q⟩
    else if e_q < minExponent then
      some Number.zero
    else none
  else
    if minExponent ≤ e_q - 1 ∧ e_q - 1 ≤ maxExponent then
      some ⟨false, ⟨maxMul10Witness, by decide⟩, e_q - 1⟩
    else if e_q - 1 < minExponent then
      some Number.zero
    else none

/-! ## Top-level Number.upper / Number.lower -/

/-- Smallest representable Number `n` with `n.toRat ≥ q`, if it exists.
For `q < 0`, uses `upper(q) = -lower(-q)` with a `Number.zero` short-circuit. -/
noncomputable def Number.upper (q : ℚ) : Option Number :=
  if q = 0 then some Number.zero
  else if q < 0 then
    (lowerPosAux (-q)).map (fun n =>
      if n = Number.zero then Number.zero
      else { n with negative_ := true })
  else
    upperPosAux q

/-- Largest representable Number `n` with `n.toRat ≤ q`, if it exists.
For `q < 0`, uses `lower(q) = -upper(-q)`. -/
noncomputable def Number.lower (q : ℚ) : Option Number :=
  if q = 0 then some Number.zero
  else if q < 0 then
    (upperPosAux (-q)).map (fun n => { n with negative_ := true })
  else
    lowerPosAux q


end XRPL.Model.Protocol

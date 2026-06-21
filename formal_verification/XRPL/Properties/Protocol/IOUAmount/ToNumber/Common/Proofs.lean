import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.IOUAmount.Common.Defs
import XRPL.Properties.Protocol.IOUAmount.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Constructors.FromRepExact

/-! # Proof body for the `IOUAmount.toNumber` correctness headline.

`toNumber a mode = Number.from_rep a.mantissa_ a.exponent_ …`, which (under `ToNumberExact`)
is value-exact via `Number.from_rep_exact`. No rounding, since the `Int64` mantissa scales
up into `largeRange` exactly. Mirrors `Number/ToRep/`. The thin headline lives in
`IOUAmount.ToNumber.ToNumber`. -/

namespace XRPL.Model.Protocol

/-- On a `ToNumberExact` amount, `toNumber` succeeds with a normalized `Number` of the
same value. -/
theorem IOUAmount.toNumber_exact_proof (a : IOUAmount) (mode : rounding_mode)
    (ha : a.ToNumberExact) :
    ∃ xn : Number, a.toNumber mode = .ok xn ∧ xn.toRat = a.toRat ∧ xn.isNormalized := by
  obtain ⟨xn, hok, hval, hnorm⟩ :=
    Number.from_rep_exact a.mantissa_ a.exponent_ mode ha.mant_ne_min ha.exp_lo ha.exp_hi
  exact ⟨xn, hok, by rw [hval, IOUAmount.toRat_eq], hnorm⟩

end XRPL.Model.Protocol

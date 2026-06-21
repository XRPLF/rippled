import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount
import XRPL.Properties.Protocol.Number.Constructors.FromRepExact

/-! # Proof body for the `MPTAmount.toNumber` correctness headline.

`toNumber x mode = Number.from_rep x.value_ 0 …`, which (for `value_ ≠ Int64.minValue`) is
value-exact via `Number.from_rep_exact`. No rounding, since an exponent-0 integer mantissa
scales up into `largeRange` exactly. Mirrors `Number/ToRep/`. The thin headline lives in
`MPTAmount.ToNumber.ToNumber`. -/

namespace XRPL.Model.Protocol

/-- **`toNumber` is value-exact** (mantissa `≠ Int64.minValue`): it produces a normalized
`Number` of value `toRat`. -/
theorem MPTAmount.toNumber_exact_proof (x : MPTAmount) (mode : rounding_mode)
    (h_ne_min : x.value_ ≠ Int64.minValue) :
    ∃ xn : Number, x.toNumber mode = .ok xn ∧ xn.toRat = x.toRat ∧ xn.isNormalized := by
  obtain ⟨xn, hok, hval, hnorm⟩ := Number.from_rep_exact x.value_ 0 mode h_ne_min
    (by have h : minExponent = -32768 := rfl; omega)
    (by have h : maxExponent = 32768 := rfl; omega)
  refine ⟨xn, hok, ?_, hnorm⟩
  rw [hval]; unfold MPTAmount.toRat; norm_num

end XRPL.Model.Protocol

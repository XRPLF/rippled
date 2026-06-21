import XRPL.Properties.Protocol.MPTAmount.ToNumber.Common.Proofs

/-! # Correctness of `MPTAmount.toNumber`

`toNumber` embeds an `MPTAmount` into the `Number` layer value-exactly (mantissa
`≠ Int64.minValue`). Mirrors `Number/ToRep/`. -/

namespace XRPL.Model.Protocol

/-- **`toNumber` is value-exact** (mantissa `≠ Int64.minValue`). -/
theorem MPTAmount.toNumber_exact (x : MPTAmount) (mode : rounding_mode)
    (h_ne_min : x.value_ ≠ Int64.minValue) :
    ∃ xn : Number, x.toNumber mode = .ok xn ∧ xn.toRat = x.toRat ∧ xn.isNormalized :=
  MPTAmount.toNumber_exact_proof x mode h_ne_min

end XRPL.Model.Protocol

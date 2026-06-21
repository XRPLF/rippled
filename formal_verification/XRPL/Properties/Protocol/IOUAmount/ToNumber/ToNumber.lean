import XRPL.Properties.Protocol.IOUAmount.ToNumber.Common.Proofs

/-! # Correctness of `IOUAmount.toNumber` -/

namespace XRPL.Model.Protocol

/-- **`toNumber` is value-exact** under `ToNumberExact`: a normalized `Number` of value
`toRat`. -/
theorem IOUAmount.toNumber_exact (a : IOUAmount) (mode : rounding_mode) (ha : a.ToNumberExact) :
    ∃ xn : Number, a.toNumber mode = .ok xn ∧ xn.toRat = a.toRat ∧ xn.isNormalized :=
  IOUAmount.toNumber_exact_proof a mode ha

end XRPL.Model.Protocol

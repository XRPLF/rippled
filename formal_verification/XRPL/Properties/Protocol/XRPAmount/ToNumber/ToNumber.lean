import XRPL.Properties.Protocol.XRPAmount.ToNumber.Common.Proofs

/-! # Correctness of `XRPAmount.toNumber` -/

namespace XRPL.Model.Protocol

/-- **`toNumber` is value-exact** unless drops is `Int64.minValue`. `from_rep` converts every
`Int64` exactly except `minValue`, whose magnitude `2^63` is one past the representable limit
`maxRep = 2^63 - 1`. `minValue` is not a valid XRPAmount, so safe to hypothesise it will not happen. -/
theorem XRPAmount.toNumber_exact (x : XRPAmount) (mode : rounding_mode)
    (h_ne_min : x.drops_ ≠ Int64.minValue) :
    ∃ xn : Number, x.toNumber mode = .ok xn ∧ xn.toRat = x.toRat ∧ xn.isNormalized :=
  XRPAmount.toNumber_exact_proof x mode h_ne_min

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.IOUAmount.Neg.Common.Proofs

/-! # Correctness of `IOUAmount.operator_neg` -/

namespace XRPL.Model.Protocol

/-- **`operator_neg` is value-exact on canonical inputs** and returns a canonical amount. -/
theorem IOUAmount.operator_neg_exact (x : IOUAmount) (mode : rounding_mode)
    (hx : x.InRange16) :
    ∃ r : IOUAmount, IOUAmount.operator_neg x mode = .ok r ∧ r.toRat = -x.toRat ∧ r.InRange16 :=
  IOUAmount.operator_neg_exact_proof x mode hx

end XRPL.Model.Protocol

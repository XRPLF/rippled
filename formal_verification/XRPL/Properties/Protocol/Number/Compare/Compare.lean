import XRPL.Properties.Protocol.Number.Compare.Common.Proofs


namespace XRPL.Model.Protocol

/-- `x.operator_lt y` is `true` if and only if the rational value of `x` is below the rational value of `y`.

We hide the actual proof in a helper file (Common/Proofs.lean),
so readers of this file can focus on theorems and not tactics and proofs.
-/
theorem operator_lt_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_lt y = true ↔ x.toRat < y.toRat :=
  operator_lt_iff_proof x y hx hy

end XRPL.Model.Protocol

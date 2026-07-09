import XRPL.Properties.Protocol.Number.Compare.Common.Proofs


namespace XRPL.Model.Protocol

/-- Top-level comparison theorem for normalized numbers.
`x.operator_lt y` is `true` exactly when the value of `x` is below the value of `y` as rationals. -/
theorem operator_lt_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_lt y = true ↔ x.toRat < y.toRat :=
  operator_lt_iff_proof x y hx hy

end XRPL.Model.Protocol

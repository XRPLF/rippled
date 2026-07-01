import XRPL.Properties.Protocol.Number.Compare.Common.Proofs


namespace XRPL.Model.Protocol

theorem operator_lt_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_lt y = true ↔ x.toRat < y.toRat :=
  operator_lt_iff_proof x y hx hy

end XRPL.Model.Protocol

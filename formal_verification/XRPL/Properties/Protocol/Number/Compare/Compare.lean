import XRPL.Properties.Protocol.Number.Compare.Common.Proofs


namespace XRPL.Model.Protocol

theorem operator_lt_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_lt y = true ↔ x.toRat < y.toRat :=
  operator_lt_iff_proof x y hx hy

theorem operator_le_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_le y = true ↔ x.toRat ≤ y.toRat :=
  operator_le_iff_proof x y hx hy

theorem operator_gt_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_gt y = true ↔ y.toRat < x.toRat :=
  operator_gt_iff_proof x y hx hy

theorem operator_ge_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_ge y = true ↔ y.toRat ≤ x.toRat :=
  operator_ge_iff_proof x y hx hy

theorem operator_eq_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_eq y = true ↔ x.toRat = y.toRat :=
  operator_eq_iff_proof x y hx hy

theorem operator_ne_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_ne y = true ↔ x.toRat ≠ y.toRat :=
  operator_ne_iff_proof x y hx hy

end XRPL.Model.Protocol

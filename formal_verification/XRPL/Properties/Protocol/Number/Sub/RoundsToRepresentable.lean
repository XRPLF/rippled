import XRPL.Properties.Protocol.Number.Sub.Common.RoundsToRepresentableProofs

/-! # `operator_sub` is correctly rounded (`RoundsToRepresentable`), via `x - y = x + (-y)`.
Proofs in `Common.RoundsToRepresentableProofs`. -/

namespace XRPL.Model.Protocol

theorem operator_sub_rounded_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .downward :=
  operator_sub_rounded_downward_proof x y result hx hy hok hresult

theorem operator_sub_rounded_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .upward :=
  operator_sub_rounded_upward_proof x y result hx hy hok hresult

theorem operator_sub_rounded_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .towards_zero = .ok result) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .towards_zero :=
  operator_sub_rounded_towards_zero_proof x y result hx hy hok

theorem operator_sub_rounded_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .to_nearest = .ok result) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .to_nearest :=
  operator_sub_rounded_to_nearest_proof x y result hx hy hok

end XRPL.Model.Protocol

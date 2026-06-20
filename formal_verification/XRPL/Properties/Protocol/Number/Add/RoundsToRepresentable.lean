import XRPL.Properties.Protocol.Number.Add.Common.Downward.RoundedHelpers
import XRPL.Properties.Protocol.Number.Add.Common.Upward.RoundedHelpers
import XRPL.Properties.Protocol.Number.Add.Common.TowardsZero.RoundedHelpers
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.RoundedHelpers


namespace XRPL.Model.Protocol

-- hresult is needed because of underflow flush
theorem operator_add_rounded_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat + y.toRat) .downward :=
  operator_add_rounded_downward_proof x y result hx hy hok hresult

theorem operator_add_rounded_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat + y.toRat) .upward :=
  operator_add_rounded_upward_proof x y result hx hy hok hresult

theorem operator_add_rounded_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_add x y .towards_zero = .ok result) :
    Number.RoundsToRepresentable result (x.toRat + y.toRat) .towards_zero :=
  operator_add_rounded_towards_zero_proof x y result hx hy hok

theorem operator_add_rounded_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_add x y .to_nearest = .ok result) :
    Number.RoundsToRepresentable result (x.toRat + y.toRat) .to_nearest :=
  operator_add_rounded_to_nearest_proof x y result hx hy hok

end XRPL.Model.Protocol

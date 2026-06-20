import XRPL.Properties.Protocol.Number.Sub.Common.RoundsWithinProofs
import XRPL.Properties.Approx

/-! # `operator_sub` relative-error bounds (`RoundsWithin`). Subtraction reduces to addition
via `x - y = x + (-y)`: each theorem delegates to its `Add` counterpart, a diff-sign
subtraction becoming a same-sign addition. Proofs in `Common.RoundsWithinProofs`. -/

namespace XRPL.Model.Protocol

theorem operator_sub_rounding_bound_downward_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat - y.toRat)|
      < |x.toRat - y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_sub_rounding_bound_downward_tight_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

theorem operator_sub_rounds_diff_sign_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (hok : Number.operator_sub x y .downward = .ok result) :
    RoundsWithin result (x.toRat - y.toRat) .downward (10 / (2 ^ 63 + 2 : ℚ)) :=
  operator_sub_rounds_diff_sign_downward_proof x y result hx hy hx_mant_ne hy_mant_ne h_diff_sign hok

theorem operator_sub_rounds_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .downward (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_sub_rounds_downward_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

theorem operator_sub_rounding_bound_upward_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat - y.toRat)|
      < |x.toRat - y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_sub_rounding_bound_upward_tight_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

theorem operator_sub_rounds_diff_sign_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (hok : Number.operator_sub x y .upward = .ok result) :
    RoundsWithin result (x.toRat - y.toRat) .upward (10 / (2 ^ 63 + 2 : ℚ)) :=
  operator_sub_rounds_diff_sign_upward_proof x y result hx hy hx_mant_ne hy_mant_ne h_diff_sign hok

theorem operator_sub_rounds_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .upward (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_sub_rounds_upward_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

theorem operator_sub_rounding_bound_towards_zero_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat - y.toRat)|
      < |x.toRat - y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_sub_rounding_bound_towards_zero_tight_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

theorem operator_sub_rounds_diff_sign_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (hok : Number.operator_sub x y .towards_zero = .ok result) :
    RoundsWithin result (x.toRat - y.toRat) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) :=
  operator_sub_rounds_diff_sign_towards_zero_proof x y result hx hy hx_mant_ne hy_mant_ne h_diff_sign hok

theorem operator_sub_rounds_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .towards_zero (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_sub_rounds_towards_zero_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

theorem operator_sub_rounding_bound_to_nearest_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat - y.toRat)|
      ≤ |x.toRat - y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) :=
  operator_sub_rounding_bound_to_nearest_tight_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

theorem operator_sub_rounds_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .to_nearest (6 / (2 ^ 63 - 3 : ℚ)) :=
  operator_sub_rounds_to_nearest_proof x y result hx hy hx_mant_ne hy_mant_ne h_not_eq hok hresult

end XRPL.Model.Protocol

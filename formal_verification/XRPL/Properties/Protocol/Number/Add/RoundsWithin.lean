import XRPL.Properties.Protocol.Number.Add.Common.RoundsWithinProofs
import XRPL.Properties.Approx

/-! # `operator_add` relative-error bounds (`RoundsWithin`), per mode:
* `operator_add_rounds_<mode>`: unconditional, ε `11/(2^63-18)` (`6/(2^63-3)` for to_nearest).
* `operator_add_rounds_same_sign_<mode>`: tighter ε `10/(2^63+2)` (`5/(2^63+7)`) when operands
  share a sign (no cancellation). The general theorem case-splits onto it; Sub reuses it via
  `x - y = x + (-y)`, which turns a diff-sign subtraction into a same-sign addition. -/

namespace XRPL.Model.Protocol

theorem operator_add_rounds_same_sign_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .downward (10 / (2 ^ 63 + 2 : ℚ)) :=
  operator_add_rounds_same_sign_downward_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_same_sign h_not_zero hok

theorem operator_add_rounds_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .downward (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_add_rounds_downward_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_not_zero hok hresult

theorem operator_add_rounding_bound_same_sign_downward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_add x y .downward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result (x.toRat + y.toRat) (8 / (2 ^ 63 + 2 : ℚ)) :=
  operator_add_downward_attained

theorem operator_add_rounds_same_sign_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .upward (10 / (2 ^ 63 + 2 : ℚ)) :=
  operator_add_rounds_same_sign_upward_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_same_sign h_not_zero hok

theorem operator_add_rounds_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .upward (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_add_rounds_upward_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_not_zero hok hresult

theorem operator_add_rounding_bound_same_sign_upward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_add x y .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result (x.toRat + y.toRat) (9 / (2 ^ 63 + 2 : ℚ)) :=
  operator_add_upward_attained

theorem operator_add_rounds_same_sign_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) :=
  operator_add_rounds_same_sign_towards_zero_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_same_sign h_not_zero hok

theorem operator_add_rounds_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .towards_zero (11 / (2 ^ 63 - 18 : ℚ)) :=
  operator_add_rounds_towards_zero_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_not_zero hok hresult

theorem operator_add_rounding_bound_same_sign_towards_zero_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_add x y .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result (x.toRat + y.toRat) (8 / (2 ^ 63 + 2 : ℚ)) :=
  operator_add_towards_zero_attained

theorem operator_add_rounds_same_sign_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) :=
  operator_add_rounds_same_sign_to_nearest_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_same_sign h_not_zero hok

theorem operator_add_rounds_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .to_nearest (6 / (2 ^ 63 - 3 : ℚ)) :=
  operator_add_rounds_to_nearest_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_not_zero hok hresult

theorem operator_add_rounding_bound_same_sign_to_nearest_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_add x y .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        = |x.toRat + y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) :=
  operator_add_to_nearest_attained

end XRPL.Model.Protocol

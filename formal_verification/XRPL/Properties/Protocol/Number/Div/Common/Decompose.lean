import XRPL.Properties.Protocol.Number.Div.Common.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128Facts
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas


namespace XRPL.Model.Protocol

/-! # Division shared bridges

The staged `operator_div` routes through `doNormalize128`; these wrappers
instantiate the mode-generic `doNormalize128` result-shape and underflow
keystones with the division facts bundle, for the discrete-rounding files. -/

/-- A quotient of nonzero operands is nonzero. -/
theorem operator_div_truth_ne (x y : Number)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0) :
    x.toRat / y.toRat ≠ 0 :=
  div_ne_zero (Number.toRat_ne_zero_of_mantissa_ne_zero x hx_mant_ne)
    (Number.toRat_ne_zero_of_mantissa_ne_zero y hy_mant_ne)

/-- A successful `operator_div` has a nonzero divisor (division by the canonical
zero errors out). Shared across the four directed-mode `Rounded` files. -/
theorem operator_div_divisor_ne_zero (x y result : Number) (mode : rounding_mode)
    (hy : y.isNormalized)
    (hok : Number.operator_div x y mode = .ok result) :
    y.mantissa_ ≠ 0 := by
  intro h
  have hy_zero : y = Number.zero := Number.eq_zero_of_mantissa_zero y hy h
  unfold Number.operator_div at hok
  rw [if_pos (show y.operator_eq Number.zero = true from by rw [hy_zero]; decide)] at hok
  exact absurd hok (by intro hh; cases hh)

/-- The division result with nonzero mantissa is normalized. -/
theorem operator_div_result_isNormalized (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.isNormalized := by
  obtain ⟨M, ze', δ, zn, sticky, _, _, _, hM_pos, _, _, _, hok128, _, _, _⟩ :=
    operator_div_algorithmic_facts_represents x y result mode hx hy
      hx_mant_ne hy_mant_ne hok
  exact doNormalize128_result_isNormalized zn M ze' sticky mode hM_pos result hok128 hresult

/-- Weak top-exponent mantissa cap for the division result. -/
theorem operator_div_no_overflow_mantissa (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_exp_ge : result.exponent_ ≥ maxExponent) :
    result.mantissa_.toNat ≤ 9223372036854775820 := by
  obtain ⟨M, ze', δ, zn, sticky, _, _, _, hM_pos, _, _, _, hok128, _, _, _⟩ :=
    operator_div_algorithmic_facts_represents x y result mode hx hy
      hx_mant_ne hy_mant_ne hok
  exact doNormalize128_no_overflow_mantissa zn M ze' sticky mode hM_pos result
    hok128 hresult h_exp_ge

/-- A zero division result forces the quotient strictly below the smallest
positive representable. -/
theorem operator_div_underflow_truth_small (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y mode = .ok result)
    (hres0 : result.mantissa_ = 0) :
    |x.toRat / y.toRat| < (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
  obtain ⟨M, ze', δ, zn, sticky, hδ_low, _, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, _, hδ_lt, _⟩ :=
    operator_div_algorithmic_facts_represents x y result mode hx hy
      hx_mant_ne hy_mant_ne hok
  rw [htruth]
  exact doNormalize128_underflow_value_small zn M ze' δ sticky mode hδ_low hδ_lt
    hsticky_zero hM_pos hM_lt hδM result hok128 hres0

/-- Both operands of a successful, nonzero `operator_div` have nonzero mantissa.
A zero divisor errors out (so `hok = .ok` forces `y ≠ 0`); a zero numerator
returns the zero operand `x`, contradicting `hresult` (so `x ≠ 0`). -/
theorem operator_div_operands_ne_zero {x y result : Number} {mode : rounding_mode}
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y mode = .ok result) (hresult : result.mantissa_ ≠ 0) :
    x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 := by
  -- The divisor is nonzero (division by the canonical zero errors out).
  have hyz : y.mantissa_ ≠ 0 := operator_div_divisor_ne_zero x y result _ hy hok
  refine ⟨?_, hyz⟩
  -- A zero numerator returns `x`, whose mantissa is zero, contradicting `hresult`.
  intro hxz
  have hx_zero : x = Number.zero := Number.eq_zero_of_mantissa_zero x hx hxz
  have hy_guard : ¬ y.operator_eq Number.zero = true := by
    intro h
    exact hyz (Number.mantissa_eq_zero_of_operator_eq_zero h)
  unfold Number.operator_div at hok
  rw [if_neg hy_guard,
      if_pos (show x.operator_eq Number.zero = true from by rw [hx_zero]; decide)] at hok
  have h_result : result = x :=
    (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
  apply hresult
  rw [h_result, hx_zero]
  rfl

end XRPL.Model.Protocol

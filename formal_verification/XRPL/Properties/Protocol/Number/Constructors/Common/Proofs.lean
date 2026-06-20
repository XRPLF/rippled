import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Normalize.RoundsWithin

namespace XRPL.Model.Protocol

/-- The exact value of an `unchecked` (pre-normalization) number:
`sign · mantissa · 10^exponent`. -/
lemma Number.unchecked_toRat (neg : Bool) (m : UInt64) (e : Int) :
    (Number.unchecked neg m e).toRat
      = (if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e) := by
  by_cases hneg : neg = true
  · rw [if_pos hneg, Number.toRat_of_neg (Number.unchecked neg m e) hneg]
    simp only [Number.unchecked]; ring
  · have hf : (Number.unchecked neg m e).negative_ = false := eq_false_of_ne_true hneg
    rw [if_neg hneg, Number.toRat_of_nonneg (Number.unchecked neg m e) hf]
    simp only [Number.unchecked]; ring

/-- The exact value of the magnitude/sign split performed by `from_rep`:
the unchecked number built from `mantissa`'s sign and `|mantissa|` equals the
signed value `mantissa · 10^exponent`. -/
lemma Number.from_rep_input_toRat (mantissa : Int64) (e : Int) :
    (Number.unchecked (mantissa < 0) mantissa.toInt.natAbs.toUInt64 e).toRat
      = (mantissa.toInt : ℚ) * (10 : ℚ) ^ e := by
  rw [Number.unchecked_toRat]
  have hb : mantissa.toInt.natAbs < 2 ^ 64 := by
    have h1 : -2 ^ 63 ≤ mantissa.toInt := Int64.le_toInt mantissa
    have h2 : mantissa.toInt < 2 ^ 63 := Int64.toInt_lt mantissa
    omega
  have hnw : mantissa.toInt.natAbs.toUInt64.toNat = mantissa.toInt.natAbs :=
    UInt64.toNat_ofNat_of_lt hb
  have hsign : (mantissa < (0 : Int64)) ↔ (mantissa.toInt < 0) := by
    rw [Int64.lt_iff_toInt_lt]; rfl
  rw [hnw, Nat.cast_natAbs]
  by_cases h : mantissa.toInt < 0
  · rw [if_pos (decide_eq_true (hsign.mpr h)), abs_of_neg (by exact_mod_cast h)]
    push_cast; ring
  · rw [if_neg (by rw [decide_eq_true_eq, hsign]; exact h),
      abs_of_nonneg (by exact_mod_cast not_lt.mp h)]
    ring

/-! ### `Number.normalized` — the `(negative, mantissa, exponent)` constructor

`Number.normalized neg m e min max mode` is `(Number.unchecked neg m e).normalize`,
so each constructor `RoundsWithin` fact is the corresponding `normalize` fact applied to
the literal input value `sign · m · 10^e`. -/

theorem normalized_rounds_to_nearest_proof (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .to_nearest = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) := by
  rw [← Number.unchecked_toRat]
  exact normalize_rounds_to_nearest (Number.unchecked neg m e) result hok hr

theorem normalized_rounds_towards_zero_proof (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .towards_zero = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) := by
  rw [← Number.unchecked_toRat]
  exact normalize_rounds_towards_zero (Number.unchecked neg m e) result hok hr

theorem normalized_rounds_upward_proof (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .upward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  rw [← Number.unchecked_toRat]
  exact normalize_rounds_upward (Number.unchecked neg m e) result hok hr

theorem normalized_rounds_downward_proof (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .downward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .downward (10 / (2 ^ 63 + 2 : ℚ)) := by
  rw [← Number.unchecked_toRat]
  exact normalize_rounds_downward (Number.unchecked neg m e) result hok hr

/-- The `from_rep` rounding-error bound, `to_nearest` mode. -/
theorem from_rep_rounds_to_nearest_proof (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .to_nearest = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) := by
  have hbridge := Number.from_rep_input_toRat mantissa e
  rw [Number.unchecked_toRat] at hbridge
  rw [← hbridge]
  exact normalized_rounds_to_nearest_proof (mantissa < 0) mantissa.toInt.natAbs.toUInt64 e result hok hr

/-- The `from_rep` rounding-error bound, `towards_zero` mode. -/
theorem from_rep_rounds_towards_zero_proof (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .towards_zero = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hbridge := Number.from_rep_input_toRat mantissa e
  rw [Number.unchecked_toRat] at hbridge
  rw [← hbridge]
  exact normalized_rounds_towards_zero_proof (mantissa < 0) mantissa.toInt.natAbs.toUInt64 e result hok hr

/-- The `from_rep` rounding-error bound, `upward` mode. -/
theorem from_rep_rounds_upward_proof (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .upward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hbridge := Number.from_rep_input_toRat mantissa e
  rw [Number.unchecked_toRat] at hbridge
  rw [← hbridge]
  exact normalized_rounds_upward_proof (mantissa < 0) mantissa.toInt.natAbs.toUInt64 e result hok hr

/-- The `from_rep` rounding-error bound, `downward` mode. -/
theorem from_rep_rounds_downward_proof (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .downward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .downward (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hbridge := Number.from_rep_input_toRat mantissa e
  rw [Number.unchecked_toRat] at hbridge
  rw [← hbridge]
  exact normalized_rounds_downward_proof (mantissa < 0) mantissa.toInt.natAbs.toUInt64 e result hok hr

end XRPL.Model.Protocol

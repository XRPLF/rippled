import XRPL.Properties.Protocol.Number.Constructors.Common.Proofs


namespace XRPL.Model.Protocol


theorem normalized_rounds_to_nearest (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .to_nearest = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) :=
  normalized_rounds_to_nearest_proof neg m e result hok hr

theorem normalized_rounds_towards_zero (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .towards_zero = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) :=
  normalized_rounds_towards_zero_proof neg m e result hok hr

theorem normalized_rounds_upward (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .upward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .upward (10 / (2 ^ 63 + 2 : ℚ)) :=
  normalized_rounds_upward_proof neg m e result hok hr

theorem normalized_rounds_downward (neg : Bool) (m : UInt64) (e : Int) (result : Number)
    (hok : Number.normalized neg m e largeRange.min largeRange.max .downward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((if neg then (-1 : ℚ) else 1) * ((m.toNat : ℚ) * (10 : ℚ) ^ e))
      .downward (10 / (2 ^ 63 + 2 : ℚ)) :=
  normalized_rounds_downward_proof neg m e result hok hr

/-- The `from_rep` rounding-error bound, `to_nearest` mode. -/
theorem from_rep_rounds_to_nearest (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .to_nearest = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) :=
  from_rep_rounds_to_nearest_proof mantissa e result hok hr

theorem from_rep_rounds_towards_zero (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .towards_zero = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) :=
  from_rep_rounds_towards_zero_proof mantissa e result hok hr

theorem from_rep_rounds_upward (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .upward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .upward (10 / (2 ^ 63 + 2 : ℚ)) :=
  from_rep_rounds_upward_proof mantissa e result hok hr

theorem from_rep_rounds_downward (mantissa : Int64) (e : Int) (result : Number)
    (hok : Number.from_rep mantissa e largeRange.min largeRange.max .downward = .ok result)
    (hr : result.mantissa_ ≠ 0) :
    RoundsWithin result ((mantissa.toInt : ℚ) * (10 : ℚ) ^ e) .downward (10 / (2 ^ 63 + 2 : ℚ)) :=
  from_rep_rounds_downward_proof mantissa e result hok hr

end XRPL.Model.Protocol

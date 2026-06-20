import XRPL.Properties.Protocol.Number.ToRep.Common.Proofs


namespace XRPL.Model.Protocol

theorem to_rep_within_one (n : Number) (mode : rounding_mode) (r : Int64)
    (hn : n.isNormalized)
    (hok : n.to_rep mode = .ok r) :
    |(r.toInt : ℚ) - n.toRat| < 1 :=
  to_rep_within_one_proof n mode r hn hok

theorem to_rep_exact_of_exponent_nonneg (n : Number) (mode : rounding_mode) (r : Int64)
    (hn : n.isNormalized) (hexp : 0 ≤ n.exponent)
    (hok : n.to_rep mode = .ok r) :
    (r.toInt : ℚ) = n.toRat :=
  to_rep_exact_of_exponent_nonneg_proof n mode r hn hexp hok

/-- `to_rep` is exact on an exponent-0 (integer) number whose magnitude fits in
`maxRep` — no normalization needed. Keystone for MPT/native integer round-trips. -/
theorem to_rep_exact_of_exponent_zero (neg : Bool) (m : UInt64) (mode : rounding_mode) (r : Int64)
    (hm : m.toNat ≤ maxRep.toNat)
    (hok : (Number.unchecked neg m 0).to_rep mode = .ok r) :
    (r.toInt : ℚ) = (if neg then (-1 : ℚ) else 1) * (m.toNat : ℚ) :=
  to_rep_exact_of_exponent_zero_proof neg m mode r hm hok

end XRPL.Model.Protocol

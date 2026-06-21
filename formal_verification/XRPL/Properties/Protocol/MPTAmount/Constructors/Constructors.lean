import XRPL.Properties.Protocol.MPTAmount.Constructors.Common.Proofs

/-! # Correctness of the `MPTAmount` constructors -/

namespace XRPL.Model.Protocol

/-- **`ofInt64` is value-exact.** -/
theorem MPTAmount.ofInt64_toRat (v : Int64) :
    (MPTAmount.ofInt64 v).toRat = (v.toInt : ℚ) :=
  MPTAmount.ofInt64_toRat_proof v

/-- **`ofNumber` rounds a `Number` to within one** of its value. -/
theorem MPTAmount.ofNumber_within_one (n : Number) (mode : rounding_mode)
    (result : MPTAmount) (hn : n.isNormalized)
    (hok : MPTAmount.ofNumber n mode = .ok result) :
    |result.toRat - n.toRat| < 1 :=
  MPTAmount.ofNumber_within_one_proof n mode result hn hok

end XRPL.Model.Protocol

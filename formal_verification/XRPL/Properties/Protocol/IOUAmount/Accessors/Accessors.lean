import XRPL.Properties.Protocol.IOUAmount.Accessors.Common.Proofs

/-! # Correctness of the `IOUAmount` accessors -/

namespace XRPL.Model.Protocol

/-- **The public `mantissa` / `exponent` accessors are faithful.** -/
theorem IOUAmount.mantissa_exponent_eq_toRat (a : IOUAmount) :
    ((IOUAmount.mantissa a).toInt : ℚ) * (10 : ℚ) ^ (IOUAmount.exponent a) = a.toRat :=
  IOUAmount.mantissa_exponent_eq_toRat_proof a

/-- **`toBool` decides non-zeroness.** -/
theorem IOUAmount.toBool_iff (a : IOUAmount) :
    IOUAmount.toBool a = true ↔ a.toRat ≠ 0 :=
  IOUAmount.toBool_iff_proof a

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.MPTAmount.Accessors.Common.Proofs

/-! # Correctness of the `MPTAmount` accessors -/

namespace XRPL.Model.Protocol

/-- **`value` is faithful**: the integer cast of `value` is `toRat`. -/
theorem MPTAmount.value_toRat (x : MPTAmount) :
    ((MPTAmount.value x).toInt : ℚ) = x.toRat :=
  MPTAmount.value_toRat_proof x

/-- **`toBool` decides non-zeroness.** -/
theorem MPTAmount.toBool_iff (x : MPTAmount) :
    MPTAmount.toBool x = true ↔ x.toRat ≠ 0 :=
  MPTAmount.toBool_iff_proof x

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.XRPAmount.Accessors.Common.Proofs

namespace XRPL.Model.Protocol

/-- `value` accessor returns exactly `toRat`. -/
theorem XRPAmount.value_toRat (x : XRPAmount) :
    ((XRPAmount.value x).toInt : ℚ) = x.toRat :=
  XRPAmount.value_toRat_proof x

/-- `toBool` accessor returns true iff the `toRat` value is not 0. -/
theorem XRPAmount.toBool_iff (x : XRPAmount) :
    XRPAmount.toBool x = true ↔ x.toRat ≠ 0 :=
  XRPAmount.toBool_iff_proof x

end XRPL.Model.Protocol

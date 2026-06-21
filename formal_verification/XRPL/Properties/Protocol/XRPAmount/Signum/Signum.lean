import XRPL.Properties.Protocol.XRPAmount.Signum.Common.Proofs

/-! # Correctness of `XRPAmount.signum`

`signum` returns the sign of the value (`1` / `-1` / `0`). -/

namespace XRPL.Model.Protocol

/-- **`signum` returns the sign of `toRat`.** -/
theorem XRPAmount.signum_eq (x : XRPAmount) :
    x.signum = if 0 < x.toRat then 1 else if x.toRat < 0 then -1 else 0 :=
  XRPAmount.signum_eq_proof x

/-- **`signum = 1 ↔ value is positive`.** -/
theorem XRPAmount.signum_eq_one_iff (x : XRPAmount) : x.signum = 1 ↔ 0 < x.toRat :=
  XRPAmount.signum_eq_one_iff_proof x

/-- **`signum = -1 ↔ value is negative`.** -/
theorem XRPAmount.signum_eq_neg_one_iff (x : XRPAmount) : x.signum = -1 ↔ x.toRat < 0 :=
  XRPAmount.signum_eq_neg_one_iff_proof x

/-- **`signum = 0 ↔ value is zero`.** -/
theorem XRPAmount.signum_eq_zero_iff (x : XRPAmount) : x.signum = 0 ↔ x.toRat = 0 :=
  XRPAmount.signum_eq_zero_iff_proof x

end XRPL.Model.Protocol

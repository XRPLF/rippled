import XRPL.Properties.Protocol.MPTAmount.Signum.Common.Proofs

/-! # Correctness of `MPTAmount.signum`

`signum` returns the sign of the value (`1` / `-1` / `0`). -/

namespace XRPL.Model.Protocol

/-- **`signum` returns the sign of `toRat`.** -/
theorem MPTAmount.signum_eq (x : MPTAmount) :
    x.signum = if 0 < x.toRat then 1 else if x.toRat < 0 then -1 else 0 :=
  MPTAmount.signum_eq_proof x

/-- **`signum = 1 ↔ value is positive`.** -/
theorem MPTAmount.signum_eq_one_iff (x : MPTAmount) : x.signum = 1 ↔ 0 < x.toRat :=
  MPTAmount.signum_eq_one_iff_proof x

/-- **`signum = -1 ↔ value is negative`.** -/
theorem MPTAmount.signum_eq_neg_one_iff (x : MPTAmount) : x.signum = -1 ↔ x.toRat < 0 :=
  MPTAmount.signum_eq_neg_one_iff_proof x

/-- **`signum = 0 ↔ value is zero`.** -/
theorem MPTAmount.signum_eq_zero_iff (x : MPTAmount) : x.signum = 0 ↔ x.toRat = 0 :=
  MPTAmount.signum_eq_zero_iff_proof x

end XRPL.Model.Protocol

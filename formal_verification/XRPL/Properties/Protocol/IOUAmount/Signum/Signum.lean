import XRPL.Properties.Protocol.IOUAmount.Signum.Common.Proofs

/-! # Correctness of `IOUAmount.signum`

`signum` returns the sign of the value (`1` / `-1` / `0`). -/

namespace XRPL.Model.Protocol

/-- **`signum` returns the sign of `toRat`.** -/
theorem IOUAmount.signum_eq (a : IOUAmount) :
    a.signum = if 0 < a.toRat then 1 else if a.toRat < 0 then -1 else 0 :=
  IOUAmount.signum_eq_proof a

/-- **`signum = 1 ↔ value is positive`.** -/
theorem IOUAmount.signum_eq_one_iff (a : IOUAmount) : a.signum = 1 ↔ 0 < a.toRat :=
  IOUAmount.signum_eq_one_iff_proof a

/-- **`signum = -1 ↔ value is negative`.** -/
theorem IOUAmount.signum_eq_neg_one_iff (a : IOUAmount) : a.signum = -1 ↔ a.toRat < 0 :=
  IOUAmount.signum_eq_neg_one_iff_proof a

/-- **`signum = 0 ↔ value is zero`.** -/
theorem IOUAmount.signum_eq_zero_iff (a : IOUAmount) : a.signum = 0 ↔ a.toRat = 0 :=
  IOUAmount.signum_eq_zero_iff_proof a

end XRPL.Model.Protocol

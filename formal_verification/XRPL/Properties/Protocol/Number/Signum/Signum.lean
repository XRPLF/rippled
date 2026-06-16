import XRPL.Properties.Protocol.Number.Signum.Common.Proofs


namespace XRPL.Model.Protocol

/-- `Number.signum` returns the sign of the value: `1` if positive, `-1` if
negative, `0` if zero. The `isNormalized` hypothesis rules out a "negative zero"
(`negative_ = true`, `mantissa_ = 0`), which is not a canonical value. -/
theorem signum_eq (n : Number) (hn : n.isNormalized) :
    n.signum = if 0 < n.toRat then 1 else if n.toRat < 0 then -1 else 0 :=
  signum_eq_proof n hn

theorem signum_eq_one_iff (n : Number) (hn : n.isNormalized) :
    n.signum = 1 ↔ 0 < n.toRat :=
  signum_eq_one_iff_proof n hn

theorem signum_eq_neg_one_iff (n : Number) (hn : n.isNormalized) :
    n.signum = -1 ↔ n.toRat < 0 :=
  signum_eq_neg_one_iff_proof n hn

theorem signum_eq_zero_iff (n : Number) (hn : n.isNormalized) :
    n.signum = 0 ↔ n.toRat = 0 :=
  signum_eq_zero_iff_proof n hn

end XRPL.Model.Protocol

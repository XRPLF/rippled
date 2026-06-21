import XRPL.Properties.Protocol.XRPAmount.Constructors.Common.Proofs

/-! # Correctness of the `XRPAmount` constructors

`ofInt64` is value-exact; `ofNumber` rounds to integer drops (error `< 1` drop) via
`Number.to_rep`. -/

namespace XRPL.Model.Protocol

/-- **`ofInt64` is value-exact.** -/
theorem XRPAmount.ofInt64_toRat (v : Int64) :
    (XRPAmount.ofInt64 v).toRat = (v.toInt : ℚ) :=
  XRPAmount.ofInt64_toRat_proof v

/-- **`ofNumber` converts a `Number` to integer drops, off by less than one drop.**
This is the `Number`-to-`XRPAmount` conversion, where rounding is unavoidable (drops are
integral). The bound is `< 1` rather than `1/2` because the underlying `to_rep` is
mode-generic: a directed mode can round up to (just under) a full drop. -/
theorem XRPAmount.ofNumber_within_one (n : Number) (mode : rounding_mode)
    (result : XRPAmount) (hn : n.isNormalized)
    (hok : XRPAmount.ofNumber n mode = .ok result) :
    |result.toRat - n.toRat| < 1 :=
  XRPAmount.ofNumber_within_one_proof n mode result hn hok

end XRPL.Model.Protocol

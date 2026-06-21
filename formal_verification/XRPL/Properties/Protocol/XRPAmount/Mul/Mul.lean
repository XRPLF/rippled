import XRPL.Properties.Protocol.XRPAmount.Mul.Common.Proofs

namespace XRPL.Model.Protocol

/-- **`operator_mul` multiplies by an `Int64` (no overflow).** -/
theorem XRPAmount.operator_mul_exact (x : XRPAmount) (rhs : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt * rhs.toInt)
    (h_hi : x.drops_.toInt * rhs.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_mul x rhs).toRat = x.toRat * (rhs.toInt : ℚ) :=
  XRPAmount.operator_mul_exact_proof x rhs h_lo h_hi

end XRPL.Model.Protocol

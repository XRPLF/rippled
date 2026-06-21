import XRPL.Properties.Protocol.XRPAmount.Sub.Common.Proofs

/-! # Correctness of the `XRPAmount` subtraction / negation operators

Each raw `Int64` operation is value-exact (`toRat` of the result equals the rational
operation) as long as the exact integer result stays in the `Int64` range (no overflow). -/

namespace XRPL.Model.Protocol

/-- **`operator_sub` subtracts (no overflow).** -/
theorem XRPAmount.operator_sub_exact (x y : XRPAmount)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt - y.drops_.toInt)
    (h_hi : x.drops_.toInt - y.drops_.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_sub x y).toRat = x.toRat - y.toRat :=
  XRPAmount.operator_sub_exact_proof x y h_lo h_hi

/-- **`operator_neg` negates (overflows only at `minValue`, ruled out by `h_hi`).** -/
theorem XRPAmount.operator_neg_exact (x : XRPAmount)
    (h_hi : -x.drops_.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_neg x).toRat = -x.toRat :=
  XRPAmount.operator_neg_exact_proof x h_hi

/-- **`operator_sub_int` subtracts an `Int64` (no overflow).** -/
theorem XRPAmount.operator_sub_int_exact (x : XRPAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt - v.toInt)
    (h_hi : x.drops_.toInt - v.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_sub_int x v).toRat = x.toRat - (v.toInt : ℚ) :=
  XRPAmount.operator_sub_int_exact_proof x v h_lo h_hi

end XRPL.Model.Protocol

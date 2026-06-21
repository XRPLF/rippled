import XRPL.Properties.Protocol.MPTAmount.Sub.Common.Proofs

/-! # Correctness of the `MPTAmount` subtraction / negation operators -/

namespace XRPL.Model.Protocol

/-- **`operator_sub` subtracts (no overflow).** -/
theorem MPTAmount.operator_sub_exact (x y : MPTAmount)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt - y.value_.toInt)
    (h_hi : x.value_.toInt - y.value_.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_sub x y).toRat = x.toRat - y.toRat :=
  MPTAmount.operator_sub_exact_proof x y h_lo h_hi

/-- **`operator_neg` negates (overflows only at `minValue`, ruled out by `h_hi`).** -/
theorem MPTAmount.operator_neg_exact (x : MPTAmount)
    (h_hi : -x.value_.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_neg x).toRat = -x.toRat :=
  MPTAmount.operator_neg_exact_proof x h_hi

/-- **`operator_sub_int` subtracts an `Int64` (no overflow).** -/
theorem MPTAmount.operator_sub_int_exact (x : MPTAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt - v.toInt)
    (h_hi : x.value_.toInt - v.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_sub_int x v).toRat = x.toRat - (v.toInt : ℚ) :=
  MPTAmount.operator_sub_int_exact_proof x v h_lo h_hi

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.XRPAmount.Add.Common.Proofs

namespace XRPL.Model.Protocol

/-- **`operator_add` adds (no overflow).**
- `h_lo`: the exact integer sum is `≥ Int64.minValue` (no underflow);
- `h_hi`: the exact integer sum is `≤ Int64.maxValue` (no overflow).

Safe to assume: `STAmount.canonicalize` rejects native amounts above `kMaxNativeN` on
deserialization, and `kMaxNativeN` is below `Int64.maxValue`. -/
theorem XRPAmount.operator_add_exact (x y : XRPAmount)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt + y.drops_.toInt)
    (h_hi : x.drops_.toInt + y.drops_.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_add x y).toRat = x.toRat + y.toRat :=
  XRPAmount.operator_add_exact_proof x y h_lo h_hi

/-- **`operator_add_int` adds an `Int64` (no overflow).**
- Same hypothesis as operator_add -/
theorem XRPAmount.operator_add_int_exact (x : XRPAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt + v.toInt)
    (h_hi : x.drops_.toInt + v.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_add_int x v).toRat = x.toRat + (v.toInt : ℚ) :=
  XRPAmount.operator_add_int_exact_proof x v h_lo h_hi

end XRPL.Model.Protocol

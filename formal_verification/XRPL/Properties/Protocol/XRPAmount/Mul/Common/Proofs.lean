import Mathlib.Tactic
import XRPL.Model.Protocol.XRPAmount
import XRPL.Properties.Protocol.Common.AmountArith

/-! # Proof body for the `XRPAmount.operator_mul` correctness headline.

`operator_mul` multiplies an `XRPAmount` by a raw `Int64` factor (wraps mod 2⁶⁴), so it is
value-exact precisely when the exact integer product stays in the `Int64` range (no
overflow). The generic no-overflow `bmod` collapse is the shared
`AmountArith.toInt_bmod_self`. The thin headline lives in `XRPAmount.Mul.Mul`. -/

namespace XRPL.Model.Protocol

/-- **`operator_mul` (by an `Int64`) is value-exact (no overflow).** -/
theorem XRPAmount.operator_mul_exact_proof (x : XRPAmount) (rhs : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt * rhs.toInt)
    (h_hi : x.drops_.toInt * rhs.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_mul x rhs).toRat = x.toRat * (rhs.toInt : ℚ) := by
  unfold XRPAmount.operator_mul XRPAmount.toRat
  show ((x.drops_ * rhs).toInt : ℚ) = (x.drops_.toInt : ℚ) * (rhs.toInt : ℚ)
  rw [Int64.toInt_mul, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

end XRPL.Model.Protocol

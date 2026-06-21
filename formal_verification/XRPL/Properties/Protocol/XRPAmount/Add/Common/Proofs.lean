import Mathlib.Tactic
import XRPL.Model.Protocol.XRPAmount
import XRPL.Properties.Protocol.Common.AmountArith

/-! # Proof bodies for the `XRPAmount` addition correctness headlines.

`XRPAmount` addition is raw signed `Int64` (wraps mod 2⁶⁴), so it is value-exact precisely
when the exact integer sum stays in the `Int64` range (no overflow). The generic no-overflow
`bmod` collapse is the shared `AmountArith.toInt_bmod_self`. The thin headlines live in
`XRPAmount.Add.Add`. -/

namespace XRPL.Model.Protocol

/-- **`operator_add` is value-exact (no overflow).** -/
theorem XRPAmount.operator_add_exact_proof (x y : XRPAmount)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt + y.drops_.toInt)
    (h_hi : x.drops_.toInt + y.drops_.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_add x y).toRat = x.toRat + y.toRat := by
  unfold XRPAmount.operator_add XRPAmount.toRat
  show ((x.drops_ + y.drops_).toInt : ℚ) = (x.drops_.toInt : ℚ) + (y.drops_.toInt : ℚ)
  rw [Int64.toInt_add, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

/-- **`operator_add_int` is value-exact (no overflow).** -/
theorem XRPAmount.operator_add_int_exact_proof (x : XRPAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt + v.toInt)
    (h_hi : x.drops_.toInt + v.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_add_int x v).toRat = x.toRat + (v.toInt : ℚ) := by
  unfold XRPAmount.operator_add_int XRPAmount.toRat
  show ((x.drops_ + v).toInt : ℚ) = (x.drops_.toInt : ℚ) + (v.toInt : ℚ)
  rw [Int64.toInt_add, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

end XRPL.Model.Protocol

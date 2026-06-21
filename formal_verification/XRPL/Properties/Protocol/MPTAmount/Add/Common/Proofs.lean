import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount
import XRPL.Properties.Protocol.Common.AmountArith

/-! # Proof bodies for the `MPTAmount` addition correctness headlines.

`MPTAmount` addition is raw signed `Int64` (wraps mod 2⁶⁴), so it is value-exact precisely
when the exact integer sum stays in the `Int64` range (no overflow). The generic no-overflow
`bmod` collapse is the shared `AmountArith.toInt_bmod_self`. The thin headlines live in
`MPTAmount.Add.Add`. -/

namespace XRPL.Model.Protocol

/-- **`operator_add` is value-exact (no overflow).** -/
theorem MPTAmount.operator_add_exact_proof (x y : MPTAmount)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt + y.value_.toInt)
    (h_hi : x.value_.toInt + y.value_.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_add x y).toRat = x.toRat + y.toRat := by
  unfold MPTAmount.operator_add MPTAmount.toRat
  show ((x.value_ + y.value_).toInt : ℚ) = (x.value_.toInt : ℚ) + (y.value_.toInt : ℚ)
  rw [Int64.toInt_add, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

/-- **`operator_add_int` is value-exact (no overflow).** -/
theorem MPTAmount.operator_add_int_exact_proof (x : MPTAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt + v.toInt)
    (h_hi : x.value_.toInt + v.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_add_int x v).toRat = x.toRat + (v.toInt : ℚ) := by
  unfold MPTAmount.operator_add_int MPTAmount.toRat
  show ((x.value_ + v).toInt : ℚ) = (x.value_.toInt : ℚ) + (v.toInt : ℚ)
  rw [Int64.toInt_add, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

end XRPL.Model.Protocol

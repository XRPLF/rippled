import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount
import XRPL.Properties.Protocol.Common.AmountArith

/-! # Proof bodies for the `MPTAmount` subtraction / negation correctness headlines.

`MPTAmount` subtraction and negation are raw signed `Int64` (wraps mod 2⁶⁴), so they are
value-exact precisely when the exact integer result stays in the `Int64` range (no
overflow). The generic no-overflow `bmod` collapse is the shared
`AmountArith.toInt_bmod_self`.-/

namespace XRPL.Model.Protocol

/-- **`operator_sub` is value-exact (no overflow).** -/
theorem MPTAmount.operator_sub_exact_proof (x y : MPTAmount)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt - y.value_.toInt)
    (h_hi : x.value_.toInt - y.value_.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_sub x y).toRat = x.toRat - y.toRat := by
  unfold MPTAmount.operator_sub MPTAmount.toRat
  show ((x.value_ - y.value_).toInt : ℚ) = (x.value_.toInt : ℚ) - (y.value_.toInt : ℚ)
  rw [Int64.toInt_sub, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

/-- **`operator_neg` is value-exact (overflows only at `minValue`).** The lower bound
`minValue ≤ -x` is automatic (since `x ≤ maxValue < 2⁶³`), so only the upper bound
`-x ≤ maxValue` is required, i.e. `x ≠ minValue`. -/
theorem MPTAmount.operator_neg_exact_proof (x : MPTAmount)
    (h_hi : -x.value_.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_neg x).toRat = -x.toRat := by
  have h_lo : Int64.minValue.toInt ≤ -x.value_.toInt := by
    have := Int64.toInt_le x.value_
    have hmin : Int64.minValue.toInt = (-9223372036854775808 : ℤ) := by decide
    have hmax : Int64.maxValue.toInt = (9223372036854775807 : ℤ) := by decide
    omega
  unfold MPTAmount.operator_neg MPTAmount.toRat
  show ((-x.value_).toInt : ℚ) = -(x.value_.toInt : ℚ)
  rw [Int64.toInt_neg, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

/-- **`operator_sub_int` is value-exact (no overflow).** -/
theorem MPTAmount.operator_sub_int_exact_proof (x : MPTAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt - v.toInt)
    (h_hi : x.value_.toInt - v.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_sub_int x v).toRat = x.toRat - (v.toInt : ℚ) := by
  unfold MPTAmount.operator_sub_int MPTAmount.toRat
  show ((x.value_ - v).toInt : ℚ) = (x.value_.toInt : ℚ) - (v.toInt : ℚ)
  rw [Int64.toInt_sub, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

end XRPL.Model.Protocol

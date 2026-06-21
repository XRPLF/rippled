import Mathlib.Tactic
import XRPL.Model.Protocol.XRPAmount
import XRPL.Properties.Protocol.Common.AmountArith

/-! # Proof bodies for the `XRPAmount` subtraction / negation correctness headlines.

`XRPAmount` subtraction and negation are raw signed `Int64` (wraps mod 2⁶⁴), so they are
value-exact precisely when the exact integer result stays in the `Int64` range (no
overflow). The generic no-overflow `bmod` collapse is the shared
`AmountArith.toInt_bmod_self`. The thin headlines live in `XRPAmount.Sub.Sub`. -/

namespace XRPL.Model.Protocol

/-- **`operator_sub` is value-exact (no overflow).** -/
theorem XRPAmount.operator_sub_exact_proof (x y : XRPAmount)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt - y.drops_.toInt)
    (h_hi : x.drops_.toInt - y.drops_.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_sub x y).toRat = x.toRat - y.toRat := by
  unfold XRPAmount.operator_sub XRPAmount.toRat
  show ((x.drops_ - y.drops_).toInt : ℚ) = (x.drops_.toInt : ℚ) - (y.drops_.toInt : ℚ)
  rw [Int64.toInt_sub, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

/-- **`operator_neg` is value-exact (overflows only at `minValue`).** The lower bound
`minValue ≤ -x` is automatic (since `x ≤ maxValue < 2⁶³`), so only the upper bound
`-x ≤ maxValue` is required, i.e. `x ≠ minValue`. -/
theorem XRPAmount.operator_neg_exact_proof (x : XRPAmount)
    (h_hi : -x.drops_.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_neg x).toRat = -x.toRat := by
  have h_lo : Int64.minValue.toInt ≤ -x.drops_.toInt := by
    have := Int64.toInt_le x.drops_
    have hmin : Int64.minValue.toInt = (-9223372036854775808 : ℤ) := by decide
    have hmax : Int64.maxValue.toInt = (9223372036854775807 : ℤ) := by decide
    omega
  unfold XRPAmount.operator_neg XRPAmount.toRat
  show ((-x.drops_).toInt : ℚ) = -(x.drops_.toInt : ℚ)
  rw [Int64.toInt_neg, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

/-- **`operator_sub_int` is value-exact (no overflow).** -/
theorem XRPAmount.operator_sub_int_exact_proof (x : XRPAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.drops_.toInt - v.toInt)
    (h_hi : x.drops_.toInt - v.toInt ≤ Int64.maxValue.toInt) :
    (XRPAmount.operator_sub_int x v).toRat = x.toRat - (v.toInt : ℚ) := by
  unfold XRPAmount.operator_sub_int XRPAmount.toRat
  show ((x.drops_ - v).toInt : ℚ) = (x.drops_.toInt : ℚ) - (v.toInt : ℚ)
  rw [Int64.toInt_sub, AmountArith.toInt_bmod_self h_lo h_hi]; push_cast; ring

end XRPL.Model.Protocol

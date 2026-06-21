import XRPL.Properties.Protocol.MPTAmount.Add.Common.Proofs

/-! # Correctness of the `MPTAmount` addition operators -/

namespace XRPL.Model.Protocol

/-- **`operator_add` adds (no overflow).**
- `h_lo`: the exact integer sum is `≥ Int64.minValue` (no underflow);
- `h_hi`: the exact integer sum is `≤ Int64.maxValue` (no overflow).

Unlike XRP, a single MPT amount can reach `maxMPTokenAmount = Int64.maxValue`, so two of them
can overflow. The caller must keep the sum in range. In rippled, total outstanding per
issuance is capped at `maxMPTokenAmount`, so balances of one issuance sum within range. -/
theorem MPTAmount.operator_add_exact (x y : MPTAmount)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt + y.value_.toInt)
    (h_hi : x.value_.toInt + y.value_.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_add x y).toRat = x.toRat + y.toRat :=
  MPTAmount.operator_add_exact_proof x y h_lo h_hi

/-- **`operator_add_int` adds an `Int64` (no overflow).**
- Same hypothesis as operator_add -/
theorem MPTAmount.operator_add_int_exact (x : MPTAmount) (v : Int64)
    (h_lo : Int64.minValue.toInt ≤ x.value_.toInt + v.toInt)
    (h_hi : x.value_.toInt + v.toInt ≤ Int64.maxValue.toInt) :
    (MPTAmount.operator_add_int x v).toRat = x.toRat + (v.toInt : ℚ) :=
  MPTAmount.operator_add_int_exact_proof x v h_lo h_hi

end XRPL.Model.Protocol

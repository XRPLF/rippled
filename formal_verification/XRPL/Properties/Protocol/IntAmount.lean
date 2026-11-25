import Mathlib.Tactic
import XRPL.Model.Protocol.IntAmount

namespace XRPL.Model.Protocol

/-- The mathematical value of the amount. -/
def IntAmount.toRat (x : IntAmount) : ℚ := (x.value.toInt : ℚ)

/-- `signum` is the sign of the amount's mathematical value. -/
theorem IntAmount.signum_eq (x : IntAmount) :
    x.signum = if 0 < x.toRat then 1 else if x.toRat < 0 then -1 else 0 := by
  -- Restate the `Int64` comparisons of `signum` over the integers.
  have h_neg : x.value < 0 ↔ x.value.toInt < 0 := by
    rw [Int64.lt_iff_toInt_lt, Int64.toInt_zero]
  have h_zero : x.value = 0 ↔ x.value.toInt = 0 := by
    rw [← Int64.toInt_zero, Int64.toInt_inj]
  unfold IntAmount.signum IntAmount.toRat
  simp only [Int.cast_pos, Int.cast_lt_zero, bne_iff_ne, ne_eq, h_neg, h_zero]
  split_ifs <;> omega

end XRPL.Model.Protocol

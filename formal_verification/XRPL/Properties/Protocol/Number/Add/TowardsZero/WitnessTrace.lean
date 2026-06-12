import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `operator_add_rounding_bound_towards_zero` (same-sign)

The bound `10/(2^63 + 2)` is the supremum but is **not attained**. We exhibit a
witness whose relative error exceeds `9/(2^63 + 2)`:

`x = (false, 1000000000000000000, 0)`, `y = (false, 8223372036854775809, 0)`.

* Both operands share exponent 0 (no alignment needed).
* Sum at exp 0: `1000000000000000000 + 8223372036854775809 = 9223372036854775809`,
  which exceeds `maxRep = maxRepNat`. `doDropDigit128` extracts digit
  `9` and scales to `mantissaFloor` at exp 1.
* `.towards_zero` truncates (`Guard.round = -1`). After `bringIntoRange`
  (since `mantissaFloor < minMantissa = 10^18`), mantissa becomes
  `9223372036854775800` at exp 0.
* Result = `9223372036854775800`. Truth = `9223372036854775809`. Error = `9`.

Relative error: `9 / 9223372036854775809`. Cross-multiply against
`9 / maxRepCuspTarget`: LHS = `9 * maxRepCuspTarget = 83010348331692982290`,
RHS = `9 * 9223372036854775809 = 83010348331692982281`. LHS > RHS, hence
`relative error > 9/(2^63 + 2)`. -/

/-- `operator_add` trace for the towards_zero witness inputs. -/
lemma operator_add_towards_zero_witness :
    Number.operator_add
        ⟨false, 1000000000000000000, 0⟩
        ⟨false, 8223372036854775809, 0⟩
        .towards_zero =
      .ok ⟨false, 9223372036854775800, 0⟩ := by
  unfold Number.operator_add
  have hy_ne : (⟨false, 8223372036854775809, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hx_ne : (⟨false, 1000000000000000000, 0⟩ : Number).operator_eq Number.zero = false := by decide
  have hxy_neg_ne : (⟨false, 1000000000000000000, 0⟩ : Number).operator_eq
      (⟨false, 8223372036854775809, 0⟩ : Number).operator_neg = false := by decide
  simp only [hy_ne, hx_ne, hxy_neg_ne, Bool.false_eq_true, if_false]
  rw [if_neg (by decide : ¬ ((0 : Int) < 0))]
  rw [if_neg (by decide : ¬ ((0 : Int) > 0))]
  rw [show ((false : Bool) == (false : Bool)) = true from rfl]
  simp only [if_true]
  have hsum : toUInt128 (1000000000000000000 : UInt64) + toUInt128 (8223372036854775809 : UInt64)
      = (9223372036854775809 : UInt128) := by decide
  rw [hsum]
  rw [show (decide ((9223372036854775809 : UInt128) > toUInt128 largeRange.max) ||
           decide ((9223372036854775809 : UInt128) > toUInt128 maxRep)) = true from by decide]
  simp only [if_true]
  have hdrop : Guard.new.doDropDigit128 (9223372036854775809 : UInt128) 0
      = ({ digits_ := 10376293541461622784, xbit_ := false, sbit_ := false },
         (mantissaFloor : UInt128), (1 : Int)) := by
    unfold Guard.doDropDigit128 Guard.new Guard.push; rfl
  rw [hdrop]
  simp only
  have htoUInt64 : toUInt64 (mantissaFloor : UInt128) = (mantissaFloor : UInt64) := by decide
  rw [htoUInt64]
  -- doRoundUp for towards_zero: just truncates, then bringIntoRange scales up
  -- (mantissaFloor < minMantissa → mantissa*10 = 9223372036854775800, exp-1 = 0)
  have h_rup : ({ digits_ := 10376293541461622784, xbit_ := false, sbit_ := false } : Guard).doRoundUp
      false (mantissaFloor : UInt64) (1 : Int) largeRange.min largeRange.max .towards_zero
      "Number::addition overflow" =
      .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 0 } := by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit
    rfl
  rw [h_rup]
  simp only [RoundResult.toNumber]
  unfold Number.normalize
  rw [show doNormalize false (9223372036854775800 : UInt64) (0 : Int)
        largeRange.min largeRange.max .towards_zero
        = .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 0 } from by
    unfold doNormalize
    rw [show ((9223372036854775800 : UInt64) == 0) = false from rfl]
    simp only [Bool.false_eq_true, if_false]
    rw [show doNormalize_scaleUp largeRange.min (9223372036854775800 : UInt64) (0 : Int)
          = ((9223372036854775800 : UInt64), (0 : Int)) by
          unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
    rw [show doNormalize_scaleDown largeRange.max (9223372036854775800 : UInt64) (0 : Int) Guard.new
          = .ok ((9223372036854775800 : UInt64), (0 : Int), Guard.new) by
          unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
    simp only []
    rw [show ((0 : Int) < minExponent || (9223372036854775800 : UInt64) < largeRange.min) = false from by decide]
    simp only [Bool.false_eq_true, if_false]
    rw [show doNormalize_capAtMaxRep (9223372036854775800 : UInt64) (0 : Int) Guard.new
          = .ok ((9223372036854775800 : UInt64), (0 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep
      rw [if_neg (by decide)]]
    simp only []
    have h_rup2 : Guard.new.doRoundUp false (9223372036854775800 : UInt64) (0 : Int)
        largeRange.min largeRange.max .towards_zero "Number::normalize 2" =
        .ok { negative_ := false, mantissa_ := 9223372036854775800, exponent_ := 0 } := by
      unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl
    rw [h_rup2]
    simp only [RoundResult.toNumber]]

end XRPL.Model.Protocol

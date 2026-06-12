import XRPL.Properties.Protocol.Number.Add.Common
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `operator_add_rounding_bound_same_sign_upward`

The bound `10/(2^63 + 2)` is the tight supremum. Witness:
`x = 10^18` at exponent 0, `y = 10^18` at exponent 20.
After aligning `x` down to exponent 20, `xm_a = 0` and the guard captures
the single nonzero digit (a `1` at slot 14). The sum is `10^18`, no further
drop fires, and `.upward` rounds up to `10^18 + 1` because the guard is
nonzero. The relative error `(10^20 - 10^18) / (10^38 + 10^18)` strictly
exceeds `9/(2^63 + 2)`. -/

set_option maxRecDepth 1024 in
/-- `alignDown` trace from `(10^18, 0)` to exponent `20`: produces mantissa `0`
and a guard whose `digits_ = 2^56` (one packed `1`-nibble at slot 14). -/
lemma alignDown_upward_addwitness :
    Number.operator_add.alignDown (1000000000000000000 : UInt64) (0 : Int) Guard.new (20 : Int)
      = ((0 : UInt64), (20 : Int),
         { digits_ := 72057594037927936, xbit_ := false, sbit_ := false }) := by
  repeat (first | rw [alignDown_step (by decide)] | rw [alignDown_noop (by decide)])
  rfl

/-- `doNormalize` trace for the upward witness post-rounding state. -/
lemma doNormalize_upward_addwitness :
    doNormalize false (1000000000000000001 : UInt64) (20 : Int)
        largeRange.min largeRange.max .upward =
        .ok { negative_ := false, mantissa_ := 1000000000000000001, exponent_ := 20 } := by
  unfold doNormalize
  rw [show ((1000000000000000001 : UInt64) == 0) = false from rfl]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_scaleUp largeRange.min (1000000000000000001 : UInt64) 20
        = ((1000000000000000001 : UInt64), (20 : Int)) by
        unfold doNormalize_scaleUp; rw [if_neg (by decide)]]
  rw [show doNormalize_scaleDown largeRange.max (1000000000000000001 : UInt64) (20 : Int) Guard.new
        = .ok ((1000000000000000001 : UInt64), (20 : Int), Guard.new) by
        unfold doNormalize_scaleDown; rw [dif_neg (by decide)]]
  simp only []
  rw [show ((20 : Int) < minExponent || (1000000000000000001 : UInt64) < largeRange.min) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (1000000000000000001 : UInt64) (20 : Int) Guard.new
        = .ok ((1000000000000000001 : UInt64), (20 : Int), Guard.new) from by
      unfold doNormalize_capAtMaxRep; rw [if_neg (by decide)]]
  simp only []
  rw [show Guard.new.doRoundUp false (1000000000000000001 : UInt64) (20 : Int)
        largeRange.min largeRange.max .upward "Number::normalize 2" =
        .ok { negative_ := false, mantissa_ := 1000000000000000001, exponent_ := 20 } from by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit; rfl]
  rfl

/-- `operator_add` trace for the upward witness inputs. -/
lemma operator_add_upward_witness :
    Number.operator_add
        ⟨false, 1000000000000000000, 0⟩
        ⟨false, 1000000000000000000, 20⟩
        .upward =
      .ok ⟨false, 1000000000000000001, 20⟩ := by
  unfold Number.operator_add
  conv_lhs =>
    rw [show (⟨false, 1000000000000000000, 0⟩ : Number).operator_eq Number.zero = false from by decide,
        show (⟨false, 1000000000000000000, 20⟩ : Number).operator_eq Number.zero = false from by decide,
        show (⟨false, 1000000000000000000, 0⟩ : Number).operator_eq
            (⟨false, 1000000000000000000, 20⟩ : Number).operator_neg = false from by decide,
        if_neg (by decide : ¬ false = true),
        if_neg (by decide : ¬ false = true),
        if_neg (by decide : ¬ false = true)]
  dsimp only
  rw [if_pos (by decide : (0 : Int) < 20)]
  rw [show (if (false : Bool) = true then Guard.new.set_negative else Guard.new) = Guard.new from if_neg (by decide)]
  rw [alignDown_upward_addwitness]
  dsimp only
  rw [show ((false : Bool) == false) = true from rfl]
  rw [if_pos rfl]
  rw [show toUInt128 (0 : UInt64) + toUInt128 (1000000000000000000 : UInt64)
        = (1000000000000000000 : UInt128) from by decide]
  rw [if_neg (by decide : ¬ ((decide ((1000000000000000000 : UInt128) > toUInt128 largeRange.max) ||
           decide ((1000000000000000000 : UInt128) > toUInt128 maxRep)) = true))]
  rw [show toUInt64 (1000000000000000000 : UInt128) = (1000000000000000000 : UInt64) from by decide]
  rw [show ({ digits_ := 72057594037927936, xbit_ := false, sbit_ := false } : Guard).doRoundUp
        false (1000000000000000000 : UInt64) (20 : Int) largeRange.min largeRange.max .upward
        "Number::addition overflow" =
        .ok { negative_ := false, mantissa_ := 1000000000000000001, exponent_ := 20 } from by
    unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit
    rfl]
  simp only [RoundResult.toNumber]
  unfold Number.normalize
  exact doNormalize_upward_addwitness

end XRPL.Model.Protocol

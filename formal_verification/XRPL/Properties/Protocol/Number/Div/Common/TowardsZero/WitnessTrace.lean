import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Div.Common.TowardsZero.BoundProof


namespace XRPL.Model.Protocol

/-! ### Sharpness witness for `operator_div` under `.towards_zero`

`x = 1000735865998746448`, `y = 1085000000000003547`: the staged quotient is
`⌊x·10²²/y⌋ = 9223372036854775819999` with a nonzero residual (sticky), whose
trace drops `9,9,9` in `doNormalize_scaleDown128` and a fourth `9` in
`doNormalize_capAtMaxRep` (guard `0x9999…`, the `capAtMaxRep → mantissaFloorSucc`
corner). Truncation then loses ≈ 1 ulp at scale `mantissaFloorSucc`, giving
relative error ≈ 1.084·10⁻¹⁸ > 9/(2⁶³−18). -/

/-- The staged quotient on the witness inputs. -/
lemma divQuotient128_towards_zero_witness :
    divQuotient128 (1000735865998746448 : UInt64) (1085000000000003547 : UInt64) 0 0
      = (9223372036854775819999, -22, true) := by
  decide

local syntax "dsd_step" : tactic
local macro_rules | `(tactic| dsd_step) => `(tactic|
  (conv_lhs => rw [doNormalize_scaleDown128]; rw [dif_pos (by decide), if_neg (by decide)]; rfl))

/-- `doNormalize_scaleDown128` trace: three drops of `9`. -/
lemma doNormalize_scaleDown128_towards_zero_witness :
    doNormalize_scaleDown128 largeRange.max (9223372036854775819999 : UInt128) (-22)
        { digits_ := 0, xbit_ := true, sbit_ := false }
      = .ok (9223372036854775819, -19,
             { digits_ := 0x9990000000000000, xbit_ := true, sbit_ := false }) := by
  dsd_step; dsd_step; dsd_step
  conv_lhs => rw [doNormalize_scaleDown128]; rw [dif_neg (by decide)]
  rfl

/-- `doNormalize128` trace on the staged quotient. -/
lemma doNormalize128_towards_zero_witness :
    doNormalize128 false (9223372036854775819999 : UInt128) (-22)
        largeRange.min largeRange.max .towards_zero true
      = .ok ⟨false, 9223372036854775810, -19⟩ := by
  unfold doNormalize128
  rw [show ((9223372036854775819999 : UInt128) == 0) = false from by decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize128.scaleUp largeRange.min (9223372036854775819999 : UInt128) (-22)
        = ((9223372036854775819999 : UInt128), (-22 : Int)) from by
      rw [doNormalize128.scaleUp.eq_def]; rw [if_neg (by decide)]]
  simp only [if_true]
  rw [show (Guard.new.set_sticky : Guard)
        = { digits_ := 0, xbit_ := true, sbit_ := false } from rfl]
  rw [doNormalize_scaleDown128_towards_zero_witness]
  simp only []
  rw [show (decide ((-19 : Int) < minExponent)
        || decide ((9223372036854775819 : UInt128) < toUInt128 largeRange.min)) = false from by
      decide]
  simp only [Bool.false_eq_true, if_false]
  rw [show doNormalize_capAtMaxRep (toUInt64 (9223372036854775819 : UInt128)) (-19)
        { digits_ := 0x9990000000000000, xbit_ := true, sbit_ := false }
        = .ok ((922337203685477581 : UInt64), (-18 : Int),
               { digits_ := 0x9999000000000000, xbit_ := true, sbit_ := false }) from by
      unfold doNormalize_capAtMaxRep
      rw [if_pos (by decide)]
      rfl]
  simp only []
  rw [show ({ digits_ := 0x9999000000000000, xbit_ := true, sbit_ := false } : Guard).doRoundUp
        false (922337203685477581 : UInt64) (-18) largeRange.min largeRange.max .towards_zero
        "Number::normalize 2"
        = .ok { negative_ := false, mantissa_ := 9223372036854775810, exponent_ := -19 } from by
      unfold Guard.doRoundUp Guard.bringIntoRange Guard.round Guard.doDropDigit
      rfl]
  simp only [RoundResult.toNumber]

/-- `operator_div` trace for the `.towards_zero` witness inputs. -/
lemma operator_div_towards_zero_witness :
    Number.operator_div ⟨false, 1000735865998746448, 0⟩ ⟨false, 1085000000000003547, 0⟩
        .towards_zero = .ok ⟨false, 9223372036854775810, -19⟩ := by
  unfold Number.operator_div
  rw [if_neg (by decide), if_neg (by decide)]
  change (match divQuotient128 (1000735865998746448 : UInt64) (1085000000000003547 : UInt64) 0 0 with
        | (zm128, ze, dropped) =>
          doNormalize128 false zm128 ze largeRange.min largeRange.max .towards_zero dropped)
      = Except.ok ⟨false, 9223372036854775810, -19⟩
  rw [divQuotient128_towards_zero_witness]
  change doNormalize128 false (9223372036854775819999 : UInt128) (-22)
      largeRange.min largeRange.max .towards_zero true
      = Except.ok ⟨false, 9223372036854775810, -19⟩
  exact doNormalize128_towards_zero_witness


/-- The directed bound is sharp up to its integer numerator: the truncation
loss at the `capAtMaxRep → mantissaFloorSucc` corner exceeds `9/(2^63 − 18)`.
(`10/(2^63 − 18)` is unattainable — the true supremum is `10/(2^63 + 12)`,
one full ulp at `mantissaFloorSucc + 1` — so the provable numerator over this
denominator is at most `9` while the bound holds with `11`.) -/
theorem operator_div_towards_zero_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_div x y .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat / y.toRat| > |x.toRat / y.toRat| * (9 / (2 ^ 63 - 18 : ℚ)) := by
  refine ⟨⟨false, 1000735865998746448, 0⟩, ⟨false, 1085000000000003547, 0⟩,
          ⟨false, 9223372036854775810, -19⟩, ?_, ?_, ?_, ?_, ?_⟩
  · right; refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right; refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · exact operator_div_towards_zero_witness
  · decide
  · have hx_rat : (⟨false, 1000735865998746448, 0⟩ : Number).toRat = 1000735865998746448 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1085000000000003547, 0⟩ : Number).toRat = 1085000000000003547 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775810, -19⟩ : Number).toRat
        = 9223372036854775810 / (10 : ℚ) ^ (19 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    norm_num

end XRPL.Model.Protocol

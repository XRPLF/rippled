import XRPL.Properties.Protocol.Number.Add.Downward.RoundingBound
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Subtraction rounding bound (.downward)

`Number.operator_sub x y mode = Number.operator_add x y.operator_neg mode`, so the
subtraction bound follows from the corresponding addition bound applied to `(x, -y)`. -/

private lemma neg_neg_of_mant_ne {y : Number} (hy_mant_ne : y.mantissa_ ≠ 0) :
    y.operator_neg.operator_neg = y := by
  obtain ⟨yn, ym, ye⟩ := y
  have hy_mant_ne' : ym ≠ 0 := hy_mant_ne
  have hbool : (ym == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hy_mant_ne'
  unfold Number.operator_neg
  simp only [hbool, Bool.false_eq_true, if_false]
  cases yn <;> rfl

private lemma sub_eq_add_neg_toRat (x y : Number) :
    x.toRat - y.toRat = x.toRat + y.operator_neg.toRat := by
  rw [Number.toRat_neg]; ring

theorem operator_sub_rounding_bound_downward_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat - y.toRat)|
      < |x.toRat - y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounding_bound_downward_tight x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_not_zero hok hresult

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.Number.Add.RoundsWithin
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas


namespace XRPL.Model.Protocol

theorem operator_sub_rounding_bound_downward_tight_proof (x y result : Number)
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

theorem operator_sub_rounds_diff_sign_downward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (hok : Number.operator_sub x y .downward = .ok result) :
    RoundsWithin result (x.toRat - y.toRat) .downward (10 / (2 ^ 63 + 2 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_same_sign : x.negative_ = y.operator_neg.negative_ := by
    rw [Number.operator_neg_negative_of_ne y hy_mant_ne]
    cases hxn : x.negative_ <;> cases hyn : y.negative_
    · exact absurd (hxn.trans hyn.symm) h_diff_sign
    · rfl
    · rfl
    · exact absurd (hxn.trans hyn.symm) h_diff_sign
  have h_not_eq : ¬ x.operator_eq y := by
    intro h
    unfold Number.operator_eq at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    exact h_diff_sign h.1.1
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounds_same_sign_downward x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_same_sign h_not_zero hok

theorem operator_sub_rounds_downward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .downward (11 / (2 ^ 63 - 18 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounds_downward x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_not_zero hok hresult

theorem operator_sub_rounding_bound_upward_tight_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .upward = .ok result)
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
  exact operator_add_rounding_bound_upward_tight x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_not_zero hok hresult

theorem operator_sub_rounds_diff_sign_upward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (hok : Number.operator_sub x y .upward = .ok result) :
    RoundsWithin result (x.toRat - y.toRat) .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_same_sign : x.negative_ = y.operator_neg.negative_ := by
    rw [Number.operator_neg_negative_of_ne y hy_mant_ne]
    cases hxn : x.negative_ <;> cases hyn : y.negative_
    · exact absurd (hxn.trans hyn.symm) h_diff_sign
    · rfl
    · rfl
    · exact absurd (hxn.trans hyn.symm) h_diff_sign
  have h_not_eq : ¬ x.operator_eq y := by
    intro h
    unfold Number.operator_eq at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    exact h_diff_sign h.1.1
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounds_same_sign_upward x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_same_sign h_not_zero hok

theorem operator_sub_rounds_upward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .upward (11 / (2 ^ 63 - 18 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounds_upward x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_not_zero hok hresult

theorem operator_sub_rounding_bound_towards_zero_tight_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .towards_zero = .ok result)
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
  exact operator_add_rounding_bound_towards_zero_tight x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_not_zero hok hresult

theorem operator_sub_rounds_diff_sign_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (hok : Number.operator_sub x y .towards_zero = .ok result) :
    RoundsWithin result (x.toRat - y.toRat) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_same_sign : x.negative_ = y.operator_neg.negative_ := by
    rw [Number.operator_neg_negative_of_ne y hy_mant_ne]
    cases hxn : x.negative_ <;> cases hyn : y.negative_
    · exact absurd (hxn.trans hyn.symm) h_diff_sign
    · rfl
    · rfl
    · exact absurd (hxn.trans hyn.symm) h_diff_sign
  have h_not_eq : ¬ x.operator_eq y := by
    intro h
    unfold Number.operator_eq at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    exact h_diff_sign h.1.1
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounds_same_sign_towards_zero x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_same_sign h_not_zero hok

theorem operator_sub_rounds_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .towards_zero (11 / (2 ^ 63 - 18 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounds_towards_zero x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_not_zero hok hresult

theorem operator_sub_rounding_bound_to_nearest_tight_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat - y.toRat)|
      ≤ |x.toRat - y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have hy_neg_mant : y.operator_neg.mantissa_ ≠ 0 := by
    rw [Number.operator_neg_mantissa_of_ne y hy_mant_ne]; exact hy_mant_ne
  have h_not_zero : ¬ x.operator_eq y.operator_neg.operator_neg := by
    rw [neg_neg_of_mant_ne hy_mant_ne]; exact h_not_eq
  rw [sub_eq_add_neg_toRat]
  exact operator_add_rounding_bound_to_nearest_tight x y.operator_neg result hx hy_neg_norm
    hx_mant_ne hy_neg_mant h_not_zero hok hresult

theorem operator_sub_rounds_to_nearest_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_eq : ¬ x.operator_eq y)
    (hok : Number.operator_sub x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat - y.toRat) .to_nearest (6 / (2 ^ 63 - 3 : ℚ)) :=
  operator_sub_rounding_bound_to_nearest_tight_proof x y result hx hy hx_mant_ne hy_mant_ne
    h_not_eq hok hresult

end XRPL.Model.Protocol

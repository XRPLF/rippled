import XRPL.Properties.Protocol.STAmount.Add.Common.DirectedSupport

/-! # Proof bodies for the IOU addition discrete/ULP (`RoundsToRepresentableWithin`)
headlines. The thin headlines live in `Add.RoundsToRepresentable`. -/

namespace XRPL.Model.Protocol

/-- Proof of `operator_add_repr_iou` (IOU addition within 1 ULP, `to_nearest`). -/
theorem STAmount.operator_add_repr_iou_proof (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 .to_nearest = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat + v2.toRat) 1 .to_nearest := by
  obtain ⟨xn, yn, sum, sumI, hrv, hexp_br, hofn, hsumI_ne, h_lo, h_hi, he_lo, he_hi,
      hxn_val, hyn_val, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel, hsum_ne, hadd⟩ :=
    STAmount.operator_add_iou_decompose_anyMode v1 v2 result iss .to_nearest hv1 h_xrp hc1 hc2
      h_truth_ne hok hresult
  have hexp : sum.exponent_ + 3 ≤ result.exponent := by
    rw [hexp_br]; exact IOUAmount.ofNumber_exp_ge sum .to_nearest sumI h_lo h_hi he_lo he_hi hofn hsumI_ne
  have hsnap : |result.toRat - sum.toRat| ≤ (1 / 2) * (10 : ℚ) ^ result.exponent := by
    rw [hrv]
    refine le_trans (IOUAmount.ofNumber_within_half_ulp sum sumI h_lo h_hi he_lo he_hi hofn hsumI_ne) ?_
    exact mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) hexp) (by norm_num)
  have hr_ulp : |sum.toRat| ≤ 10 ^ 16 * (10 : ℚ) ^ result.exponent := by
    rw [abs_toRat_eq sum]
    have hkey : (sum.mantissa_.toNat : ℚ) * (10 : ℚ) ^ sum.exponent_
        ≤ 10 ^ 16 * (10 : ℚ) ^ (sum.exponent_ + 3) := by
      rw [show (10 : ℚ) ^ (sum.exponent_ + 3) = (10 : ℚ) ^ sum.exponent_ * (10 : ℚ) ^ (3 : ℤ) by
            rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)],
          show (10 : ℚ) ^ 16 * ((10 : ℚ) ^ sum.exponent_ * (10 : ℚ) ^ (3 : ℤ))
            = ((10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ)) * (10 : ℚ) ^ sum.exponent_ by ring,
          show (10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ) = (10 : ℚ) ^ 19 by norm_num]
      gcongr
      exact_mod_cast le_of_lt h_hi
    exact le_trans hkey (mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) hexp)
      (by positivity))
  have hop : |sum.toRat - (v1.toRat + v2.toRat)|
      ≤ |v1.toRat + v2.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
    have hb := operator_add_RoundsWithin_anyMode xn yn sum .to_nearest hxn_norm hyn_norm
      hxn_ne hyn_ne h_no_cancel hsum_ne hadd
    simp only [RoundsWithin] at hb
    rw [hxn_val, hyn_val, show RatValued.toRat sum = sum.toRat from rfl] at hb
    exact hb
  exact STAmount.RoundsToRepresentableWithin_of_double_round result sum (v1.toRat + v2.toRat)
    .to_nearest 1 (1 / 2) (11 / (2 ^ 63 - 18 : ℚ)) trivial hsnap hr_ulp hop (by positivity)
    (by norm_num) (by norm_num)

/-- Proof of `operator_add_repr_iou_directed` (IOU addition within 2 ULP, directed modes,
any sign). -/
theorem STAmount.operator_add_repr_iou_directed_proof (v1 v2 result : STAmount) (iss : Issue)
    (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat + v2.toRat) 2 mode := by
  obtain ⟨xn, yn, sum, sumI, hrv, hexp_br, hofn, hsumI_ne, h_lo, h_hi, he_lo, he_hi,
      hxn_val, hyn_val, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel, hsum_ne, hadd⟩ :=
    STAmount.operator_add_iou_decompose_anyMode v1 v2 result iss mode hv1 h_xrp hc1 hc2
      h_truth_ne hok hresult
  have hb := operator_add_RoundsWithin_anyMode xn yn sum mode hxn_norm hyn_norm hxn_ne hyn_ne
    h_no_cancel hsum_ne hadd
  rw [hxn_val, hyn_val] at hb
  have hop : |sum.toRat - (v1.toRat + v2.toRat)|
      ≤ |v1.toRat + v2.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
    have h := operator_add_abs_diff_le_anyMode xn yn sum mode hxn_norm hyn_norm hxn_ne hyn_ne
      h_no_cancel hsum_ne hadd
    rwa [hxn_val, hyn_val] at h
  have hexp : sum.exponent_ + 3 ≤ result.exponent := by
    rw [hexp_br]; exact IOUAmount.ofNumber_exp_ge sum mode sumI h_lo h_hi he_lo he_hi hofn hsumI_ne
  have hsnap : |result.toRat - sum.toRat| ≤ 1 * (10 : ℚ) ^ result.exponent := by
    rw [one_mul, hrv]
    exact le_trans (IOUAmount.ofNumber_within_ulp sum mode sumI h_lo h_hi he_lo he_hi hofn hsumI_ne)
      (zpow_le_zpow_right₀ (by norm_num) hexp)
  have hr_ulp : |sum.toRat| ≤ 10 ^ 16 * (10 : ℚ) ^ result.exponent := by
    rw [abs_toRat_eq sum]
    have hkey : (sum.mantissa_.toNat : ℚ) * (10 : ℚ) ^ sum.exponent_
        ≤ 10 ^ 16 * (10 : ℚ) ^ (sum.exponent_ + 3) := by
      rw [show (10 : ℚ) ^ (sum.exponent_ + 3) = (10 : ℚ) ^ sum.exponent_ * (10 : ℚ) ^ (3 : ℤ) by
            rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)],
          show (10 : ℚ) ^ 16 * ((10 : ℚ) ^ sum.exponent_ * (10 : ℚ) ^ (3 : ℤ))
            = ((10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ)) * (10 : ℚ) ^ sum.exponent_ by ring,
          show (10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ) = (10 : ℚ) ^ 19 by norm_num]
      gcongr
      exact_mod_cast le_of_lt h_hi
    exact le_trans hkey (mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) hexp)
      (by positivity))
  have hsnap_dir : RoundsWithin result sum.toRat mode ((10 : ℚ) ^ (-15 : ℤ)) :=
    RoundsWithin_toRat_congr result sumI sum.toRat _ mode hrv
      (IOUAmount.ofNumber_rounds_within sum mode sumI h_lo h_hi he_lo he_hi hofn hsumI_ne)
  exact STAmount.RoundsToRepresentableWithin_of_double_round result sum (v1.toRat + v2.toRat)
    mode 2 1 (11 / (2 ^ 63 - 18 : ℚ))
    (by cases mode <;>
      first
      | trivial
      | exact le_trans hsnap_dir.1 hb.1
      | exact le_trans hb.1 hsnap_dir.1)
    hsnap hr_ulp hop (by positivity) (by norm_num) (by norm_num)

end XRPL.Model.Protocol

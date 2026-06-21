import XRPL.Properties.Protocol.STAmount.Mul.Common.IOU
import XRPL.Properties.Protocol.STAmount.Add.Common.DirectedSupport
import XRPL.Properties.Protocol.Number.Mul.RoundsWithin
import XRPL.Properties.Protocol.Number.Mul.Common.Decompose

/-! # Directed-mode support for the IOU `multiply` `RoundsWithin` headlines

Directed-mode (`downward`/`upward`/`towards_zero`) versions for IOU multiplication of
**non-negative** operands. (Non-negativity matches the native/MPT `multiply` headlines
and avoids the magnitude-based sign flip in `STAmount.ofNumber`, which rounds `|r|`.)
`Number`-multiply bound `10/(2⁶³+2)` composed with the 16-digit re-round `10⁻¹⁵`. -/

namespace XRPL.Model.Protocol

lemma STAmount.ofNumber_iou_rounds_within (asset : Asset) (r : Number) (mode : rounding_mode)
    (result : STAmount)
    (h_iou : asset.holdsIssue = true) (h_not_xrp : asset.isNative = false)
    (h_nonneg : r.negative_ = false)
    (hr_lo : 10 ^ 18 ≤ r.mantissa_.toNat) (hr_hi : r.mantissa_.toNat < 10 ^ 19)
    (hre_lo : minExponent ≤ r.exponent_) (hre_hi : r.exponent_ + 4 ≤ maxExponent)
    (hok : STAmount.ofNumber asset r mode = .ok result) (hresult : result.mValue ≠ 0) :
    RoundsWithin result r.toRat mode ((10 : ℚ) ^ (-15 : ℤ)) := by
  have hr_ne : r.mantissa_ ≠ 0 := by
    intro h; rw [h] at hr_lo; simp at hr_lo
  have hmne : (r.mantissa_ != 0) = true := by simp [hr_ne]
  have hneg_eq : decide (r.signum < 0) = r.negative_ := by
    unfold Number.signum
    rcases hrn : r.negative_ with _ | _ <;> simp only [hmne, if_true, if_false,
      Bool.false_eq_true] <;> decide
  -- `neg`/`working` as in `ofNumber`; `working = |r|` is non-negative, same magnitude/exponent.
  set neg : Bool := decide (r.signum < 0) with hneg_def
  set working : Number := if neg then r.operator_neg else r with hw_def
  have hw_mant : working.mantissa_ = r.mantissa_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp [Number.operator_neg_mantissa_of_ne r hr_ne]
  have hw_exp : working.exponent_ = r.exponent_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp only [if_true]; unfold Number.operator_neg
      rw [if_neg (by simpa using hr_ne)]
  have hw_neg : working.negative_ = false := by
    rw [hw_def, hneg_eq]
    by_cases hrn : r.negative_ = true
    · rw [if_pos hrn, Number.operator_neg_negative_of_ne r hr_ne]; simp [hrn]
    · rw [if_neg hrn]; simpa using hrn
  have hw_lo : 10 ^ 18 ≤ working.mantissa_.toNat := by rw [hw_mant]; exact hr_lo
  have hw_hi : working.mantissa_.toNat < 10 ^ 19 := by rw [hw_mant]; exact hr_hi
  have hwe_lo : minExponent ≤ working.exponent_ + 3 := by rw [hw_exp]; omega
  have hwe_hi : working.exponent_ + 4 ≤ maxExponent := by rw [hw_exp]; exact hre_hi
  -- `working.toRat = |r.toRat|`, and `r.toRat = (if neg then -1 else 1) * working.toRat`.
  have hw_toRat_nonneg : 0 ≤ working.toRat := by
    rw [Number.toRat_of_nonneg working hw_neg]; positivity
  have hr_toRat : r.toRat = (if neg then (-1 : ℚ) else 1) * working.toRat := by
    rw [hw_def]
    by_cases hn : neg = true
    · rw [if_pos hn, if_pos hn, Number.toRat_neg]; ring
    · rw [if_neg hn, if_neg hn]; ring
  -- `asset` is non-integral, so `ofNumber` reduces to `checked` on the `normalizeToRange` output.
  have h_asset_int : asset.integral = false := by
    rcases ha : asset with i | m
    · rw [ha] at h_not_xrp; exact h_not_xrp
    · rw [ha] at h_iou; exact Bool.noConfusion h_iou
  unfold STAmount.ofNumber at hok
  rw [if_neg (by rw [h_asset_int]; decide), ← hneg_def, ← hw_def] at hok
  cases hnorm : working.normalizeToRange kMinValue kMaxValue mode with
  | error e => rw [hnorm] at hok; exact absurd hok (by simp)
  | ok me =>
    obtain ⟨mant, exp⟩ := me
    rw [hnorm] at hok
    simp only at hok
    -- keystone facts (kMinValue/kMaxValue ≡ cMinValue/cMaxValue).
    have hnorm' : working.normalizeToRange cMinValue cMaxValue mode = .ok (mant, exp) := hnorm
    have hbound := normalizeToRange_16_within_ulp working mode mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hmrange := normalizeToRange_16_mantissa_range working mode mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have herange := normalizeToRange_16_exp_range working mode mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
    have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
    -- `mant ≥ 0`: it is within one ULP of the (large, non-negative) `working.toRat`.
    have hwlarge : (10 : ℚ) ^ (working.exponent_ + 18) ≤ working.toRat := by
      rw [Number.toRat_of_nonneg working hw_neg]
      have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (working.mantissa_.toNat : ℚ) := by
        have : ((10 ^ 18 : ℕ) : ℚ) ≤ (working.mantissa_.toNat : ℚ) := by exact_mod_cast hw_lo
        rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
      calc (10 : ℚ) ^ (working.exponent_ + 18)
          = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ working.exponent_ := by
            rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
        _ ≤ (working.mantissa_.toNat : ℚ) * (10 : ℚ) ^ working.exponent_ := by gcongr
    have hmant_pos : 0 ≤ mant.toInt := by
      by_contra hc
      push_neg at hc
      have hmant_neg_q : (mant.toInt : ℚ) < 0 := by exact_mod_cast hc
      have hval_neg : (mant.toInt : ℚ) * 10 ^ exp < 0 :=
        mul_neg_of_neg_of_pos hmant_neg_q (zpow_pos (by norm_num) _)
      have hb := abs_le.mp hbound
      have hule : (10 : ℚ) ^ (working.exponent_ + 3) ≤ (10 : ℚ) ^ (working.exponent_ + 18) :=
        zpow_le_zpow_right₀ (by norm_num) (by omega)
      have : working.toRat ≤ (mant.toInt : ℚ) * 10 ^ exp + 10 ^ (working.exponent_ + 3) := by
        linarith [hb.1]
      nlinarith [hwlarge, hule, hval_neg]
    have hmant_natAbs : mant.toInt.natAbs = mant.toUInt64.toNat := by
      have := toUInt64_toNat_of_nonneg mant hmant_pos
      omega
    -- `checked` reproduces the record (exponent forced in range by `hok`/`hresult`).
    have hmtu_lo : 10 ^ 15 ≤ mant.toUInt64.toNat := by
      have := hmrange.1; rw [hcMin] at this; omega
    have hmtu_hi : mant.toUInt64.toNat < 10 ^ 16 := by
      have := hmrange.2; rw [hcMax] at this; omega
    have hcc := STAmount.checked_iou_cases asset mant.toUInt64 exp neg mode h_iou h_not_xrp
      hmtu_lo hmtu_hi (by rw [hw_exp] at herange; omega) (by rw [hw_exp] at herange; omega) result hok hresult
    obtain ⟨_, _, hres_eq⟩ := hcc
    -- value transport.
    have hres_toRat : result.toRat = (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp) := by
      rw [hres_eq, STAmount.toRat_signed]
      show (if neg then (-1 : ℚ) else 1) * (mant.toUInt64.toNat : ℚ) * 10 ^ exp = _
      have : (mant.toUInt64.toNat : ℚ) = (mant.toInt : ℚ) := by
        exact_mod_cast toUInt64_toNat_of_nonneg mant hmant_pos
      rw [this]; ring
    -- non-negativity ⟹ neg = false, so the result tracks `working = r` with no sign flip
    have hneg_false : neg = false := by
      rw [hneg_def]; unfold Number.signum; rw [h_nonneg]
      simp only [hmne, if_true]; decide
    have hwr : working.toRat = r.toRat := by rw [hr_toRat, hneg_false]; simp
    have hres' : result.toRat = (mant.toInt : ℚ) * 10 ^ exp := by rw [hres_toRat, hneg_false]; simp
    have hmag : |result.toRat - r.toRat| ≤ (10 : ℚ) ^ (r.exponent_ + 3) := by
      rw [hres', ← hwr, ← hw_exp]; exact hbound
    have hmag' : |result.toRat - r.toRat| ≤ |r.toRat| * (10 : ℚ) ^ (-15 : ℤ) := by
      refine le_trans hmag ?_
      rw [show |r.toRat| = (r.mantissa_.toNat : ℚ) * 10 ^ r.exponent_ by
            rw [Number.toRat_of_nonneg r h_nonneg, abs_of_nonneg (by positivity)]]
      have e2 : (10 : ℚ) ^ (r.exponent_ + 3) = 10 ^ (18 : ℤ) * 10 ^ (r.exponent_ - 15) := by
        rw [show (r.exponent_ + 3 : ℤ) = 18 + (r.exponent_ - 15) by ring, zpow_add₀ (by norm_num : (10:ℚ) ≠ 0)]
      have e1 : (r.mantissa_.toNat : ℚ) * 10 ^ r.exponent_ * 10 ^ (-15 : ℤ)
          = (r.mantissa_.toNat : ℚ) * 10 ^ (r.exponent_ - 15) := by
        rw [mul_assoc, ← zpow_add₀ (by norm_num : (10:ℚ) ≠ 0)]; ring_nf
      rw [e1, e2]
      have hM : (10 : ℚ) ^ (18 : ℤ) ≤ (r.mantissa_.toNat : ℚ) := by
        rw [show (10:ℚ)^(18:ℤ) = (10:ℚ)^(18:ℕ) by norm_num]; exact_mod_cast hr_lo
      gcongr
    have habs2 := abs_le.mp hmag'
    have htr : (RatValued.toRat result : ℚ) = result.toRat := rfl
    -- the re-round of `working = r` is correctly directed (working is non-negative)
    have hnorm_r : r.normalizeToRange cMinValue cMaxValue mode = .ok (mant, exp) := by
      have hwr_eq : working = r := by
        have : working = if neg then r.operator_neg else r := hw_def
        rw [this, hneg_false]; simp
      rw [← hwr_eq]; exact hnorm'
    unfold RoundsWithin
    cases mode with
    | to_nearest => rw [htr]; exact hmag'
    | downward =>
      rw [htr]
      exact ⟨by rw [hres']; exact normalizeToRange_16_downward r mant exp hr_lo hr_hi (by omega) hre_hi hnorm_r,
             by linarith [habs2.1]⟩
    | upward =>
      rw [htr]
      exact ⟨by rw [hres']; exact normalizeToRange_16_upward r mant exp hr_lo hr_hi (by omega) hre_hi hnorm_r,
             by linarith [habs2.2]⟩
    | towards_zero =>
      rw [htr]
      refine ⟨by rw [hres']; exact normalizeToRange_16_towards_zero r mant exp hr_lo hr_hi (by omega) hre_hi hnorm_r, ?_⟩
      have hrev : |r.toRat| - |result.toRat| ≤ |result.toRat - r.toRat| := by
        rw [abs_sub_comm result.toRat r.toRat]; exact abs_sub_abs_le_abs_sub r.toRat result.toRat
      linarith [hrev, hmag']

/-- **`towards_zero` snap never increases magnitude, any sign.** `STAmount.ofNumber`
rounds the magnitude `|r|` toward zero, so `|result| ≤ |r|`. (Unlike the *signed*
directional clause of `ofNumber_iou_rounds_within`, this magnitude statement needs no
non-negativity hypothesis.) -/
lemma STAmount.ofNumber_iou_abs_le_towards_zero (asset : Asset) (r : Number) (result : STAmount)
    (h_iou : asset.holdsIssue = true) (h_not_xrp : asset.isNative = false)
    (hr_lo : 10 ^ 18 ≤ r.mantissa_.toNat) (hr_hi : r.mantissa_.toNat < 10 ^ 19)
    (hre_lo : minExponent ≤ r.exponent_) (hre_hi : r.exponent_ + 4 ≤ maxExponent)
    (hok : STAmount.ofNumber asset r .towards_zero = .ok result) (hresult : result.mValue ≠ 0) :
    |result.toRat| ≤ |r.toRat| := by
  have hr_ne : r.mantissa_ ≠ 0 := by intro h; rw [h] at hr_lo; simp at hr_lo
  have hmne : (r.mantissa_ != 0) = true := by simp [hr_ne]
  have hneg_eq : decide (r.signum < 0) = r.negative_ := by
    unfold Number.signum
    rcases hrn : r.negative_ with _ | _ <;> simp only [hmne, if_true, if_false,
      Bool.false_eq_true] <;> decide
  set neg : Bool := decide (r.signum < 0) with hneg_def
  set working : Number := if neg then r.operator_neg else r with hw_def
  have hw_mant : working.mantissa_ = r.mantissa_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp [Number.operator_neg_mantissa_of_ne r hr_ne]
  have hw_exp : working.exponent_ = r.exponent_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp only [if_true]; unfold Number.operator_neg; rw [if_neg (by simpa using hr_ne)]
  have hw_neg : working.negative_ = false := by
    rw [hw_def, hneg_eq]
    by_cases hrn : r.negative_ = true
    · rw [if_pos hrn, Number.operator_neg_negative_of_ne r hr_ne]; simp [hrn]
    · rw [if_neg hrn]; simpa using hrn
  have hw_lo : 10 ^ 18 ≤ working.mantissa_.toNat := by rw [hw_mant]; exact hr_lo
  have hw_hi : working.mantissa_.toNat < 10 ^ 19 := by rw [hw_mant]; exact hr_hi
  have hwe_lo : minExponent ≤ working.exponent_ + 3 := by rw [hw_exp]; omega
  have hwe_hi : working.exponent_ + 4 ≤ maxExponent := by rw [hw_exp]; exact hre_hi
  have hw_toRat_nonneg : 0 ≤ working.toRat := by
    rw [Number.toRat_of_nonneg working hw_neg]; positivity
  have hr_toRat : r.toRat = (if neg then (-1 : ℚ) else 1) * working.toRat := by
    rw [hw_def]
    by_cases hn : neg = true
    · rw [if_pos hn, if_pos hn, Number.toRat_neg]; ring
    · rw [if_neg hn, if_neg hn]; ring
  have h_asset_int : asset.integral = false := by
    rcases ha : asset with i | m
    · rw [ha] at h_not_xrp; exact h_not_xrp
    · rw [ha] at h_iou; exact Bool.noConfusion h_iou
  unfold STAmount.ofNumber at hok
  rw [if_neg (by rw [h_asset_int]; decide), ← hneg_def, ← hw_def] at hok
  cases hnorm : working.normalizeToRange kMinValue kMaxValue .towards_zero with
  | error e => rw [hnorm] at hok; exact absurd hok (by simp)
  | ok me =>
    obtain ⟨mant, exp⟩ := me
    rw [hnorm] at hok
    simp only at hok
    have hnorm' : working.normalizeToRange cMinValue cMaxValue .towards_zero = .ok (mant, exp) := hnorm
    have hbound := normalizeToRange_16_within_ulp working .towards_zero mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hmrange := normalizeToRange_16_mantissa_range working .towards_zero mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have herange := normalizeToRange_16_exp_range working .towards_zero mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
    have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
    have hwlarge : (10 : ℚ) ^ (working.exponent_ + 18) ≤ working.toRat := by
      rw [Number.toRat_of_nonneg working hw_neg]
      have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (working.mantissa_.toNat : ℚ) := by
        have : ((10 ^ 18 : ℕ) : ℚ) ≤ (working.mantissa_.toNat : ℚ) := by exact_mod_cast hw_lo
        rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
      calc (10 : ℚ) ^ (working.exponent_ + 18)
          = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ working.exponent_ := by
            rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
        _ ≤ (working.mantissa_.toNat : ℚ) * (10 : ℚ) ^ working.exponent_ := by gcongr
    have hmant_pos : 0 ≤ mant.toInt := by
      by_contra hc
      push_neg at hc
      have hmant_neg_q : (mant.toInt : ℚ) < 0 := by exact_mod_cast hc
      have hval_neg : (mant.toInt : ℚ) * 10 ^ exp < 0 :=
        mul_neg_of_neg_of_pos hmant_neg_q (zpow_pos (by norm_num) _)
      have hb := abs_le.mp hbound
      have hule : (10 : ℚ) ^ (working.exponent_ + 3) ≤ (10 : ℚ) ^ (working.exponent_ + 18) :=
        zpow_le_zpow_right₀ (by norm_num) (by omega)
      have : working.toRat ≤ (mant.toInt : ℚ) * 10 ^ exp + 10 ^ (working.exponent_ + 3) := by
        linarith [hb.1]
      nlinarith [hwlarge, hule, hval_neg]
    have hmant_natAbs : mant.toInt.natAbs = mant.toUInt64.toNat := by
      have := toUInt64_toNat_of_nonneg mant hmant_pos; omega
    have hmtu_lo : 10 ^ 15 ≤ mant.toUInt64.toNat := by
      have := hmrange.1; rw [hcMin] at this; omega
    have hmtu_hi : mant.toUInt64.toNat < 10 ^ 16 := by
      have := hmrange.2; rw [hcMax] at this; omega
    have hcc := STAmount.checked_iou_cases asset mant.toUInt64 exp neg .towards_zero h_iou h_not_xrp
      hmtu_lo hmtu_hi (by rw [hw_exp] at herange; omega) (by rw [hw_exp] at herange; omega) result hok hresult
    obtain ⟨_, _, hres_eq⟩ := hcc
    have hres_toRat : result.toRat = (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp) := by
      rw [hres_eq, STAmount.toRat_signed]
      show (if neg then (-1 : ℚ) else 1) * (mant.toUInt64.toNat : ℚ) * 10 ^ exp = _
      have : (mant.toUInt64.toNat : ℚ) = (mant.toInt : ℚ) := by
        exact_mod_cast toUInt64_toNat_of_nonneg mant hmant_pos
      rw [this]; ring
    have hmono := normalizeToRange_16_towards_zero working mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hwabs : |working.toRat| = |r.toRat| := by
      rw [hr_toRat, abs_mul]; rcases hn : neg with _ | _ <;> simp
    have hres_abs : |result.toRat| = |(mant.toInt : ℚ) * 10 ^ exp| := by
      rw [hres_toRat, abs_mul]; rcases hn : neg with _ | _ <;> simp
    rw [hres_abs, ← hwabs]; exact hmono

/-- The `Number`-multiply result sign is the XOR of the operand signs (any mode). -/
theorem operator_mul_negative_eq (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_ne : x.mantissa_ ≠ 0) (hy_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y mode = .ok result) (hresult : result.mantissa_ ≠ 0) :
    result.negative_ = (x.negative_ != y.negative_) := by
  obtain ⟨zn, zm, ze', f, g, res, d⟩ :=
    operator_mul_decompose x y result mode hx hy hx_ne hy_ne hok
  have hzm_ge : zm.toNat ≥ mantissaFloor := by have := d.zm_ge_floorSucc; omega
  have hres_mant_ne : res.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result d.normalizes hresult
  obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ :=
    doRoundUp_output_invariants_upTo_maxRepUp_anyMode g zn zm ze' mode hzm_ge d.zm_le_maxRepUp
      "Number::multiplication overflow" res d.rounds hres_mant_ne
  have hres_eq : result = res.toNumber :=
    Number.normalize_eq_of_invariants h_res_min h_res_max h_res_exp h_res_mod d.normalizes
  have hres_neg : res.negative_ = zn :=
    doRoundUp_negative_of_mant_ne g zn zm ze' largeRange.min largeRange.max mode
      "Number::multiplication overflow" res d.rounds hres_mant_ne
  rw [hres_eq, show res.toNumber.negative_ = res.negative_ from rfl, hres_neg]
  exact d.neg_eq

/-- `Number`-multiply relative-error bound, uniform across modes at the looser directed
value `10/(2⁶³+2)` (`to_nearest` is tighter, lifted by monotonicity). -/
lemma operator_mul_RoundsWithin_anyMode (n1 n2 r : Number) (mode : rounding_mode)
    (hn1_norm : n1.isNormalized) (hn2_norm : n2.isNormalized) (hr_ne : r.mantissa_ ≠ 0)
    (hmul : Number.operator_mul n1 n2 mode = .ok r) :
    RoundsWithin r (n1.toRat * n2.toRat) mode (10 / (2 ^ 63 + 2 : ℚ)) := by
  cases mode with
  | to_nearest =>
    exact RoundsWithin_mono r (n1.toRat * n2.toRat) (5 / (2 ^ 63 + 7 : ℚ)) (10 / (2 ^ 63 + 2 : ℚ))
      .to_nearest (operator_mul_rounds_to_nearest n1 n2 r hn1_norm hn2_norm hmul hr_ne) (by norm_num)
  | downward => exact operator_mul_rounds_downward n1 n2 r hn1_norm hn2_norm hmul hr_ne
  | upward => exact operator_mul_rounds_upward n1 n2 r hn1_norm hn2_norm hmul hr_ne
  | towards_zero => exact operator_mul_rounds_towards_zero n1 n2 r hn1_norm hn2_norm hmul hr_ne

/-- **Magnitude form of the `Number`-multiply directed bound, every mode, any operand
signs.** For `to_nearest`/`downward`/`upward` it is the `RoundsWithin` magnitude directly;
for `towards_zero` the product `r` is sign-aligned with `n1·n2` (its sign is the XOR of
the operand signs), so `|r − n1·n2| = |n1·n2| − |r|`. -/
lemma operator_mul_abs_diff_le_anyMode (n1 n2 r : Number) (mode : rounding_mode)
    (hn1_norm : n1.isNormalized) (hn2_norm : n2.isNormalized)
    (hn1_ne : n1.mantissa_ ≠ 0) (hn2_ne : n2.mantissa_ ≠ 0) (hr_ne : r.mantissa_ ≠ 0)
    (hmul : Number.operator_mul n1 n2 mode = .ok r) :
    |r.toRat - n1.toRat * n2.toRat| ≤ |n1.toRat * n2.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hrw := operator_mul_RoundsWithin_anyMode n1 n2 r mode hn1_norm hn2_norm hr_ne hmul
  unfold RoundsWithin at hrw
  cases mode with
  | to_nearest => exact hrw
  | downward =>
    obtain ⟨hle, hm⟩ := hrw
    rw [show RatValued.toRat r = r.toRat from rfl] at hle hm
    rw [abs_of_nonpos (by linarith), neg_sub]; exact hm
  | upward =>
    obtain ⟨hge, hm⟩ := hrw
    rw [show RatValued.toRat r = r.toRat from rfl] at hge hm
    rw [abs_of_nonneg (by linarith)]; exact hm
  | towards_zero =>
    obtain ⟨hle_abs, hm_abs⟩ := hrw
    rw [show RatValued.toRat r = r.toRat from rfl] at hle_abs hm_abs
    have hrneg : r.negative_ = (n1.negative_ != n2.negative_) :=
      operator_mul_negative_eq n1 n2 r .towards_zero hn1_norm hn2_norm hn1_ne hn2_ne hmul hr_ne
    rw [abs_diff_eq_abs_sub_abs_of_sign_aligned r (n1.toRat * n2.toRat)
      (fun hneg => (toRat_mul_sign n1 n2).2 (by rw [hrneg] at hneg; exact bne_iff_ne.mp hneg))
      (fun hpos => (toRat_mul_sign n1 n2).1 (by
        rw [hrneg] at hpos
        cases h1 : n1.negative_ <;> cases h2 : n2.negative_ <;> simp_all))]
    rw [abs_of_nonpos (by linarith), neg_sub]; exact hm_abs

lemma operator_mul_exponent_hi_anyMode (n1 n2 r : Number) (E : Int) (mode : rounding_mode)
    (hn1_norm : n1.isNormalized) (hn2_norm : n2.isNormalized)
    (hn1_ne : n1.mantissa_ ≠ 0) (hn2_ne : n2.mantissa_ ≠ 0) (hr_ne : r.mantissa_ ≠ 0)
    (hn1e : n1.exponent_ ≤ E) (hn2e : n2.exponent_ ≤ E) (hE : 2 * E + 24 ≤ maxExponent)
    (hmul : Number.operator_mul n1 n2 mode = .ok r) :
    r.exponent_ + 4 ≤ maxExponent := by
  by_contra hcon
  push_neg at hcon
  have hr_norm : r.isNormalized := operator_mul_result_isNormalized n1 n2 r mode
    hn1_norm hn2_norm hn1_ne hn2_ne hmul hr_ne
  have hr_mant := hr_norm.mantissaBounds_nat hr_ne
  -- lower bound: 10^(maxExponent+15) ≤ |r.toRat|.
  have hlower : (10 : ℚ) ^ (maxExponent + 15) ≤ |r.toRat| := by
    rw [abs_toRat_eq r]
    have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (r.mantissa_.toNat : ℚ) := by
      have : ((10 ^ 18 : ℕ) : ℚ) ≤ (r.mantissa_.toNat : ℚ) := by exact_mod_cast hr_mant.1
      rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
    calc (10 : ℚ) ^ (maxExponent + 15)
        ≤ (10 : ℚ) ^ (r.exponent_ + 18) := zpow_le_zpow_right₀ (by norm_num) (by omega)
      _ = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ r.exponent_ := by
          rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
      _ ≤ (r.mantissa_.toNat : ℚ) * (10 : ℚ) ^ r.exponent_ := by gcongr
  -- upper bound: |r.toRat| < 10^(maxExponent+15).
  have hmb := operator_mul_RoundsWithin_anyMode n1 n2 r mode hn1_norm hn2_norm hr_ne hmul
  have hr2 : |r.toRat| ≤ |n1.toRat * n2.toRat| * 2 := by
    have := RoundsWithin_abs_le_two r (n1.toRat * n2.toRat) (10 / (2 ^ 63 + 2 : ℚ)) mode hmb (by norm_num)
    rwa [show RatValued.toRat r = r.toRat from rfl] at this
  have hbound_n : ∀ (n : Number), n.mantissa_.toNat < 10 ^ 19 → n.exponent_ ≤ E →
      |n.toRat| < (10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E := by
    intro n hn_hi hne
    rw [abs_toRat_eq n]
    have hmant : (n.mantissa_.toNat : ℚ) < (10 : ℚ) ^ (19 : ℤ) := by
      have : (n.mantissa_.toNat : ℚ) < ((10 ^ 19 : ℕ) : ℚ) := by exact_mod_cast hn_hi
      rwa [show ((10 ^ 19 : ℕ) : ℚ) = (10 : ℚ) ^ (19 : ℤ) by push_cast; norm_num] at this
    have hexp : (10 : ℚ) ^ n.exponent_ ≤ (10 : ℚ) ^ E := zpow_le_zpow_right₀ (by norm_num) hne
    calc (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        < (10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ n.exponent_ :=
          mul_lt_mul_of_pos_right hmant (zpow_pos (by norm_num) _)
      _ ≤ (10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E := mul_le_mul_of_nonneg_left hexp (by positivity)
  have hn1b := hbound_n n1 (hn1_norm.mantissaBounds_nat hn1_ne).2 hn1e
  have hn2b := hbound_n n2 (hn2_norm.mantissaBounds_nat hn2_ne).2 hn2e
  have hupper : |r.toRat| < (10 : ℚ) ^ (maxExponent + 15) := by
    have hprod : |n1.toRat * n2.toRat| < (10 : ℚ) ^ (2 * E + 38) := by
      rw [abs_mul]
      have hbig : |n1.toRat| * |n2.toRat|
          < ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E) * ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E) :=
        mul_lt_mul'' hn1b hn2b (abs_nonneg _) (abs_nonneg _)
      have hpoweq : ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E) * ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E)
          = (10 : ℚ) ^ (2 * E + 38) := by
        rw [mul_mul_mul_comm, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0),
            ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        congr 1; ring
      rw [hpoweq] at hbig; exact hbig
    have hpos38 : (0 : ℚ) < (10 : ℚ) ^ (2 * E + 38) := zpow_pos (by norm_num) _
    have hfin : (10 : ℚ) ^ (2 * E + 38) * 2 ≤ (10 : ℚ) ^ (maxExponent + 15) := by
      have h39 : (10 : ℚ) ^ (2 * E + 38) * 2 ≤ (10 : ℚ) ^ (2 * E + 38) * 10 := by nlinarith [hpos38]
      have heq : (10 : ℚ) ^ (2 * E + 38) * 10 = (10 : ℚ) ^ (2 * E + 39) := by
        rw [show (2 * E + 39 : ℤ) = (2 * E + 38) + 1 from by ring,
            zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0) (2 * E + 38) 1, zpow_one]
      rw [heq] at h39
      exact le_trans h39 (zpow_le_zpow_right₀ (by norm_num) (by omega))
    calc |r.toRat| ≤ |n1.toRat * n2.toRat| * 2 := hr2
      _ < (10 : ℚ) ^ (2 * E + 38) * 2 := by nlinarith [hprod, hpos38]
      _ ≤ (10 : ℚ) ^ (maxExponent + 15) := hfin
  linarith [hlower, hupper]

/-- **STAmount IOU multiplication of non-negative operands rounds within the directed
relative-error bound, every mode.** `Number`-multiply (`10/(2⁶³+2)`) composed with the
16-digit re-round (`10⁻¹⁵`) via `RoundsWithin_trans`. -/
theorem STAmount.operator_mul_iou_rounds_directed (v1 v2 result : STAmount) (asset : Asset)
    (iss : Issue) (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hn1 : v1.mIsNegative = false) (hn2 : v2.mIsNegative = false)
    (hok : STAmount.multiply v1 v2 asset mode = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat * v2.toRat) mode
      (10 / (2 ^ 63 + 2 : ℚ) + (10 : ℚ) ^ (-15 : ℤ)
        + 10 / (2 ^ 63 + 2 : ℚ) * (10 : ℚ) ^ (-15 : ℤ)) := by
  have hv1ne : v1.mValue ≠ 0 := by intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  have hv2ne : v2.mValue ≠ 0 := by intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1nat : v1.native = false := by
    unfold STAmount.native; rw [hv1]; show iss.native = false; rw [← h_xrp]; rfl
  have hv1mpt : v1.holdsMPTIssue = false := by unfold STAmount.holdsMPTIssue; rw [hv1]; rfl
  unfold STAmount.multiply at hok
  rw [if_neg (show ¬ (v1.isZero || v2.isZero) = true from by simp [STAmount.isZero, hv1ne, hv2ne]),
      if_neg (show ¬ (v1.native && v2.native && asset.isNative) = true from by simp [hv1nat]),
      if_neg (show ¬ (v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue) = true from by simp [hv1mpt]),
      STAmount.toNumber_iou_canonical v1 iss mode hv1 h_xrp hc1,
      STAmount.toNumber_iou_canonical v2 iss mode hv2 h_xrp hc2] at hok
  simp only at hok
  set n1 : Number := ⟨v1.mIsNegative, v1.mValue * 10 * 10 * 10, v1.mOffset - 3⟩ with hn1_def
  set n2 : Number := ⟨v2.mIsNegative, v2.mValue * 10 * 10 * 10, v2.mOffset - 3⟩ with hn2_def
  cases hmul : Number.operator_mul n1 n2 mode with
  | error e => rw [hmul] at hok; simp at hok
  | ok r =>
    rw [hmul] at hok
    simp only at hok
    have hofn : STAmount.ofNumber asset r mode = .ok result := hok
    have hn1_norm : n1.isNormalized := STAmount.toNumber_iou_canonical_isNormalized v1 hc1
    have hn2_norm : n2.isNormalized := STAmount.toNumber_iou_canonical_isNormalized v2 hc2
    have hn1_ne : n1.mantissa_ ≠ 0 := STAmount.toNumber_iou_canonical_mantissa_ne v1 hc1
    have hn2_ne : n2.mantissa_ ≠ 0 := STAmount.toNumber_iou_canonical_mantissa_ne v2 hc2
    have hn1_val : n1.toRat = v1.toRat := STAmount.toNumber_iou_canonical_toRat v1 hc1
    have hn2_val : n2.toRat = v2.toRat := STAmount.toNumber_iou_canonical_toRat v2 hc2
    have hr_ne : r.mantissa_ ≠ 0 :=
      STAmount.ofNumber_iou_mantissa_ne_zero asset r mode result ha_iou ha_not_xrp hofn hresult
    have hr_norm := operator_mul_result_isNormalized n1 n2 r mode hn1_norm hn2_norm hn1_ne hn2_ne hmul hr_ne
    have hr_mant := hr_norm.mantissaBounds_nat hr_ne
    have hr_exp_lo : minExponent ≤ r.exponent_ := by
      rcases hr_norm with hz | ⟨_, _, _, hlo, _⟩
      · exact absurd (show r.mantissa_ = 0 by rw [hz]; rfl) hr_ne
      · exact hlo
    have hr_exp_hi : r.exponent_ + 4 ≤ maxExponent :=
      operator_mul_exponent_hi_anyMode n1 n2 r 77 mode hn1_norm hn2_norm hn1_ne hn2_ne hr_ne
        (by show v1.mOffset - 3 ≤ 77; have := hc1.exp_hi; omega)
        (by show v2.mOffset - 3 ≤ 77; have := hc2.exp_hi; omega) (by unfold maxExponent; omega) hmul
    have hr_neg : r.negative_ = false := by
      rw [operator_mul_negative_eq n1 n2 r mode hn1_norm hn2_norm hn1_ne hn2_ne hmul hr_ne]
      show (v1.mIsNegative != v2.mIsNegative) = false
      rw [hn1, hn2]; rfl
    have hmb := operator_mul_RoundsWithin_anyMode n1 n2 r mode hn1_norm hn2_norm hr_ne hmul
    have hre := STAmount.ofNumber_iou_rounds_within asset r mode result ha_iou ha_not_xrp hr_neg
      hr_mant.1 hr_mant.2 hr_exp_lo hr_exp_hi hofn hresult
    have htrans := RoundsWithin_trans result r (n1.toRat * n2.toRat) (10 / (2 ^ 63 + 2 : ℚ))
      ((10 : ℚ) ^ (-15 : ℤ)) mode hmb
      (by rw [show RatValued.toRat r = r.toRat from rfl]; exact hre) (by positivity) (by positivity)
    rw [hn1_val, hn2_val] at htrans
    exact htrans

/-- **IOU multiply pipeline decomposition (any mode, non-negative operands).** Exposes
the 19-digit `Number` product `r`, its range/sign, and the mode-generic `Number`-multiply
`RoundsWithin` bound. Feeds the directed ULP/repr headlines. -/
lemma STAmount.operator_mul_iou_decompose_anyMode (v1 v2 result : STAmount) (asset : Asset)
    (iss : Issue) (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hn1 : v1.mIsNegative = false) (hn2 : v2.mIsNegative = false)
    (hok : STAmount.multiply v1 v2 asset mode = .ok result) (hresult : result.mValue ≠ 0) :
    ∃ r : Number, STAmount.ofNumber asset r mode = .ok result ∧
      10 ^ 18 ≤ r.mantissa_.toNat ∧ r.mantissa_.toNat < 10 ^ 19 ∧
      minExponent ≤ r.exponent_ ∧ r.exponent_ + 4 ≤ maxExponent ∧ r.negative_ = false ∧
      RoundsWithin r (v1.toRat * v2.toRat) mode (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hv1ne : v1.mValue ≠ 0 := by intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  have hv2ne : v2.mValue ≠ 0 := by intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1nat : v1.native = false := by
    unfold STAmount.native; rw [hv1]; show iss.native = false; rw [← h_xrp]; rfl
  have hv1mpt : v1.holdsMPTIssue = false := by unfold STAmount.holdsMPTIssue; rw [hv1]; rfl
  unfold STAmount.multiply at hok
  rw [if_neg (show ¬ (v1.isZero || v2.isZero) = true from by simp [STAmount.isZero, hv1ne, hv2ne]),
      if_neg (show ¬ (v1.native && v2.native && asset.isNative) = true from by simp [hv1nat]),
      if_neg (show ¬ (v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue) = true from by
        simp [hv1mpt]),
      STAmount.toNumber_iou_canonical v1 iss mode hv1 h_xrp hc1,
      STAmount.toNumber_iou_canonical v2 iss mode hv2 h_xrp hc2] at hok
  simp only at hok
  set n1 : Number := ⟨v1.mIsNegative, v1.mValue * 10 * 10 * 10, v1.mOffset - 3⟩ with hn1_def
  set n2 : Number := ⟨v2.mIsNegative, v2.mValue * 10 * 10 * 10, v2.mOffset - 3⟩ with hn2_def
  cases hmul : Number.operator_mul n1 n2 mode with
  | error e => rw [hmul] at hok; simp at hok
  | ok r =>
    rw [hmul] at hok; simp only at hok
    have hofn : STAmount.ofNumber asset r mode = .ok result := hok
    have hn1_norm := STAmount.toNumber_iou_canonical_isNormalized v1 hc1
    have hn2_norm := STAmount.toNumber_iou_canonical_isNormalized v2 hc2
    have hn1_ne := STAmount.toNumber_iou_canonical_mantissa_ne v1 hc1
    have hn2_ne := STAmount.toNumber_iou_canonical_mantissa_ne v2 hc2
    have hn1_val := STAmount.toNumber_iou_canonical_toRat v1 hc1
    have hn2_val := STAmount.toNumber_iou_canonical_toRat v2 hc2
    have hr_ne := STAmount.ofNumber_iou_mantissa_ne_zero asset r mode result ha_iou ha_not_xrp
      hofn hresult
    have hr_norm := operator_mul_result_isNormalized n1 n2 r mode hn1_norm hn2_norm hn1_ne hn2_ne
      hmul hr_ne
    have hr_mant := hr_norm.mantissaBounds_nat hr_ne
    have hr_exp_lo : minExponent ≤ r.exponent_ := by
      rcases hr_norm with hz | ⟨_, _, _, hlo, _⟩
      · exact absurd (show r.mantissa_ = 0 by rw [hz]; rfl) hr_ne
      · exact hlo
    have hr_exp_hi := operator_mul_exponent_hi_anyMode n1 n2 r 77 mode hn1_norm hn2_norm hn1_ne
      hn2_ne hr_ne (by show v1.mOffset - 3 ≤ 77; have := hc1.exp_hi; omega)
      (by show v2.mOffset - 3 ≤ 77; have := hc2.exp_hi; omega) (by unfold maxExponent; omega) hmul
    have hr_neg : r.negative_ = false := by
      rw [operator_mul_negative_eq n1 n2 r mode hn1_norm hn2_norm hn1_ne hn2_ne hmul hr_ne]
      show (v1.mIsNegative != v2.mIsNegative) = false
      rw [hn1, hn2]; rfl
    have hmb := operator_mul_RoundsWithin_anyMode n1 n2 r mode hn1_norm hn2_norm hr_ne hmul
    rw [hn1_val, hn2_val] at hmb
    exact ⟨r, hofn, hr_mant.1, hr_mant.2, hr_exp_lo, hr_exp_hi, hr_neg, hmb⟩

/-- **Sign-agnostic IOU multiply pipeline decomposition (any mode, any operand signs).**
Like `operator_mul_iou_decompose_anyMode` but with no non-negativity assumption: it
exposes the 19-digit `Number` product `r`, its range, and the *magnitude* relative-error
bound (rather than the one-sided directional `RoundsWithin`, whose signed direction the
`STAmount.ofNumber` magnitude re-round flips for negative products). -/
lemma STAmount.operator_mul_iou_decompose_mag (v1 v2 result : STAmount) (asset : Asset)
    (iss : Issue) (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hok : STAmount.multiply v1 v2 asset mode = .ok result) (hresult : result.mValue ≠ 0) :
    ∃ r : Number, STAmount.ofNumber asset r mode = .ok result ∧
      10 ^ 18 ≤ r.mantissa_.toNat ∧ r.mantissa_.toNat < 10 ^ 19 ∧
      minExponent ≤ r.exponent_ ∧ r.exponent_ + 4 ≤ maxExponent ∧
      |r.toRat - v1.toRat * v2.toRat| ≤ |v1.toRat * v2.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) ∧
      RoundsWithin r (v1.toRat * v2.toRat) mode (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hv1ne : v1.mValue ≠ 0 := by intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  have hv2ne : v2.mValue ≠ 0 := by intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1nat : v1.native = false := by
    unfold STAmount.native; rw [hv1]; show iss.native = false; rw [← h_xrp]; rfl
  have hv1mpt : v1.holdsMPTIssue = false := by unfold STAmount.holdsMPTIssue; rw [hv1]; rfl
  unfold STAmount.multiply at hok
  rw [if_neg (show ¬ (v1.isZero || v2.isZero) = true from by simp [STAmount.isZero, hv1ne, hv2ne]),
      if_neg (show ¬ (v1.native && v2.native && asset.isNative) = true from by simp [hv1nat]),
      if_neg (show ¬ (v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue) = true from by
        simp [hv1mpt]),
      STAmount.toNumber_iou_canonical v1 iss mode hv1 h_xrp hc1,
      STAmount.toNumber_iou_canonical v2 iss mode hv2 h_xrp hc2] at hok
  simp only at hok
  set n1 : Number := ⟨v1.mIsNegative, v1.mValue * 10 * 10 * 10, v1.mOffset - 3⟩ with hn1_def
  set n2 : Number := ⟨v2.mIsNegative, v2.mValue * 10 * 10 * 10, v2.mOffset - 3⟩ with hn2_def
  cases hmul : Number.operator_mul n1 n2 mode with
  | error e => rw [hmul] at hok; simp at hok
  | ok r =>
    rw [hmul] at hok; simp only at hok
    have hofn : STAmount.ofNumber asset r mode = .ok result := hok
    have hn1_norm := STAmount.toNumber_iou_canonical_isNormalized v1 hc1
    have hn2_norm := STAmount.toNumber_iou_canonical_isNormalized v2 hc2
    have hn1_ne := STAmount.toNumber_iou_canonical_mantissa_ne v1 hc1
    have hn2_ne := STAmount.toNumber_iou_canonical_mantissa_ne v2 hc2
    have hn1_val := STAmount.toNumber_iou_canonical_toRat v1 hc1
    have hn2_val := STAmount.toNumber_iou_canonical_toRat v2 hc2
    have hr_ne := STAmount.ofNumber_iou_mantissa_ne_zero asset r mode result ha_iou ha_not_xrp
      hofn hresult
    have hr_norm := operator_mul_result_isNormalized n1 n2 r mode hn1_norm hn2_norm hn1_ne hn2_ne
      hmul hr_ne
    have hr_mant := hr_norm.mantissaBounds_nat hr_ne
    have hr_exp_lo : minExponent ≤ r.exponent_ := by
      rcases hr_norm with hz | ⟨_, _, _, hlo, _⟩
      · exact absurd (show r.mantissa_ = 0 by rw [hz]; rfl) hr_ne
      · exact hlo
    have hr_exp_hi := operator_mul_exponent_hi_anyMode n1 n2 r 77 mode hn1_norm hn2_norm hn1_ne
      hn2_ne hr_ne (by show v1.mOffset - 3 ≤ 77; have := hc1.exp_hi; omega)
      (by show v2.mOffset - 3 ≤ 77; have := hc2.exp_hi; omega) (by unfold maxExponent; omega) hmul
    have hmag := operator_mul_abs_diff_le_anyMode n1 n2 r mode hn1_norm hn2_norm hn1_ne hn2_ne
      hr_ne hmul
    rw [hn1_val, hn2_val] at hmag
    have hmb := operator_mul_RoundsWithin_anyMode n1 n2 r mode hn1_norm hn2_norm hr_ne hmul
    rw [hn1_val, hn2_val] at hmb
    exact ⟨r, hofn, hr_mant.1, hr_mant.2, hr_exp_lo, hr_exp_hi, hmag, hmb⟩

end XRPL.Model.Protocol

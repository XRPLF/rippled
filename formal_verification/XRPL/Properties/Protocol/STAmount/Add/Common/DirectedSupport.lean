import XRPL.Properties.Protocol.STAmount.Add.Common.IOU
import XRPL.Properties.Protocol.Number.Add.Common.TowardsZero.DiffSignTight

/-! # Directed-mode support for the IOU `RoundsWithin` headlines

Mode-generic versions of the `to_nearest` IOU-addition composition lemmas, plus the
re-rounding `RoundsWithin` (directional + magnitude). These feed the directed-mode
(`downward`/`upward`/`towards_zero`) IOU headlines via `RoundsWithin_trans`. The
`Number`-addition relative-error bound for every directed mode is `11/(2⁶³−18)`
(double the `to_nearest` half-ULP bound), and the 16-digit re-rounding bound is
`10⁻¹⁵`, so the composed directed bound is `εd = 11/(2⁶³−18) + 10⁻¹⁵ + 11/(2⁶³−18)·10⁻¹⁵`. -/

namespace XRPL.Model.Protocol

/-- The `Number`-addition result sign equals the (common) operand sign when both
operands share a sign and do not cancel. -/
lemma operator_add_negative_eq_of_same_sign (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y mode = .ok result) :
    result.negative_ = x.negative_ := by
  obtain ⟨_, _, _, _, _, hspec⟩ :=
    operator_add_algorithmic_facts_same_sign_anyMode x y result mode hx hy
      hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok
  exact hspec.result_neg

lemma IOUAmount.ofNumber_rounds_within (sum : Number) (mode : rounding_mode) (result : IOUAmount)
    (h_lo : 10 ^ 18 ≤ sum.mantissa_.toNat) (h_hi : sum.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ sum.exponent_ + 3) (he_hi : sum.exponent_ + 4 ≤ maxExponent)
    (hok : IOUAmount.ofNumber sum mode = .ok result) (hne : result.mantissa_ ≠ 0) :
    RoundsWithin result sum.toRat mode ((10 : ℚ) ^ (-15 : ℤ)) := by
  have hmag := IOUAmount.ofNumber_within_ulp sum mode result h_lo h_hi he_lo he_hi hok hne
  have hsum_abs : |sum.toRat| = (sum.mantissa_.toNat : ℚ) * 10 ^ sum.exponent_ := by
    rcases hsg : sum.negative_ with _ | _
    · rw [Number.toRat_of_nonneg sum (by rw [hsg]), abs_of_nonneg (by positivity)]
    · rw [Number.toRat_of_neg sum (by rw [hsg]), abs_neg, abs_of_nonneg (by positivity)]
  have hmag' : |result.toRat - sum.toRat| ≤ |sum.toRat| * (10 : ℚ) ^ (-15 : ℤ) := by
    refine le_trans hmag ?_
    rw [hsum_abs]
    have e1 : (sum.mantissa_.toNat : ℚ) * 10 ^ sum.exponent_ * 10 ^ (-15 : ℤ)
        = (sum.mantissa_.toNat : ℚ) * 10 ^ (sum.exponent_ - 15) := by
      rw [mul_assoc, ← zpow_add₀ (by norm_num : (10:ℚ) ≠ 0)]; ring_nf
    have e2 : (10 : ℚ) ^ (sum.exponent_ + 3) = 10 ^ (18 : ℤ) * 10 ^ (sum.exponent_ - 15) := by
      rw [show (sum.exponent_ + 3 : ℤ) = 18 + (sum.exponent_ - 15) by ring,
          zpow_add₀ (by norm_num : (10:ℚ) ≠ 0)]
    rw [e1, e2]
    have hM : (10 : ℚ) ^ (18 : ℤ) ≤ (sum.mantissa_.toNat : ℚ) := by
      rw [show (10:ℚ)^(18:ℤ) = (10:ℚ)^(18:ℕ) by norm_num]; exact_mod_cast h_lo
    gcongr
  have habs := abs_le.mp hmag'
  -- result = ⟨m, e⟩ from the in-range clamp
  unfold IOUAmount.ofNumber IOUAmount.fromNumber at hok
  cases hnorm : sum.normalizeToRange cMinValue cMaxValue mode with
  | error e => rw [hnorm] at hok; simp at hok
  | ok me =>
    obtain ⟨m, e⟩ := me
    rw [hnorm] at hok; simp only at hok
    by_cases hhi : e > cMaxOffset
    · rw [if_pos hhi] at hok; simp at hok
    · by_cases hlo : e < cMinOffset
      · rw [if_neg hhi, if_pos hlo] at hok
        exact absurd (by rw [← Except.ok.inj hok] : result.mantissa_ = IOUAmount.zero.mantissa_) hne
      · rw [if_neg hhi, if_neg hlo] at hok
        have hres : result = ⟨m, e⟩ := (Except.ok.inj hok).symm
        have hres_toRat : result.toRat = (m.toInt : ℚ) * 10 ^ e := by rw [hres, IOUAmount.toRat_eq]
        have htr : (RatValued.toRat result : ℚ) = result.toRat := rfl
        unfold RoundsWithin
        cases mode with
        | to_nearest => rw [htr]; exact hmag'
        | downward =>
          rw [htr]
          exact ⟨by rw [hres_toRat]; exact normalizeToRange_16_downward sum m e h_lo h_hi he_lo he_hi hnorm,
                 by linarith [habs.1]⟩
        | upward =>
          rw [htr]
          exact ⟨by rw [hres_toRat]; exact normalizeToRange_16_upward sum m e h_lo h_hi he_lo he_hi hnorm,
                 by linarith [habs.2]⟩
        | towards_zero =>
          rw [htr]
          refine ⟨by rw [hres_toRat]; exact normalizeToRange_16_towards_zero sum m e h_lo h_hi he_lo he_hi hnorm, ?_⟩
          have hrev : |sum.toRat| - |result.toRat| ≤ |result.toRat - sum.toRat| := by
            rw [abs_sub_comm result.toRat sum.toRat]
            exact abs_sub_abs_le_abs_sub sum.toRat result.toRat
          linarith [hrev, hmag']
/-- The `Number`-addition relative-error bound, uniform across modes at the looser
directed value `11/(2⁶³−18)` (`to_nearest` is tighter, lifted by monotonicity). -/
lemma operator_add_RoundsWithin_anyMode (xn yn sum : Number) (mode : rounding_mode)
    (hxn_norm : xn.isNormalized) (hyn_norm : yn.isNormalized)
    (hxn_ne : xn.mantissa_ ≠ 0) (hyn_ne : yn.mantissa_ ≠ 0)
    (h_no_cancel : ¬ xn.operator_eq yn.operator_neg) (hsum_ne : sum.mantissa_ ≠ 0)
    (hadd : Number.operator_add xn yn mode = .ok sum) :
    RoundsWithin sum (xn.toRat + yn.toRat) mode (11 / (2 ^ 63 - 18 : ℚ)) := by
  cases mode with
  | to_nearest =>
    exact RoundsWithin_mono sum (xn.toRat + yn.toRat) (6 / (2 ^ 63 - 3 : ℚ)) (11 / (2 ^ 63 - 18 : ℚ))
      .to_nearest (operator_add_rounds_to_nearest xn yn sum hxn_norm hyn_norm hxn_ne hyn_ne
        h_no_cancel hadd hsum_ne) (by norm_num)
  | downward =>
    exact operator_add_rounds_downward xn yn sum hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hadd hsum_ne
  | upward =>
    exact operator_add_rounds_upward xn yn sum hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hadd hsum_ne
  | towards_zero =>
    exact operator_add_rounds_towards_zero xn yn sum hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hadd hsum_ne

/-- **Magnitude form of the `Number`-addition directed bound, every mode, any operand
signs** (no non-negativity needed). For `to_nearest`/`downward`/`upward` it is the
`RoundsWithin` magnitude directly; for `towards_zero` the result is sign-aligned with the
exact sum: same-sign via the result-sign lemma, different-sign via the tight
cancellation bound, so `|sum − truth| = |truth| − |sum|`. -/
lemma operator_add_abs_diff_le_anyMode (xn yn sum : Number) (mode : rounding_mode)
    (hxn_norm : xn.isNormalized) (hyn_norm : yn.isNormalized)
    (hxn_ne : xn.mantissa_ ≠ 0) (hyn_ne : yn.mantissa_ ≠ 0)
    (h_no_cancel : ¬ xn.operator_eq yn.operator_neg) (hsum_ne : sum.mantissa_ ≠ 0)
    (hadd : Number.operator_add xn yn mode = .ok sum) :
    |sum.toRat - (xn.toRat + yn.toRat)| ≤ |xn.toRat + yn.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  have hrw := operator_add_RoundsWithin_anyMode xn yn sum mode hxn_norm hyn_norm hxn_ne hyn_ne
    h_no_cancel hsum_ne hadd
  unfold RoundsWithin at hrw
  cases mode with
  | to_nearest => exact hrw
  | downward =>
    obtain ⟨hle, hm⟩ := hrw
    rw [show RatValued.toRat sum = sum.toRat from rfl] at hle hm
    rw [abs_of_nonpos (by linarith), neg_sub]; exact hm
  | upward =>
    obtain ⟨hge, hm⟩ := hrw
    rw [show RatValued.toRat sum = sum.toRat from rfl] at hge hm
    rw [abs_of_nonneg (by linarith)]; exact hm
  | towards_zero =>
    by_cases hss : xn.negative_ = yn.negative_
    · -- same sign: the result sign equals the operand sign, hence aligned with the sum.
      obtain ⟨hle_abs, hm_abs⟩ := hrw
      rw [show RatValued.toRat sum = sum.toRat from rfl] at hle_abs hm_abs
      have hsumneg : sum.negative_ = xn.negative_ :=
        operator_add_negative_eq_of_same_sign xn yn sum .towards_zero hxn_norm hyn_norm
          hxn_ne hyn_ne hss h_no_cancel hadd
      rw [abs_diff_eq_abs_sub_abs_of_sign_aligned sum (xn.toRat + yn.toRat)
        (fun hneg => by
          have hxneg : xn.negative_ = true := hsumneg ▸ hneg
          have hyneg : yn.negative_ = true := hss ▸ hxneg
          have h1 := Number.toRat_nonpos_of_negative xn hxneg
          have h2 := Number.toRat_nonpos_of_negative yn hyneg
          linarith)
        (fun hpos => by
          have hxpos : xn.negative_ = false := hsumneg ▸ hpos
          have hypos : yn.negative_ = false := hss ▸ hxpos
          have h1 := Number.toRat_nonneg_of_nonnegative xn hxpos
          have h2 := Number.toRat_nonneg_of_nonnegative yn hypos
          linarith)]
      rw [abs_of_nonpos (by linarith), neg_sub]; exact hm_abs
    · -- different sign: the tight cancellation bound gives the magnitude directly.
      exact le_of_lt (operator_add_rounding_bound_diff_sign_towards_zero_tight xn yn sum
        hxn_norm hyn_norm hxn_ne hyn_ne hss h_no_cancel hadd hsum_ne)

/-- Mode-generic version of `IOUAmount.add_sum_exponent_hi`: the `Number` sum's
exponent stays `≤ maxExponent − 4` for any rounding mode (the `|sum| ≤ 2·|x+y|`
magnitude bound is uniform across modes). -/
lemma IOUAmount.add_sum_exponent_hi_anyMode (x y : IOUAmount) (xn yn sum : Number) (mode : rounding_mode)
    (hxn_val : xn.toRat = x.toRat) (hyn_val : yn.toRat = y.toRat)
    (hxn_norm : xn.isNormalized) (hyn_norm : yn.isNormalized)
    (hxn_ne : xn.mantissa_ ≠ 0) (hyn_ne : yn.mantissa_ ≠ 0)
    (h_no_cancel : ¬ xn.operator_eq yn.operator_neg) (hsum_ne : sum.mantissa_ ≠ 0)
    (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe : x.exponent_ ≤ maxExponent - 4) (hye : y.exponent_ ≤ maxExponent - 4)
    (hadd : Number.operator_add xn yn mode = .ok sum) :
    sum.exponent_ + 4 ≤ maxExponent := by
  by_contra hcon
  push_neg at hcon
  have hge : maxExponent - 3 ≤ sum.exponent_ := by omega
  have hsum_norm := operator_add_result_isNormalized_anyMode xn yn sum mode
    hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hadd hsum_ne
  have hsum_mant := hsum_norm.mantissaBounds_nat hsum_ne
  have hlower : (10 : ℚ) ^ (maxExponent + 15) ≤ |sum.toRat| := by
    rw [_root_.XRPL.Model.Protocol.abs_toRat_eq sum]
    have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (sum.mantissa_.toNat : ℚ) := by
      have : ((10 ^ 18 : ℕ) : ℚ) ≤ (sum.mantissa_.toNat : ℚ) := by exact_mod_cast hsum_mant.1
      rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
    calc (10 : ℚ) ^ (maxExponent + 15)
        ≤ (10 : ℚ) ^ (sum.exponent_ + 18) := zpow_le_zpow_right₀ (by norm_num) (by omega)
      _ = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ sum.exponent_ := by
          rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
      _ ≤ (sum.mantissa_.toNat : ℚ) * (10 : ℚ) ^ sum.exponent_ := by gcongr
  have hsum2 : |sum.toRat| ≤ |x.toRat + y.toRat| * 2 := by
    have hrw := operator_add_RoundsWithin_anyMode xn yn sum mode hxn_norm hyn_norm hxn_ne hyn_ne
      h_no_cancel hsum_ne hadd
    have h := RoundsWithin_abs_le_two sum (xn.toRat + yn.toRat) (11 / (2 ^ 63 - 18 : ℚ)) mode hrw (by norm_num)
    rwa [show RatValued.toRat sum = sum.toRat from rfl, hxn_val, hyn_val] at h
  have hbound_xy : ∀ (a : IOUAmount), a.mantissa_.toInt.natAbs < 10 ^ 16 →
      a.exponent_ ≤ maxExponent - 4 → |a.toRat| < (10 : ℚ) ^ (maxExponent + 12) := by
    intro a ha_hi ha_e
    rw [IOUAmount.abs_toRat_eq a]
    have hpow : (10 : ℚ) ^ (maxExponent + 12) = (10 : ℚ) ^ (16 : ℤ) * (10 : ℚ) ^ (maxExponent - 4) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
    rw [hpow]
    have hmant : (a.mantissa_.toInt.natAbs : ℚ) < (10 : ℚ) ^ (16 : ℤ) := by
      have : (a.mantissa_.toInt.natAbs : ℚ) < ((10 ^ 16 : ℕ) : ℚ) := by exact_mod_cast ha_hi
      rwa [show ((10 ^ 16 : ℕ) : ℚ) = (10 : ℚ) ^ (16 : ℤ) by push_cast; norm_num] at this
    have hexp : (10 : ℚ) ^ a.exponent_ ≤ (10 : ℚ) ^ (maxExponent - 4) :=
      zpow_le_zpow_right₀ (by norm_num) ha_e
    calc (a.mantissa_.toInt.natAbs : ℚ) * (10 : ℚ) ^ a.exponent_
        < (10 : ℚ) ^ (16 : ℤ) * (10 : ℚ) ^ a.exponent_ :=
          mul_lt_mul_of_pos_right hmant (zpow_pos (by norm_num) _)
      _ ≤ (10 : ℚ) ^ (16 : ℤ) * (10 : ℚ) ^ (maxExponent - 4) :=
          mul_le_mul_of_nonneg_left hexp (by positivity)
  have hxabs := hbound_xy x hx_hi hxe
  have hyabs := hbound_xy y hy_hi hye
  have hupper : |sum.toRat| < (10 : ℚ) ^ (maxExponent + 15) := by
    have hxy : |x.toRat + y.toRat| ≤ |x.toRat| + |y.toRat| := abs_add_le _ _
    have hstep2 : |x.toRat + y.toRat| * 2 ≤ (|x.toRat| + |y.toRat|) * 2 := by linarith [hxy]
    have hstep3 : (|x.toRat| + |y.toRat|) * 2 < (10 : ℚ) ^ (maxExponent + 12) * 4 := by
      nlinarith [hxabs, hyabs]
    have hpow15 : (10 : ℚ) ^ (maxExponent + 15) = (10 : ℚ) ^ (maxExponent + 12) * (10 : ℚ) ^ (3 : ℤ) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
    have hcmp : (10 : ℚ) ^ (maxExponent + 12) * 4 < (10 : ℚ) ^ (maxExponent + 15) := by
      rw [hpow15, show (10 : ℚ) ^ (3 : ℤ) = 1000 by norm_num]
      have hpos : (0 : ℚ) < (10 : ℚ) ^ (maxExponent + 12) := zpow_pos (by norm_num) _
      nlinarith [hpos]
    linarith [hsum2, hstep2, hstep3, hcmp]
  linarith

/-- Mode-generic version of `IOUAmount.operator_add_to_nearest_decompose`. -/
lemma IOUAmount.operator_add_decompose_anyMode (x y result : IOUAmount) (mode : rounding_mode)
    (hx_lo : 10 ^ 15 ≤ x.mantissa_.toInt.natAbs) (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16)
    (hy_lo : 10 ^ 15 ≤ y.mantissa_.toInt.natAbs) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe_lo : minExponent + 3 ≤ x.exponent_) (hxe_hi : x.exponent_ ≤ maxExponent - 4)
    (hye_lo : minExponent + 3 ≤ y.exponent_) (hye_hi : y.exponent_ ≤ maxExponent - 4)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (xn yn sum : Number),
      xn.toRat = x.toRat ∧ yn.toRat = y.toRat ∧
      xn.isNormalized ∧ yn.isNormalized ∧ xn.mantissa_ ≠ 0 ∧ yn.mantissa_ ≠ 0 ∧
      ¬ xn.operator_eq yn.operator_neg ∧ sum.mantissa_ ≠ 0 ∧
      Number.operator_add xn yn mode = .ok sum ∧
      10 ^ 18 ≤ sum.mantissa_.toNat ∧ sum.mantissa_.toNat < 10 ^ 19 ∧
      minExponent ≤ sum.exponent_ + 3 ∧ sum.exponent_ + 4 ≤ maxExponent ∧
      IOUAmount.ofNumber sum mode = .ok result := by
  have hxm : x.mantissa_ ≠ 0 := by intro h; rw [h] at hx_lo; simp at hx_lo
  have hym : y.mantissa_ ≠ 0 := by intro h; rw [h] at hy_lo; simp at hy_lo
  obtain ⟨xn, hxn_eq, hxn_val, hxn_norm, hxn_ne⟩ :=
    IOUAmount.toNumber_canonical x mode hx_lo hx_hi hxe_lo (by omega)
  obtain ⟨yn, hyn_eq, hyn_val, hyn_norm, hyn_ne⟩ :=
    IOUAmount.toNumber_canonical y mode hy_lo hy_hi hye_lo (by omega)
  unfold IOUAmount.operator_add at hok
  rw [if_neg (by simpa using hym), if_neg (by simpa using hxm), hxn_eq, hyn_eq] at hok
  simp only at hok
  cases hadd : Number.operator_add xn yn mode with
  | error e => rw [hadd] at hok; simp at hok
  | ok sum =>
    rw [hadd] at hok
    simp only at hok
    have hofn : IOUAmount.ofNumber sum mode = .ok result := hok
    have h_no_cancel : ¬ xn.operator_eq yn.operator_neg := by
      intro hc
      have hyn_neg_norm : yn.operator_neg.isNormalized := Number.operator_neg_isNormalized yn hyn_norm
      have heq := (operator_eq_iff xn yn.operator_neg hxn_norm hyn_neg_norm).mp hc
      rw [Number.toRat_neg, hxn_val, hyn_val] at heq
      exact h_truth_ne (by linarith)
    have hsum_ne : sum.mantissa_ ≠ 0 :=
      IOUAmount.ofNumber_mantissa_ne_zero sum mode result hofn hresult
    have hsum_norm := operator_add_result_isNormalized_anyMode xn yn sum mode
      hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hadd hsum_ne
    have hsum_mant := hsum_norm.mantissaBounds_nat hsum_ne
    have hsum_exp : minExponent ≤ sum.exponent_ ∧ sum.exponent_ ≤ maxExponent := by
      rcases hsum_norm with hz | ⟨_, _, _, hlo, hhi⟩
      · exact absurd (show sum.mantissa_ = 0 by rw [hz]; rfl) hsum_ne
      · exact ⟨hlo, hhi⟩
    have he_hi : sum.exponent_ + 4 ≤ maxExponent :=
      IOUAmount.add_sum_exponent_hi_anyMode x y xn yn sum mode hxn_val hyn_val hxn_norm hyn_norm
        hxn_ne hyn_ne h_no_cancel hsum_ne hx_hi hy_hi hxe_hi hye_hi hadd
    exact ⟨xn, yn, sum, hxn_val, hyn_val, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel,
      hsum_ne, hadd, hsum_mant.1, hsum_mant.2, by omega, he_hi, hofn⟩

/-- **IOU addition rounds within the directed relative-error bound `εd ≈ 2·10⁻¹⁵`,
every mode.** `Number`-addition (bound `11/(2⁶³−18)`) composed with the 16-digit
re-rounding (bound `10⁻¹⁵`) via `RoundsWithin_trans`. -/
lemma IOUAmount.operator_add_rounds_directed (x y result : IOUAmount) (mode : rounding_mode)
    (hx_lo : 10 ^ 15 ≤ x.mantissa_.toInt.natAbs) (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16)
    (hy_lo : 10 ^ 15 ≤ y.mantissa_.toInt.natAbs) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe_lo : minExponent + 3 ≤ x.exponent_) (hxe_hi : x.exponent_ ≤ maxExponent - 4)
    (hye_lo : minExponent + 3 ≤ y.exponent_) (hye_hi : y.exponent_ ≤ maxExponent - 4)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) mode
      (11 / (2 ^ 63 - 18 : ℚ) + (10 : ℚ) ^ (-15 : ℤ)
        + 11 / (2 ^ 63 - 18 : ℚ) * (10 : ℚ) ^ (-15 : ℤ)) := by
  obtain ⟨xn, yn, sum, hxn_val, hyn_val, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel,
    hsum_ne, hadd, h_lo, h_hi, he_lo, he_hi, hofn⟩ :=
    IOUAmount.operator_add_decompose_anyMode x y result mode hx_lo hx_hi hy_lo hy_hi
      hxe_lo hxe_hi hye_lo hye_hi h_truth_ne hok hresult
  have h1 : RoundsWithin sum (xn.toRat + yn.toRat) mode (11 / (2 ^ 63 - 18 : ℚ)) :=
    operator_add_RoundsWithin_anyMode xn yn sum mode hxn_norm hyn_norm hxn_ne hyn_ne
      h_no_cancel hsum_ne hadd
  have h2 : RoundsWithin result (RatValued.toRat sum) mode ((10 : ℚ) ^ (-15 : ℤ)) := by
    rw [show RatValued.toRat sum = sum.toRat from rfl]
    exact IOUAmount.ofNumber_rounds_within sum mode result h_lo h_hi he_lo he_hi hofn hresult
  have htrans := RoundsWithin_trans result sum (xn.toRat + yn.toRat) (11 / (2 ^ 63 - 18 : ℚ))
    ((10 : ℚ) ^ (-15 : ℤ)) mode h1 h2 (by positivity) (by positivity)
  rwa [hxn_val, hyn_val] at htrans

/-- Mode-generic version of `IOUAmount.operator_add_InRange16`. -/
lemma IOUAmount.operator_add_InRange16_anyMode (x y result : IOUAmount) (mode : rounding_mode)
    (hx_lo : 10 ^ 15 ≤ x.mantissa_.toInt.natAbs) (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16)
    (hy_lo : 10 ^ 15 ≤ y.mantissa_.toInt.natAbs) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe_lo : minExponent + 3 ≤ x.exponent_) (hxe_hi : x.exponent_ ≤ maxExponent - 4)
    (hye_lo : minExponent + 3 ≤ y.exponent_) (hye_hi : y.exponent_ ≤ maxExponent - 4)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y mode = .ok result) (hresult : result.mantissa_ ≠ 0) :
    result.InRange16 := by
  obtain ⟨_, _, sum, _, _, _, _, _, _, _, _, _, h_lo, h_hi, he_lo, he_hi, hofn⟩ :=
    IOUAmount.operator_add_decompose_anyMode x y result mode hx_lo hx_hi hy_lo hy_hi
      hxe_lo hxe_hi hye_lo hye_hi h_truth_ne hok hresult
  exact IOUAmount.ofNumber_InRange16 sum mode result h_lo h_hi he_lo he_hi hofn hresult

/-- **STAmount IOU addition rounds within the directed relative-error bound, every
mode.** The `STAmount → IOUAmount → STAmount` round-trip is value-exact; the bound
is inherited from `IOUAmount.operator_add_rounds_directed`. -/
theorem STAmount.operator_add_iou_rounds_directed (v1 v2 result : STAmount) (iss : Issue)
    (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 mode = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat + v2.toRat) mode
      (11 / (2 ^ 63 - 18 : ℚ) + (10 : ℚ) ^ (-15 : ℤ)
        + 11 / (2 ^ 63 - 18 : ℚ) * (10 : ℚ) ^ (-15 : ℤ)) := by
  have h_not_xrp : (Asset.issue iss).isNative = false := h_xrp
  have hcmp : STAmount.areComparable v1 v2 = true := by
    rcases hb : STAmount.areComparable v1 v2 with _ | _
    · rw [STAmount.operator_add, hb] at hok; simp at hok
    · rfl
  rw [STAmount.operator_add, if_neg (by rw [hcmp]; decide)] at hok
  have hv2ne : v2.mValue ≠ 0 := by intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1ne : v1.mValue ≠ 0 := by intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  rw [if_neg (by simpa using hv2ne), if_neg (by simpa using hv1ne), hv1] at hok
  simp only at hok
  rw [if_neg (show ¬ (iss.isXRP = true) from by rw [h_xrp]; decide)] at hok
  rw [STAmount.iou_canonical_id v1 mode hc1, STAmount.iou_canonical_id v2 mode hc2] at hok
  simp only at hok
  set i1 : IOUAmount := ⟨v1.signedDrops.toInt64, v1.mOffset⟩ with hi1
  set i2 : IOUAmount := ⟨v2.signedDrops.toInt64, v2.mOffset⟩ with hi2
  cases hadd : IOUAmount.operator_add i1 i2 mode with
  | error e => rw [hadd] at hok; simp at hok
  | ok sumI =>
    rw [hadd] at hok
    simp only at hok
    have hi1v : i1.toRat = v1.toRat := STAmount.iou_canonical_toRat v1 hc1
    have hi2v : i2.toRat = v2.toRat := STAmount.iou_canonical_toRat v2 hc2
    have hi1n : i1.mantissa_.toInt.natAbs = v1.mValue.toNat := STAmount.iou_canonical_mantissa_natAbs v1 hc1
    have hi2n : i2.mantissa_.toInt.natAbs = v2.mValue.toNat := STAmount.iou_canonical_mantissa_natAbs v2 hc2
    have hi1e : i1.exponent_ = v1.mOffset := rfl
    have hi2e : i2.exponent_ = v2.mOffset := rfl
    have hsumI_ne : sumI.mantissa_ ≠ 0 :=
      STAmount.ofIOUAmount_mantissa_ne_zero sumI iss mode h_not_xrp result hok hresult
    have hsumI_range : sumI.InRange16 :=
      IOUAmount.operator_add_InRange16_anyMode i1 i2 sumI mode
        (by rw [hi1n]; exact hc1.mant_lo) (by rw [hi1n]; exact hc1.mant_hi)
        (by rw [hi2n]; exact hc2.mant_lo) (by rw [hi2n]; exact hc2.mant_hi)
        (by rw [hi1e]; have := hc1.exp_lo; unfold minExponent; omega)
        (by rw [hi1e]; have := hc1.exp_hi; unfold maxExponent; omega)
        (by rw [hi2e]; have := hc2.exp_lo; unfold minExponent; omega)
        (by rw [hi2e]; have := hc2.exp_hi; unfold maxExponent; omega)
        (by rw [hi1v, hi2v]; exact h_truth_ne) hadd hsumI_ne
    have hrv : result.toRat = sumI.toRat :=
      STAmount.ofIOUAmount_canonical_toRat sumI iss mode h_not_xrp hsumI_range result hok
    have hbound := IOUAmount.operator_add_rounds_directed i1 i2 sumI mode
      (by rw [hi1n]; exact hc1.mant_lo) (by rw [hi1n]; exact hc1.mant_hi)
      (by rw [hi2n]; exact hc2.mant_lo) (by rw [hi2n]; exact hc2.mant_hi)
      (by rw [hi1e]; have := hc1.exp_lo; unfold minExponent; omega)
      (by rw [hi1e]; have := hc1.exp_hi; unfold maxExponent; omega)
      (by rw [hi2e]; have := hc2.exp_lo; unfold minExponent; omega)
      (by rw [hi2e]; have := hc2.exp_hi; unfold maxExponent; omega)
      (by rw [hi1v, hi2v]; exact h_truth_ne) hadd hsumI_ne
    rw [hi1v, hi2v] at hbound
    exact RoundsWithin_toRat_congr result sumI _ _ _ hrv hbound

/-- **STAmount IOU addition decomposition (mode-generic).** Exposes the 19-digit
`Number` summands `xn`, `yn`, their `Number` sum `sum`, the intermediate `IOUAmount`
`sumI`, and every fact the discrete (`RoundsToRepresentableWithin`) headlines need:
the value/exponent bridge `result.toRat = sumI.toRat`, `result.exponent = sumI.exponent_`
(the `STAmount → IOUAmount → STAmount` round-trip is value- and exponent-exact), the
closing snap `IOUAmount.ofNumber sum = sumI`, the 19-digit range of `sum`, and the
`Number`-add reduction. -/
lemma STAmount.operator_add_iou_decompose_anyMode (v1 v2 result : STAmount) (iss : Issue)
    (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 mode = .ok result)
    (hresult : result.mValue ≠ 0) :
    ∃ (xn yn sum : Number) (sumI : IOUAmount),
      result.toRat = sumI.toRat ∧ result.exponent = sumI.exponent_ ∧
      IOUAmount.ofNumber sum mode = .ok sumI ∧ sumI.mantissa_ ≠ 0 ∧
      10 ^ 18 ≤ sum.mantissa_.toNat ∧ sum.mantissa_.toNat < 10 ^ 19 ∧
      minExponent ≤ sum.exponent_ + 3 ∧ sum.exponent_ + 4 ≤ maxExponent ∧
      xn.toRat = v1.toRat ∧ yn.toRat = v2.toRat ∧
      xn.isNormalized ∧ yn.isNormalized ∧ xn.mantissa_ ≠ 0 ∧ yn.mantissa_ ≠ 0 ∧
      ¬ xn.operator_eq yn.operator_neg ∧ sum.mantissa_ ≠ 0 ∧
      Number.operator_add xn yn mode = .ok sum := by
  have h_not_xrp : (Asset.issue iss).isNative = false := h_xrp
  have hcmp : STAmount.areComparable v1 v2 = true := by
    rcases hb : STAmount.areComparable v1 v2 with _ | _
    · rw [STAmount.operator_add, hb] at hok; simp at hok
    · rfl
  rw [STAmount.operator_add, if_neg (by rw [hcmp]; decide)] at hok
  have hv2ne : v2.mValue ≠ 0 := by intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1ne : v1.mValue ≠ 0 := by intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  rw [if_neg (by simpa using hv2ne), if_neg (by simpa using hv1ne), hv1] at hok
  simp only at hok
  rw [if_neg (show ¬ (iss.isXRP = true) from by rw [h_xrp]; decide)] at hok
  rw [STAmount.iou_canonical_id v1 mode hc1, STAmount.iou_canonical_id v2 mode hc2] at hok
  simp only at hok
  set i1 : IOUAmount := ⟨v1.signedDrops.toInt64, v1.mOffset⟩ with hi1
  set i2 : IOUAmount := ⟨v2.signedDrops.toInt64, v2.mOffset⟩ with hi2
  cases hadd : IOUAmount.operator_add i1 i2 mode with
  | error e => rw [hadd] at hok; simp at hok
  | ok sumI =>
    rw [hadd] at hok
    simp only at hok
    have hi1v : i1.toRat = v1.toRat := STAmount.iou_canonical_toRat v1 hc1
    have hi2v : i2.toRat = v2.toRat := STAmount.iou_canonical_toRat v2 hc2
    have hi1n : i1.mantissa_.toInt.natAbs = v1.mValue.toNat := STAmount.iou_canonical_mantissa_natAbs v1 hc1
    have hi2n : i2.mantissa_.toInt.natAbs = v2.mValue.toNat := STAmount.iou_canonical_mantissa_natAbs v2 hc2
    have hi1e : i1.exponent_ = v1.mOffset := rfl
    have hi2e : i2.exponent_ = v2.mOffset := rfl
    have hsumI_ne : sumI.mantissa_ ≠ 0 :=
      STAmount.ofIOUAmount_mantissa_ne_zero sumI iss mode h_not_xrp result hok hresult
    have hsumI_range : sumI.InRange16 :=
      IOUAmount.operator_add_InRange16_anyMode i1 i2 sumI mode
        (by rw [hi1n]; exact hc1.mant_lo) (by rw [hi1n]; exact hc1.mant_hi)
        (by rw [hi2n]; exact hc2.mant_lo) (by rw [hi2n]; exact hc2.mant_hi)
        (by rw [hi1e]; have := hc1.exp_lo; unfold minExponent; omega)
        (by rw [hi1e]; have := hc1.exp_hi; unfold maxExponent; omega)
        (by rw [hi2e]; have := hc2.exp_lo; unfold minExponent; omega)
        (by rw [hi2e]; have := hc2.exp_hi; unfold maxExponent; omega)
        (by rw [hi1v, hi2v]; exact h_truth_ne) hadd hsumI_ne
    have hrv : result.toRat = sumI.toRat :=
      STAmount.ofIOUAmount_canonical_toRat sumI iss mode h_not_xrp hsumI_range result hok
    -- exponent bridge: `ofIOUAmount` packs `sumI.exponent_` into `result.mOffset`.
    have hpack := STAmount.ofIOUAmount_canonical sumI iss mode h_not_xrp hsumI_range
    rw [hok] at hpack
    have hres_eq : result = ⟨.issue iss, sumI.mantissa_.toInt.natAbs.toUInt64, sumI.exponent_,
        decide (sumI.mantissa_ < 0)⟩ := Except.ok.inj hpack
    have hexp_br : result.exponent = sumI.exponent_ := by rw [hres_eq]; rfl
    -- the IOUAmount-level decomposition gives the `Number` summands and sum.
    obtain ⟨xn, yn, sum, hxn_val, hyn_val, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel,
        hsum_ne, hadd', h_lo, h_hi, he_lo, he_hi, hofn⟩ :=
      IOUAmount.operator_add_decompose_anyMode i1 i2 sumI mode
        (by rw [hi1n]; exact hc1.mant_lo) (by rw [hi1n]; exact hc1.mant_hi)
        (by rw [hi2n]; exact hc2.mant_lo) (by rw [hi2n]; exact hc2.mant_hi)
        (by rw [hi1e]; have := hc1.exp_lo; unfold minExponent; omega)
        (by rw [hi1e]; have := hc1.exp_hi; unfold maxExponent; omega)
        (by rw [hi2e]; have := hc2.exp_lo; unfold minExponent; omega)
        (by rw [hi2e]; have := hc2.exp_hi; unfold maxExponent; omega)
        (by rw [hi1v, hi2v]; exact h_truth_ne) hadd hsumI_ne
    exact ⟨xn, yn, sum, sumI, hrv, hexp_br, hofn, hsumI_ne, h_lo, h_hi, he_lo, he_hi,
      hxn_val.trans hi1v, hyn_val.trans hi2v, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel,
      hsum_ne, hadd'⟩

end XRPL.Model.Protocol

import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.DiffSignTail
import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts.DiffSignRepresents
import XRPL.Properties.Protocol.Number.Common.Helpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

set_option maxHeartbeats 1600000 in
-- Large case analysis (4 alignment cases × 3 rounding branches) over the full
-- doRoundDown + normalize pipeline; the extra budget is for the Case B supTight chain.
theorem operator_add_rounding_bound_diff_sign_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| ≤ |x.toRat + y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) := by
  obtain ⟨zm, ze', f, zn, g, res_pos, hf_rep, hzm_gt, hzm_lt, habs_xy_eq, h_rdown,
          h_norm, hsign, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .to_nearest hx hy hx_mant_ne hy_mant_ne
      h_diff_sign h_not_zero hok hresult
  -- result.negative_ = zn
  have h_norm' : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
      res_pos.toNumber.exponent_ largeRange.min largeRange.max .to_nearest = .ok result := h_norm
  -- res_pos has nonzero mantissa (else normalize would zero out result).
  have hres_pos_mant_ne : res_pos.toNumber.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result h_norm hresult
  have h_res_pos_neg : res_pos.toNumber.negative_ = zn := by
    -- bringIntoRange's underflow branch sets negative_ := false AND mantissa_ := 0;
    -- since mantissa ≠ 0 we are in the non-underflow branch where negative_ := zn.
    rw [← h_rdown] at hres_pos_mant_ne ⊢
    unfold RoundResult.toNumber Guard.doRoundDown Guard.bringIntoRange at hres_pos_mant_ne ⊢
    simp only [] at hres_pos_mant_ne ⊢
    split_ifs at hres_pos_mant_ne ⊢ <;> first | rfl | (exact absurd rfl hres_pos_mant_ne)
  have h_result_neg : result.negative_ = zn := by
    rw [doNormalize_preserves_negative h_norm' hresult, h_res_pos_neg]
  -- sign bridge
  have h_abs_diff_eq : |result.toRat - (x.toRat + y.toRat)|
      = |(|result.toRat| - |x.toRat + y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat + y.toRat)
    · intro h_neg; exact hsign.1 (h_result_neg ▸ h_neg)
    · intro h_pos; exact hsign.2 (h_result_neg ▸ h_pos)
  rw [h_abs_diff_eq, habs_xy_eq]
  rw [abs_toRat_eq result]
  -- Now goal: |(result.mantissa_.toNat * 10^result.exponent_) - (zm.toNat - f)*10^ze'|
  --           ≤ (zm.toNat - f)*10^ze' * (6/(2^63-3))
  -- Shared facts.
  have hzm_ge : (mantissaFloor : ℕ) ≤ zm.toNat := le_of_lt hzm_gt
  have hf_nn : 0 ≤ f := represents_nonneg hf_rep
  have hf_lt : f < 1 := represents_lt_one hf_rep
  have h10ze_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_truth_pos : 0 < ((zm.toNat : ℚ) - f) * 10 ^ ze' := by
    apply mul_pos _ h10ze_pos; linarith
  have h_truth_nn : 0 ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze' := le_of_lt h_truth_pos
  -- res_pos.mantissa_ ≠ 0 (already have hres_pos_mant_ne).
  -- doRoundDown input exponent ≥ minExponent.
  have h_rd_mant_ne : (g.doRoundDown zn zm ze' largeRange.min .to_nearest).mantissa_ ≠ 0 := by
    rw [h_rdown]; exact hres_pos_mant_ne
  have h_ze_ge : minExponent ≤ ze' :=
    doRoundDown_input_exp_ge_minExp_of_mant_ne g zn zm ze' largeRange.min .to_nearest h_rd_mant_ne
  -- Bridge res_pos to the sign-false doRoundDown for value/supTight lemmas.
  have h_mant_si : res_pos.mantissa_ = (g.doRoundDown false zm ze' largeRange.min .to_nearest).mantissa_ := by
    rw [← h_rdown]; exact doRoundDown_mantissa_sign_indep g zn zm ze' largeRange.min .to_nearest
  have h_exp_si : res_pos.exponent_ = (g.doRoundDown false zm ze' largeRange.min .to_nearest).exponent_ := by
    rw [← h_rdown]; exact doRoundDown_exponent_sign_indep g zn zm ze' largeRange.min .to_nearest
  -- The supTight floor constraint is vacuous since zm.toNat > floor (strict).
  have h_floor_vac : zm.toNat = mantissaFloor → f ≤ (2 : ℚ) / 10 := by
    intro h; omega
  -- res_pos value abbreviation.
  set V : ℚ := (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ with hV_def
  set T : ℚ := ((zm.toNat : ℚ) - f) * 10 ^ ze' with hT_def
  -- Main case split: zm.toNat ≤ maxRep and minExponent < ze'.
  by_cases h_zm_le_max : zm.toNat ≤ maxRep.toNat
  · by_cases h_ze_gt : minExponent < ze'
    · -- CASE A: supTight applies directly; normalize is identity.
      -- doRoundDown output invariants (for the false-sign version; sign-independent).
      have h_inv := doRoundDown_output_in_range_of_floor g false zm ze' .to_nearest hzm_ge h_zm_le_max h_ze_gt
      simp only at h_inv
      obtain ⟨h_rp_min, h_rp_max, h_rp_exp, h_rp_mod⟩ := h_inv
      -- Transfer to res_pos via sign-independence.
      rw [← h_mant_si] at h_rp_min h_rp_max h_rp_mod
      rw [← h_exp_si] at h_rp_exp
      -- normalize identity: result = res_pos.toNumber.
      have h_res_eq : result = res_pos.toNumber := by
        apply Number.normalize_eq_of_invariants
          (n := res_pos.toNumber) (result := result) h_rp_min h_rp_max h_rp_exp h_rp_mod h_norm
      -- |result.toRat| = V.
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = V := by
        rw [h_res_eq]; rfl
      rw [h_result_val, hV_def, hT_def]
      -- supTight on the false-sign doRoundDown gives the 5/(2^63-3) bound.
      have h_sup := doRoundDown_rounds_to_nearest_supTight g zm ze' f hf_rep hzm_ge h_zm_le_max
        h_ze_gt h_floor_vac
      simp only at h_sup
      rw [← h_mant_si, ← h_exp_si] at h_sup
      -- h_sup : |V - T| ≤ T * (5/(2^63-3))
      have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 3 : ℕ) : ℚ) := by push_cast; norm_num
      have h_5le6 : (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) ≤ (6 / (2 ^ 63 - 3 : ℚ)) := by
        have : ((2 ^ 63 - 3 : ℕ) : ℚ) = (2 ^ 63 - 3 : ℚ) := by push_cast; norm_num
        rw [this]; apply div_le_div_of_nonneg_right (by norm_num) (by norm_num)
      calc |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ - ((zm.toNat : ℚ) - f) * 10 ^ ze'|
          ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze' * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := h_sup
        _ ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze' * (6 / (2 ^ 63 - 3 : ℚ)) :=
            mul_le_mul_of_nonneg_left h_5le6 h_truth_nn
    · -- ze' = minExponent: no rescale possible (would underflow → zero mantissa).
      have h_ze_eq : ze' = minExponent := le_antisymm (not_lt.mp h_ze_gt) h_ze_ge
      -- Determine res_pos structure from the (sign-false) doRoundDown.
      have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_toNat_val
      have hminMant_v : largeRange.min.toNat = 1000000000000000000 := by decide
      have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := by decide
      have h_m_pos : 1 ≤ zm.toNat := by omega
      have hsub : (zm - 1).toNat = zm.toNat - 1 := m_sub_one_no_underflow h_m_pos
      -- res_pos.mantissa ≠ 0 (false-sign version is sign-independent).
      have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .to_nearest).mantissa_ ≠ 0 := by
        rw [← h_mant_si]; exact hres_pos_mant_ne
      obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hf_rep
      have h_round_values_local : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
          ∨ g.round .to_nearest = -1 := by
        rcases lt_trichotomy f (1/2) with hlt | heq | hgt
        · right; right; exact hround_neg.mpr hlt
        · right; left; exact hround_zero.mpr heq
        · left; exact hround_pos.mpr hgt
      -- Structural facts: res_pos = (m', minExp) with m' ∈ {zm, zm-1}, m' ≥ minMant,
      -- and the half-ULP rounding bound |m' - (zm - f)| ≤ 1/2.
      have h_struct : (res_pos.mantissa_.toNat = zm.toNat ∨ res_pos.mantissa_.toNat = zm.toNat - 1)
          ∧ largeRange.min.toNat ≤ res_pos.mantissa_.toNat
          ∧ res_pos.exponent_ = minExponent
          ∧ |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 / 2 := by
        have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .to_nearest).mantissa_.toNat := by
          rw [h_mant_si]
        rw [hmant_eq_si, h_exp_si]
        rw [h_ze_eq] at h_rdf_mant_ne ⊢
        have hzm_q_pos : (0 : ℚ) ≤ (zm.toNat : ℚ) - 1 := by
          have : (1 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_m_pos
          linarith
        by_cases h_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = true
        · -- round-down: candidate zm - 1.
          by_cases h_m1_lt : (zm - 1) < largeRange.min
          · -- rescale → underflow → mantissa 0: contradiction.
            exfalso
            -- (zm-1)*10 ≥ minMantissa so bringIntoRange's inner rescale doesn't fire; exponent = minExp - 1.
            have hzm1 : (zm - 1).toNat = zm.toNat - 1 := hsub
            have h_le_min : zm.toNat ≤ largeRange.min.toNat := by
              rw [UInt64.lt_iff_toNat_lt, hsub] at h_m1_lt; rw [hminMant_v] at h_m1_lt ⊢; omega
            have h_mul10 : ((zm - 1) * 10).toNat = (zm.toNat - 1) * 10 :=
              m_sub_one_mul_ten_no_overflow h_m_pos h_le_min
            have h_not_resc2 : ¬ ((zm - 1) * 10) < largeRange.min := by
              rw [UInt64.lt_iff_toNat_lt, h_mul10, hminMant_v]; omega
            have : (g.doRoundDown false zm minExponent largeRange.min .to_nearest).mantissa_ = 0 := by
              unfold Guard.doRoundDown Guard.bringIntoRange
              simp only [h_rd, if_true, if_pos h_m1_lt, if_neg h_not_resc2]
              rw [if_pos (show (minExponent - 1 : Int) < minExponent by omega)]
            exact h_rdf_mant_ne this
          · have hres : g.doRoundDown false zm minExponent largeRange.min .to_nearest =
                { negative_ := false, mantissa_ := zm - 1, exponent_ := minExponent } := by
              unfold Guard.doRoundDown
              simp only [h_rd, if_true, if_neg h_m1_lt]
              exact bringIntoRange_value_inRange false (zm - 1) minExponent largeRange.min h_m1_lt (by omega)
            rw [hres]
            have h_m1_ge : largeRange.min.toNat ≤ (zm - 1).toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m1_lt; omega
            -- round-down fired ⟹ f ≥ 1/2 ⟹ |zm-1 - (zm-f)| = 1-f ≤ 1/2.
            have hf_ge : (1 : ℚ) / 2 ≤ f := by
              rw [Bool.or_eq_true] at h_rd
              rcases h_rd with h1 | h0
              · rw [beq_iff_eq] at h1; exact le_of_lt (hround_pos.mp h1)
              · rw [Bool.and_eq_true, beq_iff_eq] at h0; exact le_of_eq (hround_zero.mp h0.1).symm
            have h_half : |((zm - 1).toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 / 2 := by
              rw [hsub, Nat.cast_sub h_m_pos, Nat.cast_one]
              rw [abs_le]; constructor <;> linarith only [hf_ge, hf_lt]
            exact ⟨Or.inr hsub, h_m1_ge, rfl, h_half⟩
        · have h_rd_false : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
            rw [Bool.eq_false_iff]; exact h_rd
          by_cases h_m_lt : zm < largeRange.min
          · -- rescale → underflow → mantissa 0: contradiction.
            exfalso
            have h_le_min : zm.toNat ≤ largeRange.min.toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
            have h_m10 : (zm * 10).toNat = zm.toNat * 10 := mul_ten_no_overflow_of_lt_lr_min h_m_lt
            have h_not_resc2 : ¬ (zm * 10) < largeRange.min := by
              rw [UInt64.lt_iff_toNat_lt, h_m10, hminMant_v]; omega
            have : (g.doRoundDown false zm minExponent largeRange.min .to_nearest).mantissa_ = 0 := by
              unfold Guard.doRoundDown Guard.bringIntoRange
              simp only [h_rd_false, Bool.false_eq_true, if_false, if_pos h_m_lt]
              rw [if_pos (show (minExponent - 1 : Int) < minExponent by omega)]
            exact h_rdf_mant_ne this
          · have hres : g.doRoundDown false zm minExponent largeRange.min .to_nearest =
                { negative_ := false, mantissa_ := zm, exponent_ := minExponent } := by
              unfold Guard.doRoundDown
              simp only [h_rd_false, Bool.false_eq_true, if_false]
              exact bringIntoRange_value_inRange false zm minExponent largeRange.min h_m_lt (by omega)
            rw [hres]
            have h_m_ge : largeRange.min.toNat ≤ zm.toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
            -- round-down didn't fire ⟹ f ≤ 1/2 ⟹ |zm - (zm-f)| = f ≤ 1/2.
            have hf_le : f ≤ (1 : ℚ) / 2 := by
              rw [Bool.or_eq_false_iff] at h_rd_false
              obtain ⟨h1, h0⟩ := h_rd_false
              rw [beq_eq_false_iff_ne] at h1
              by_cases hr0 : g.round .to_nearest = 0
              · exact le_of_eq (hround_zero.mp hr0)
              · -- round ≠ 1, round ≠ 0 ⟹ round = -1 ⟹ f < 1/2.
                have hrneg : g.round .to_nearest = -1 := by
                  rcases h_round_values_local with hr | hr | hr
                  · exact absurd hr h1
                  · exact absurd hr hr0
                  · exact hr
                exact le_of_lt (hround_neg.mp hrneg)
            have h_half : |(zm.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 / 2 := by
              rw [abs_le]; constructor <;> linarith only [hf_nn, hf_le]
            exact ⟨Or.inl rfl, h_m_ge, rfl, h_half⟩
      obtain ⟨h_mant_cases, h_rp_min, h_rp_exp, h_half_ulp⟩ := h_struct
      -- normalize identity.
      have h_rp_max : res_pos.toNumber.mantissa_.toNat ≤ largeRange.max.toNat := by
        change res_pos.mantissa_.toNat ≤ largeRange.max.toNat
        rcases h_mant_cases with h | h <;> rw [h] <;> rw [hmaxMant_v] <;> omega
      have h_rp_exp' : minExponent ≤ res_pos.toNumber.exponent_ := by
        change minExponent ≤ res_pos.exponent_; rw [h_rp_exp]
      have h_rp_mod : res_pos.toNumber.mantissa_.toNat > maxRep.toNat →
          res_pos.toNumber.mantissa_.toNat % 10 = 0 := by
        change res_pos.mantissa_.toNat > maxRep.toNat → _
        rcases h_mant_cases with h | h <;> rw [h] <;> intro hgt <;> rw [hmaxRep_v] at hgt <;> omega
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants
          (show largeRange.min.toNat ≤ res_pos.toNumber.mantissa_.toNat from h_rp_min)
          h_rp_max h_rp_exp' h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = V := by
        rw [h_res_eq]; rfl
      rw [h_result_val, hV_def, hT_def, h_rp_exp, h_ze_eq]
      -- Bound: |m' * 10^minExp - (zm - f)*10^minExp| ≤ (zm-f)*10^minExp * (6/(2^63-3)).
      have h10min_pos : (0 : ℚ) < 10 ^ (minExponent : Int) := zpow_pos (by norm_num) _
      have h10min_nn : (0 : ℚ) ≤ 10 ^ (minExponent : Int) := le_of_lt h10min_pos
      have h_denom_pos : (0 : ℚ) < (2 ^ 63 - 3 : ℚ) := by norm_num
      have h_factor : (res_pos.mantissa_.toNat : ℚ) * 10 ^ (minExponent : Int) - ((zm.toNat : ℚ) - f) * 10 ^ (minExponent : Int)
          = ((res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)) * 10 ^ (minExponent : Int) := by ring
      have h_target_factor : ((zm.toNat : ℚ) - f) * 10 ^ (minExponent : Int) * (6 / (2 ^ 63 - 3 : ℚ))
          = (((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ))) * 10 ^ (minExponent : Int) := by ring
      rw [h_factor, h_target_factor, abs_mul, abs_of_nonneg h10min_nn]
      apply mul_le_mul_of_nonneg_right _ h10min_nn
      -- |m' - (zm - f)| ≤ 1/2 ≤ (zm - f) * (6/(2^63-3))
      calc |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)|
          ≤ 1 / 2 := h_half_ulp
        _ ≤ ((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ)) := by
            rw [show ((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ))
                  = (6 * ((zm.toNat : ℚ) - f)) / (2 ^ 63 - 3 : ℚ) from by ring]
            rw [le_div_iff₀ h_denom_pos]
            -- (1/2)*(2^63-3) ≤ 6*(zm-f), since zm-f ≥ floor-1 = 922337203685477579.
            nlinarith [hzm_q_ge, hf_nn, hf_lt]
  · -- CASE B: zm > maxRep. doRoundDown does not rescale (mantissa stays > minMantissa).
    push_neg at h_zm_le_max
    have h_zm_gt_maxRep : maxRep.toNat < zm.toNat := h_zm_le_max
    have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_toNat_val
    have hminMant_v : largeRange.min.toNat = 1000000000000000000 := by decide
    have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := by decide
    have h_m_pos : 1 ≤ zm.toNat := by omega
    have hsub : (zm - 1).toNat = zm.toNat - 1 := m_sub_one_no_underflow h_m_pos
    obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hf_rep
    have h_round_values_local : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
        ∨ g.round .to_nearest = -1 := by
      rcases lt_trichotomy f (1/2) with hlt | heq | hgt
      · right; right; exact hround_neg.mpr hlt
      · right; left; exact hround_zero.mpr heq
      · left; exact hround_pos.mpr hgt
    have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .to_nearest).mantissa_ ≠ 0 := by
      rw [← h_mant_si]; exact hres_pos_mant_ne
    -- Structure: res_pos.exponent_ = ze', res_pos.mantissa ∈ {zm, zm-1}, half-ULP bound,
    -- res_pos.mantissa.toNat ≥ maxRep.toNat (so ≥ minMantissa).
    have h_struct : res_pos.exponent_ = ze'
        ∧ (res_pos.mantissa_.toNat = zm.toNat ∨ res_pos.mantissa_.toNat = zm.toNat - 1)
        ∧ (maxRep.toNat ≤ res_pos.mantissa_.toNat)
        ∧ |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 / 2 := by
      have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .to_nearest).mantissa_.toNat := by
        rw [h_mant_si]
      rw [hmant_eq_si, h_exp_si]
      have hzm_q_pos : (0 : ℚ) ≤ (zm.toNat : ℚ) - 1 := by
        have : (1 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_m_pos
        linarith
      by_cases h_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = true
      · -- round-down: m - 1 ≥ maxRep ≥ minMantissa, no rescale.
        have h_m1_ge : largeRange.min.toNat ≤ (zm - 1).toNat := by rw [hsub]; omega
        have h_m1_not_lt : ¬ (zm - 1) < largeRange.min := by
          rw [UInt64.lt_iff_toNat_lt]; omega
        have hres : g.doRoundDown false zm ze' largeRange.min .to_nearest =
            { negative_ := false, mantissa_ := zm - 1, exponent_ := ze' } := by
          unfold Guard.doRoundDown
          simp only [h_rd, if_true, if_neg h_m1_not_lt]
          exact bringIntoRange_value_inRange false (zm - 1) ze' largeRange.min h_m1_not_lt (by omega)
        rw [hres]
        have hf_ge : (1 : ℚ) / 2 ≤ f := by
          rw [Bool.or_eq_true] at h_rd
          rcases h_rd with h1 | h0
          · rw [beq_iff_eq] at h1; exact le_of_lt (hround_pos.mp h1)
          · rw [Bool.and_eq_true, beq_iff_eq] at h0; exact le_of_eq (hround_zero.mp h0.1).symm
        refine ⟨rfl, Or.inr hsub, by rw [hsub]; omega, ?_⟩
        rw [hsub, Nat.cast_sub h_m_pos, Nat.cast_one, abs_le]
        constructor <;> linarith only [hf_ge, hf_lt]
      · have h_rd_false : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
          rw [Bool.eq_false_iff]; exact h_rd
        have h_m_not_lt : ¬ zm < largeRange.min := by rw [UInt64.lt_iff_toNat_lt]; omega
        have hres : g.doRoundDown false zm ze' largeRange.min .to_nearest =
            { negative_ := false, mantissa_ := zm, exponent_ := ze' } := by
          unfold Guard.doRoundDown
          simp only [h_rd_false, Bool.false_eq_true, if_false]
          exact bringIntoRange_value_inRange false zm ze' largeRange.min h_m_not_lt (by omega)
        rw [hres]
        have hf_le : f ≤ (1 : ℚ) / 2 := by
          rw [Bool.or_eq_false_iff] at h_rd_false
          obtain ⟨h1, _h0⟩ := h_rd_false
          rw [beq_eq_false_iff_ne] at h1
          by_cases hr0 : g.round .to_nearest = 0
          · exact le_of_eq (hround_zero.mp hr0)
          · have hrneg : g.round .to_nearest = -1 := by
              rcases h_round_values_local with hr | hr | hr
              · exact absurd hr h1
              · exact absurd hr hr0
              · exact hr
            exact le_of_lt (hround_neg.mp hrneg)
        refine ⟨rfl, Or.inl rfl, le_of_lt h_zm_gt_maxRep, ?_⟩
        rw [abs_le]; constructor <;> linarith only [hf_nn, hf_le]
    obtain ⟨h_rp_exp, h_rp_cases, h_rp_ge_maxRep, h_half_ulp⟩ := h_struct
    have h10ze_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze_pos
    have h_denom_pos : (0 : ℚ) < (2 ^ 63 - 3 : ℚ) := by norm_num
    have h_rp_min : largeRange.min.toNat ≤ res_pos.mantissa_.toNat := by omega
    by_cases h_rp_le_max : res_pos.mantissa_.toNat ≤ maxRep.toNat
    · -- SUB-CASE B1: res_pos.mantissa = maxRep, normalize identity.
      have h_rp_max : res_pos.toNumber.mantissa_.toNat ≤ largeRange.max.toNat := by
        change res_pos.mantissa_.toNat ≤ largeRange.max.toNat; omega
      have h_rp_exp' : minExponent ≤ res_pos.toNumber.exponent_ := by
        change minExponent ≤ res_pos.exponent_; rw [h_rp_exp]; exact h_ze_ge
      have h_rp_mod : res_pos.toNumber.mantissa_.toNat > maxRep.toNat →
          res_pos.toNumber.mantissa_.toNat % 10 = 0 := by
        change res_pos.mantissa_.toNat > maxRep.toNat → _; intro hgt; omega
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants
          (show largeRange.min.toNat ≤ res_pos.toNumber.mantissa_.toNat from h_rp_min)
          h_rp_max h_rp_exp' h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ ze' := by
        rw [h_res_eq]; change (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = _; rw [h_rp_exp]
      rw [h_result_val, hT_def]
      have h_factor : (res_pos.mantissa_.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'
          = ((res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)) * 10 ^ ze' := by ring
      have h_target_factor : ((zm.toNat : ℚ) - f) * 10 ^ ze' * (6 / (2 ^ 63 - 3 : ℚ))
          = (((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ))) * 10 ^ ze' := by ring
      rw [h_factor, h_target_factor, abs_mul, abs_of_nonneg h10ze_nn]
      apply mul_le_mul_of_nonneg_right _ h10ze_nn
      calc |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)|
          ≤ 1 / 2 := h_half_ulp
        _ ≤ ((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ)) := by
            rw [show ((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ))
                  = (6 * ((zm.toNat : ℚ) - f)) / (2 ^ 63 - 3 : ℚ) from by ring]
            rw [le_div_iff₀ h_denom_pos]
            nlinarith [hzm_q_ge, hf_nn, hf_lt]
    · -- SUB-CASE B2: res_pos.mantissa > maxRep, capAtMaxRep + doRoundUp fires.
      push_neg at h_rp_le_max
      set m : UInt64 := res_pos.mantissa_ with hm_def
      have hm_gt : maxRep < m := UInt64.lt_iff_toNat_lt.mpr h_rp_le_max
      have hm_le_maxMant : m.toNat ≤ largeRange.max.toNat := by
        rw [hmaxMant_v]; rcases h_rp_cases with h | h <;> rw [hm_def, h] <;> omega
      -- m/10 ∈ [floor, maxRep].
      have hm_div_toNat : (m / 10).toNat = m.toNat / 10 := by
        rw [UInt64.toNat_div]; rfl
      have hm_div_ge : (mantissaFloor : ℕ) ≤ (m / 10).toNat := by
        rw [hm_div_toNat]; omega
      have hm_div_le : (m / 10).toNat ≤ maxRep.toNat := by
        rw [hm_div_toNat, hmaxRep_v, hmaxMant_v] at *; omega
      -- ze' < maxExponent (else capAtMaxRep errors).
      have h_ze_lt_max : ze' < maxExponent := by
        by_contra h
        push_neg at h
        have h_cap_err : doNormalize_capAtMaxRep res_pos.toNumber.mantissa_ res_pos.toNumber.exponent_
            (if res_pos.toNumber.negative_ then Guard.new.set_negative else Guard.new)
            = .error "Number::normalize 1.5" := by
          unfold doNormalize_capAtMaxRep
          rw [if_pos (show maxRep < res_pos.toNumber.mantissa_ from hm_gt)]
          rw [if_pos (show res_pos.toNumber.exponent_ ≥ maxExponent from by
            change res_pos.exponent_ ≥ maxExponent; rw [h_rp_exp]; exact h)]
        -- normalize would error: contradiction with h_norm.
        have h_norm2 : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
            res_pos.toNumber.exponent_ largeRange.min largeRange.max .to_nearest = .ok result := h_norm
        unfold doNormalize at h_norm2
        rw [beq_eq_false_iff_ne.mpr (show res_pos.toNumber.mantissa_ ≠ 0 from hres_pos_mant_ne)] at h_norm2
        simp only [Bool.false_eq_true, if_false] at h_norm2
        have h_min_le : largeRange.min ≤ res_pos.toNumber.mantissa_ := by
          change largeRange.min ≤ m; rw [UInt64.le_iff_toNat_le]; omega
        have h_max_le : res_pos.toNumber.mantissa_ ≤ largeRange.max :=
          UInt64.le_iff_toNat_le.mpr hm_le_maxMant
        rw [doNormalize_scaleUp_id largeRange.min _ _ h_min_le] at h_norm2
        rw [doNormalize_scaleDown_id largeRange.max _ _ _ h_max_le] at h_norm2
        simp only [] at h_norm2
        have h_no_under_mant : ¬ res_pos.toNumber.mantissa_ < largeRange.min := by
          rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp h_min_le)
        have h_no_under_exp : ¬ res_pos.toNumber.exponent_ < minExponent := by
          change ¬ res_pos.exponent_ < minExponent; rw [h_rp_exp]; exact not_lt.mpr h_ze_ge
        have h_check_false : (res_pos.toNumber.exponent_ < minExponent || res_pos.toNumber.mantissa_ < largeRange.min) = false := by
          simp [h_no_under_exp, h_no_under_mant]
        rw [h_check_false] at h_norm2
        simp only [Bool.false_eq_true, if_false] at h_norm2
        rw [h_cap_err] at h_norm2
        exact absurd h_norm2 (by intro hc; cases hc)
      -- Reduce normalize to a single doRoundUp via the cap lemma.
      have h_neg_eq : res_pos.toNumber.negative_ = zn := h_res_pos_neg
      have h_reduce := doNormalize_capAtMaxRep_reduce res_pos.toNumber.negative_ m res_pos.exponent_ .to_nearest
        hm_gt hm_le_maxMant (h_rp_exp ▸ h_ze_ge) (h_rp_exp ▸ h_ze_lt_max)
      -- h_norm = doNormalize res_pos.neg m res_pos.exp = .ok result. Combine with reduce.
      have h_norm2 : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
          res_pos.toNumber.exponent_ largeRange.min largeRange.max .to_nearest = .ok result := h_norm
      rw [show res_pos.toNumber.mantissa_ = m from rfl,
          show res_pos.toNumber.exponent_ = res_pos.exponent_ from rfl, h_reduce] at h_norm2
      -- Extract the doRoundUp result.
      set G : Guard := (if res_pos.toNumber.negative_ then Guard.new.set_negative else Guard.new).push (m % 10) with hG_def
      cases hru : G.doRoundUp res_pos.toNumber.negative_ (m / 10) (res_pos.exponent_ + 1)
          largeRange.min largeRange.max .to_nearest "Number::normalize 2" with
      | error err => rw [hru] at h_norm2; simp only [Except.map] at h_norm2; exact absurd h_norm2 (by intro hc; cases hc)
      | ok res2 =>
        rw [hru] at h_norm2
        simp only [Except.map] at h_norm2
        have h_result_eq : result = res2.toNumber := (Except.ok.inj h_norm2).symm
        -- res2.mantissa ≠ 0.
        have hres2_mant_ne : res2.mantissa_ ≠ 0 := by
          rw [h_result_eq] at hresult; exact hresult
        -- Guard G represents frac := (m%10).toNat / 10.
        have hd_lt : (m % 10).toNat < 10 := by
          rw [UInt64.toNat_mod]; exact Nat.mod_lt _ (by decide)
        have hG_rep : represents G ((0 + (m % 10).toNat) / 10) := by
          rw [hG_def]
          exact represents_push (represents_initial_guard_eq res_pos.toNumber.negative_) hd_lt
        set frac : ℚ := (0 + (m % 10).toNat) / 10 with hfrac_def
        -- supTight on the FALSE-sign doRoundUp.
        have h_false := doRoundUp_false_from_ok G res_pos.toNumber.negative_ (m / 10)
          (res_pos.exponent_ + 1) .to_nearest "Number::normalize 2" res2 hru
        have hres2f_mant_ne : ({ negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ } : RoundResult).mantissa_ ≠ 0 := hres2_mant_ne
        have h_floor_vac2 : (m / 10).toNat = mantissaFloor → (8 : ℚ) / 10 ≤ frac := by
          intro h
          -- m/10 = floor and maxRep < m ≤ maxMant ⟹ m ∈ {9223372036854775808, 9223372036854775809},
          -- so m % 10 ∈ {8, 9}, hence frac = (m%10)/10 ≥ 8/10.
          rw [hm_div_toNat] at h
          have hm_range : m.toNat = 9223372036854775808 ∨ m.toNat = 9223372036854775809 := by omega
          have hmod : (m % 10).toNat = 8 ∨ (m % 10).toNat = 9 := by
            rw [UInt64.toNat_mod]; rcases hm_range with h' | h' <;> rw [show (10 : UInt64).toNat = 10 from rfl, h'] <;> omega
          rw [hfrac_def]
          rcases hmod with h' | h' <;> rw [h'] <;> norm_num
        have h_sup := doRoundUp_rounds_to_nearest_supTight G (m / 10) (res_pos.exponent_ + 1) frac
          hG_rep hm_div_ge hm_div_le h_floor_vac2 "Number::normalize 2"
          { negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ }
          h_false hres2f_mant_ne
        -- h_sup : |res2_value - ((m/10) + frac)*10^(ze'+1)| ≤ ((m/10)+frac)*10^(ze'+1) * (5/(2^63+7))
        simp only at h_sup
        -- |result.toRat-value| = res2_value (since result = res2.toNumber).
        have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
            = (res2.mantissa_.toNat : ℚ) * 10 ^ res2.exponent_ := by rw [h_result_eq]; rfl
        -- value equation: ((m/10).toNat + frac)*10^(ze'+1) = m.toNat * 10^ze'.
        have hVeq : (((m / 10).toNat : ℚ) + frac) * 10 ^ (res_pos.exponent_ + 1)
            = (m.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
          rw [hfrac_def, hm_div_toNat]
          have heucl : (10 : ℚ) * ((m.toNat / 10 : ℕ) : ℚ) + ((m.toNat % 10 : ℕ) : ℚ) = (m.toNat : ℚ) := by
            have he : 10 * (m.toNat / 10) + m.toNat % 10 = m.toNat := Nat.div_add_mod m.toNat 10
            have : ((10 * (m.toNat / 10) + m.toNat % 10 : ℕ) : ℚ) = (m.toNat : ℚ) := by rw [he]
            push_cast at this; linarith
          have hmod_cast : ((m % 10).toNat : ℚ) = ((m.toNat % 10 : ℕ) : ℚ) := by
            rw [UInt64.toNat_mod]; rfl
          rw [hmod_cast]
          rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_one]
          rw [← heucl]; ring
        -- res_pos value V and truth T, both at scale 10^ze'.
        rw [h_rp_exp] at h_sup hVeq
        rw [hVeq] at h_sup
        rw [h_result_val, hT_def]
        -- m.toNat = res_pos.mantissa_.toNat; cast.
        have hm_eq_rp : (m.toNat : ℚ) = (res_pos.mantissa_.toNat : ℚ) := by rw [hm_def]
        -- h_sup : |res2_value - m.toNat*10^ze'| ≤ m.toNat*10^ze' * (5/(2^63+7))
        -- half-ULP: |m.toNat - (zm-f)| ≤ 1/2.
        have h_half : |(m.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 / 2 := by
          rw [hm_eq_rp]; exact h_half_ulp
        -- m.toNat ≤ zm.toNat.
        have hm_le_zm_nat : m.toNat ≤ zm.toNat := by
          have : m.toNat = res_pos.mantissa_.toNat := by rw [hm_def]
          rw [this]; rcases h_rp_cases with h | h <;> omega
        have hm_le_zm : (m.toNat : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hm_le_zm_nat
        have hm_ge_maxRep_nat : maxRep.toNat < m.toNat := by rw [hm_def]; exact h_rp_le_max
        have hm_pos_q : (maxRepNat : ℚ) < (m.toNat : ℚ) := by
          rw [hmaxRep_v] at hm_ge_maxRep_nat; exact_mod_cast hm_ge_maxRep_nat
        set W : ℚ := (res2.mantissa_.toNat : ℚ) * 10 ^ res2.exponent_ with hW_def
        have hsplit : |W - ((zm.toNat : ℚ) - f) * 10 ^ ze'|
            ≤ |W - (m.toNat : ℚ) * 10 ^ ze'|
              + |(m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'| :=
          abs_sub_le _ _ _
        have hVT : |(m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'| ≤ (1/2) * 10 ^ ze' := by
          rw [show (m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'
                = ((m.toNat : ℚ) - ((zm.toNat : ℚ) - f)) * 10 ^ ze' from by ring,
              abs_mul, abs_of_nonneg h10ze_nn]
          exact mul_le_mul_of_nonneg_right h_half h10ze_nn
        -- now bound.
        have h_denom1_val : ((2 ^ 63 + 7 : ℕ) : ℚ) = 9223372036854775815 := by push_cast; norm_num
        calc |W - ((zm.toNat : ℚ) - f) * 10 ^ ze'|
            ≤ |W - (m.toNat : ℚ) * 10 ^ ze'|
                + |(m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'| := hsplit
          _ ≤ (m.toNat : ℚ) * 10 ^ ze' * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) + (1/2) * 10 ^ ze' := by
              apply add_le_add _ hVT
              exact h_sup
          _ ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze' * (6 / (2 ^ 63 - 3 : ℚ)) := by
              rw [h_denom1_val]
              rw [show (m.toNat : ℚ) * 10 ^ ze' * (5 / 9223372036854775815) + (1/2) * 10 ^ ze'
                    = ((m.toNat : ℚ) * (5 / 9223372036854775815) + 1/2) * 10 ^ ze' from by ring]
              rw [show ((zm.toNat : ℚ) - f) * 10 ^ ze' * (6 / (2 ^ 63 - 3 : ℚ))
                    = (((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ))) * 10 ^ ze' from by ring]
              apply mul_le_mul_of_nonneg_right _ h10ze_nn
              -- (m*5/D1 + 1/2) ≤ (zm-f)*6/D2, D1 = 2^63+7, D2 = 2^63-3.
              have hD1 : (0 : ℚ) < 9223372036854775815 := by norm_num
              have hD2 : (0 : ℚ) < (2 ^ 63 - 3 : ℚ) := by norm_num
              have hlhs : (m.toNat : ℚ) * (5 / 9223372036854775815) + 1/2
                  = (10 * (m.toNat : ℚ) + 9223372036854775815) / (2 * 9223372036854775815) := by
                field_simp; ring
              have hrhs : ((zm.toNat : ℚ) - f) * (6 / (2 ^ 63 - 3 : ℚ))
                  = (6 * ((zm.toNat : ℚ) - f)) / (2 ^ 63 - 3 : ℚ) := by ring
              rw [hlhs, hrhs, div_le_div_iff₀ (by norm_num) hD2]
              -- (10*m + D1)*(2^63-3) ≤ 6*(zm-f) * (2*D1)
              nlinarith [hm_le_zm, hm_pos_q, hf_nn, hf_lt, hzm_q_ge,
                (by exact_mod_cast h_zm_gt_maxRep : (maxRepNat : ℚ) < (zm.toNat : ℚ))]

end XRPL.Model.Protocol

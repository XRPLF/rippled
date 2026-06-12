import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.DiffSignTail
import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts.DiffSignRepresents
import XRPL.Properties.Protocol.Number.Common.Helpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- For the diff-sign Add guard with `zn = false` (non-negative truth), if the
`.downward` rounding does **not** trigger a round-down, then the represented
fraction `f` is exactly `0`.

`¬ shouldRoundUp_downward` means `¬ (sbit ∧ (digits>0 ∨ xbit))`. Combined with
the diff-sign sticky-bit fact `sbit = true ∨ (digits = 0 ∧ xbit = false)`, both
disjuncts force `digits = 0 ∧ xbit = false`, hence `f = 0` (the guard carries no
fractional tail). -/
private lemma diff_sign_f_zero_of_no_roundDown_downward (g : Guard) (f : ℚ)
    (hrep : represents g f)
    (hsbit : g.sbit_ = true ∨ (g.digits_ = 0 ∧ g.xbit_ = false))
    (hno : ¬ g.shouldRoundUp_downward) : f = 0 := by
  unfold Guard.shouldRoundUp_downward at hno
  push_neg at hno
  rcases hsbit with hs | ⟨hd, hx⟩
  · obtain ⟨hdig0, hxbit0⟩ := hno hs
    have hdig : g.digits_ = 0 := by
      by_contra hne
      exact hdig0 (UInt64.pos_iff_ne_zero.mpr hne)
    have hxb : g.xbit_ = false := by
      cases hxc : g.xbit_ with
      | false => rfl
      | true => exact absurd hxc hxbit0
    exact represents_eq_zero_of_digits_zero_xbit_false hdig hxb hrep
  · exact represents_eq_zero_of_digits_zero_xbit_false hd hx hrep

/-- Local (file-private) form: the `roundUp` boolean inside `doRoundDown`/`doRoundUp`
equals `true` for `.downward` exactly when `g.shouldRoundUp_downward`. -/
private lemma roundUp_bool_dn_true (g : Guard) (m : UInt64)
    (h : g.shouldRoundUp_downward) :
    ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = true := by
  rw [(round_downward_eq_one_iff g).mpr h]; rfl

private lemma roundUp_bool_dn_false (g : Guard) (m : UInt64)
    (h : ¬ g.shouldRoundUp_downward) :
    ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = false := by
  rw [(round_downward_eq_neg_one_iff g).mpr h]; rfl

set_option maxHeartbeats 1600000 in
-- Large case analysis (alignment cases × rounding branches) over the full
-- doRoundDown + normalize pipeline for `.downward`.
theorem operator_add_rounding_bound_diff_sign_downward_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  obtain ⟨zm, ze', f, zn, g, res_pos, hf_rep, hzm_gt, hzm_lt, habs_xy_eq, h_rdown,
          h_norm, hsign, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .downward hx hy hx_mant_ne hy_mant_ne
      h_diff_sign h_not_zero hok hresult
  have h_norm' : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
      res_pos.toNumber.exponent_ largeRange.min largeRange.max .downward = .ok result := h_norm
  have hres_pos_mant_ne : res_pos.toNumber.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result h_norm hresult
  have h_res_pos_neg : res_pos.toNumber.negative_ = zn := by
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
  -- Shared facts.
  have hzm_ge : (mantissaFloor : ℕ) ≤ zm.toNat := le_of_lt hzm_gt
  have hf_nn : 0 ≤ f := represents_nonneg hf_rep
  have hf_lt : f < 1 := represents_lt_one hf_rep
  have h10ze_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by
    have : (mantissaFloorSucc : ℕ) ≤ zm.toNat := hzm_gt
    exact_mod_cast this
  have h_truth_pos : 0 < ((zm.toNat : ℚ) - f) * 10 ^ ze' := by
    apply mul_pos _ h10ze_pos; linarith
  have h_truth_nn : 0 ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze' := le_of_lt h_truth_pos
  -- doRoundDown input exponent ≥ minExponent.
  have h_rd_mant_ne : (g.doRoundDown zn zm ze' largeRange.min .downward).mantissa_ ≠ 0 := by
    rw [h_rdown]; exact hres_pos_mant_ne
  have h_ze_ge : minExponent ≤ ze' :=
    doRoundDown_input_exp_ge_minExp_of_mant_ne g zn zm ze' largeRange.min .downward h_rd_mant_ne
  -- Bridge res_pos to the sign-false doRoundDown.
  have h_mant_si : res_pos.mantissa_ = (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_ := by
    rw [← h_rdown]; exact doRoundDown_mantissa_sign_indep g zn zm ze' largeRange.min .downward
  have h_exp_si : res_pos.exponent_ = (g.doRoundDown false zm ze' largeRange.min .downward).exponent_ := by
    rw [← h_rdown]; exact doRoundDown_exponent_sign_indep g zn zm ze' largeRange.min .downward
  set V : ℚ := (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ with hV_def
  set T : ℚ := ((zm.toNat : ℚ) - f) * 10 ^ ze' with hT_def
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_toNat_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := by decide
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := by decide
  have h_denom_pos : (0 : ℚ) < (2 ^ 63 - 18 : ℚ) := by norm_num
  -- the full-ULP bound 1 ≤ (zm - f) * (11/(2^63-18))
  have h_oneULP : (1 : ℚ) < ((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ)) := by
    rw [show ((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ))
          = (11 * ((zm.toNat : ℚ) - f)) / (2 ^ 63 - 18 : ℚ) from by ring]
    rw [lt_div_iff₀ h_denom_pos]
    nlinarith [hzm_q_gt, hf_nn, hf_lt]
  -- Main case split.
  by_cases h_zm_le_max : zm.toNat ≤ maxRep.toNat
  · by_cases h_ze_gt : minExponent < ze'
    · -- CASE A: supTight applies directly; normalize is identity.
      have h_inv := doRoundDown_output_in_range_of_floor g false zm ze' .downward hzm_ge h_zm_le_max h_ze_gt
      simp only at h_inv
      obtain ⟨h_rp_min, h_rp_max, h_rp_exp, h_rp_mod⟩ := h_inv
      rw [← h_mant_si] at h_rp_min h_rp_max h_rp_mod
      rw [← h_exp_si] at h_rp_exp
      have h_res_eq : result = res_pos.toNumber := by
        apply Number.normalize_eq_of_invariants_downward
          (n := res_pos.toNumber) (result := result) h_rp_min h_rp_max h_rp_exp h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = V := by
        rw [h_res_eq]; rfl
      rw [h_result_val, hV_def, hT_def]
      have h_sup := doRoundDown_rounds_downward_supTight g zm ze' f hf_rep hzm_ge h_zm_le_max h_ze_gt
      simp only at h_sup
      rw [← h_mant_si, ← h_exp_si] at h_sup
      have h_denom_pos' : (0 : ℚ) < ((2 ^ 63 - 18 : ℕ) : ℚ) := by push_cast; norm_num
      have h_10lt11 : (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) < (11 / (2 ^ 63 - 18 : ℚ)) := by
        have : ((2 ^ 63 - 18 : ℕ) : ℚ) = (2 ^ 63 - 18 : ℚ) := by push_cast; norm_num
        rw [this]; norm_num
      calc |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ - ((zm.toNat : ℚ) - f) * 10 ^ ze'|
          ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze' * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := h_sup
        _ < ((zm.toNat : ℚ) - f) * 10 ^ ze' * (11 / (2 ^ 63 - 18 : ℚ)) :=
            mul_lt_mul_of_pos_left h_10lt11 h_truth_pos
    · -- BOUNDARY: ze' = minExponent.
      have h_ze_eq : ze' = minExponent := le_antisymm (not_lt.mp h_ze_gt) h_ze_ge
      have h_m_pos : 1 ≤ zm.toNat := by omega
      have hsub : (zm - 1).toNat = zm.toNat - 1 := m_sub_one_no_underflow h_m_pos
      have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_ ≠ 0 := by
        rw [← h_mant_si]; exact hres_pos_mant_ne
      -- Structure: res_pos = (m', minExp) with m' ∈ {zm, zm-1}, half-ULP→full-ULP bound.
      have h_struct : (res_pos.mantissa_.toNat = zm.toNat ∨ res_pos.mantissa_.toNat = zm.toNat - 1)
          ∧ largeRange.min.toNat ≤ res_pos.mantissa_.toNat
          ∧ res_pos.exponent_ = minExponent
          ∧ |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
        have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_.toNat := by
          rw [h_mant_si]
        rw [hmant_eq_si, h_exp_si]
        rw [h_ze_eq] at h_rdf_mant_ne ⊢
        by_cases h_rd : g.shouldRoundUp_downward
        · -- round-down: candidate zm - 1.
          have h_rd_bool : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
            roundUp_bool_dn_true g zm h_rd
          by_cases h_m1_lt : (zm - 1) < largeRange.min
          · -- rescale → underflow → mantissa 0: contradiction.
            exfalso
            have h_le_min : zm.toNat ≤ largeRange.min.toNat := by
              rw [UInt64.lt_iff_toNat_lt, hsub] at h_m1_lt; rw [hminMant_v] at h_m1_lt ⊢; omega
            have h_mul10 : ((zm - 1) * 10).toNat = (zm.toNat - 1) * 10 :=
              m_sub_one_mul_ten_no_overflow h_m_pos h_le_min
            have h_not_resc2 : ¬ ((zm - 1) * 10) < largeRange.min := by
              rw [UInt64.lt_iff_toNat_lt, h_mul10, hminMant_v]; omega
            have : (g.doRoundDown false zm minExponent largeRange.min .downward).mantissa_ = 0 := by
              unfold Guard.doRoundDown Guard.bringIntoRange
              simp only [h_rd_bool, if_true, if_pos h_m1_lt, if_neg h_not_resc2]
              rw [if_pos (show (minExponent - 1 : Int) < minExponent by omega)]
            exact h_rdf_mant_ne this
          · have hres : g.doRoundDown false zm minExponent largeRange.min .downward =
                { negative_ := false, mantissa_ := zm - 1, exponent_ := minExponent } := by
              unfold Guard.doRoundDown
              simp only [h_rd_bool, if_true, if_neg h_m1_lt]
              exact bringIntoRange_value_inRange false (zm - 1) minExponent largeRange.min h_m1_lt (by omega)
            rw [hres]
            have h_m1_ge : largeRange.min.toNat ≤ (zm - 1).toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m1_lt; omega
            have h_full : |((zm - 1).toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
              rw [hsub, Nat.cast_sub h_m_pos, Nat.cast_one]
              rw [abs_le]; constructor <;> linarith only [hf_nn, hf_lt]
            exact ⟨Or.inr hsub, h_m1_ge, rfl, h_full⟩
        · have h_rd_false : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = false :=
            roundUp_bool_dn_false g zm h_rd
          by_cases h_m_lt : zm < largeRange.min
          · exfalso
            have h_le_min : zm.toNat ≤ largeRange.min.toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
            have h_m10 : (zm * 10).toNat = zm.toNat * 10 := mul_ten_no_overflow_of_lt_lr_min h_m_lt
            have h_not_resc2 : ¬ (zm * 10) < largeRange.min := by
              rw [UInt64.lt_iff_toNat_lt, h_m10, hminMant_v]; omega
            have : (g.doRoundDown false zm minExponent largeRange.min .downward).mantissa_ = 0 := by
              unfold Guard.doRoundDown Guard.bringIntoRange
              simp only [h_rd_false, Bool.false_eq_true, if_false, if_pos h_m_lt]
              rw [if_pos (show (minExponent - 1 : Int) < minExponent by omega)]
            exact h_rdf_mant_ne this
          · have hres : g.doRoundDown false zm minExponent largeRange.min .downward =
                { negative_ := false, mantissa_ := zm, exponent_ := minExponent } := by
              unfold Guard.doRoundDown
              simp only [h_rd_false, Bool.false_eq_true, if_false]
              exact bringIntoRange_value_inRange false zm minExponent largeRange.min h_m_lt (by omega)
            rw [hres]
            have h_m_ge : largeRange.min.toNat ≤ zm.toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
            have h_full : |(zm.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
              rw [abs_le]; constructor <;> linarith only [hf_nn, hf_lt]
            exact ⟨Or.inl rfl, h_m_ge, rfl, h_full⟩
      obtain ⟨h_mant_cases, h_rp_min, h_rp_exp, h_full_ulp⟩ := h_struct
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
        Number.normalize_eq_of_invariants_downward
          (show largeRange.min.toNat ≤ res_pos.toNumber.mantissa_.toNat from h_rp_min)
          h_rp_max h_rp_exp' h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = V := by
        rw [h_res_eq]; rfl
      rw [h_result_val, hV_def, hT_def, h_rp_exp, h_ze_eq]
      have h10min_nn : (0 : ℚ) ≤ 10 ^ (minExponent : Int) := le_of_lt (zpow_pos (by norm_num) _)
      have h_factor : (res_pos.mantissa_.toNat : ℚ) * 10 ^ (minExponent : Int) - ((zm.toNat : ℚ) - f) * 10 ^ (minExponent : Int)
          = ((res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)) * 10 ^ (minExponent : Int) := by ring
      have h_target_factor : ((zm.toNat : ℚ) - f) * 10 ^ (minExponent : Int) * (11 / (2 ^ 63 - 18 : ℚ))
          = (((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ))) * 10 ^ (minExponent : Int) := by ring
      rw [h_factor, h_target_factor, abs_mul, abs_of_nonneg h10min_nn]
      apply mul_lt_mul_of_pos_right _ (zpow_pos (by norm_num : (0 : ℚ) < 10) _)
      calc |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)|
          ≤ 1 := h_full_ulp
        _ < ((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ)) := h_oneULP
  · -- CASE B: zm > maxRep.
    push_neg at h_zm_le_max
    have h_zm_gt_maxRep : maxRep.toNat < zm.toNat := h_zm_le_max
    have h_m_pos : 1 ≤ zm.toNat := by omega
    have hsub : (zm - 1).toNat = zm.toNat - 1 := m_sub_one_no_underflow h_m_pos
    have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_ ≠ 0 := by
      rw [← h_mant_si]; exact hres_pos_mant_ne
    -- Structure: res_pos.exponent_ = ze', res_pos.mantissa ∈ {zm, zm-1}, full-ULP bound,
    -- res_pos.mantissa.toNat ≥ maxRep.toNat.
    have h_struct : res_pos.exponent_ = ze'
        ∧ (res_pos.mantissa_.toNat = zm.toNat ∨ res_pos.mantissa_.toNat = zm.toNat - 1)
        ∧ (maxRep.toNat ≤ res_pos.mantissa_.toNat)
        ∧ |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
      have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_.toNat := by
        rw [h_mant_si]
      rw [hmant_eq_si, h_exp_si]
      by_cases h_rd : g.shouldRoundUp_downward
      · have h_rd_bool : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
          roundUp_bool_dn_true g zm h_rd
        have h_m1_ge : largeRange.min.toNat ≤ (zm - 1).toNat := by rw [hsub]; omega
        have h_m1_not_lt : ¬ (zm - 1) < largeRange.min := by
          rw [UInt64.lt_iff_toNat_lt]; omega
        have hres : g.doRoundDown false zm ze' largeRange.min .downward =
            { negative_ := false, mantissa_ := zm - 1, exponent_ := ze' } := by
          unfold Guard.doRoundDown
          simp only [h_rd_bool, if_true, if_neg h_m1_not_lt]
          exact bringIntoRange_value_inRange false (zm - 1) ze' largeRange.min h_m1_not_lt (by omega)
        rw [hres]
        refine ⟨rfl, Or.inr hsub, by rw [hsub]; omega, ?_⟩
        rw [hsub, Nat.cast_sub h_m_pos, Nat.cast_one, abs_le]
        constructor <;> linarith only [hf_nn, hf_lt]
      · have h_rd_false : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = false :=
          roundUp_bool_dn_false g zm h_rd
        have h_m_not_lt : ¬ zm < largeRange.min := by rw [UInt64.lt_iff_toNat_lt]; omega
        have hres : g.doRoundDown false zm ze' largeRange.min .downward =
            { negative_ := false, mantissa_ := zm, exponent_ := ze' } := by
          unfold Guard.doRoundDown
          simp only [h_rd_false, Bool.false_eq_true, if_false]
          exact bringIntoRange_value_inRange false zm ze' largeRange.min h_m_not_lt (by omega)
        rw [hres]
        refine ⟨rfl, Or.inl rfl, le_of_lt h_zm_gt_maxRep, ?_⟩
        rw [abs_le]; constructor <;> linarith only [hf_nn, hf_lt]
    obtain ⟨h_rp_exp, h_rp_cases, h_rp_ge_maxRep, h_full_ulp⟩ := h_struct
    have h10ze_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze_pos
    have h_rp_min : largeRange.min.toNat ≤ res_pos.mantissa_.toNat := by omega
    by_cases h_rp_le_max : res_pos.mantissa_.toNat ≤ maxRep.toNat
    · -- SUB-CASE B1: res_pos.mantissa = maxRep+? (≤ maxRep), normalize identity.
      have h_rp_max : res_pos.toNumber.mantissa_.toNat ≤ largeRange.max.toNat := by
        change res_pos.mantissa_.toNat ≤ largeRange.max.toNat; omega
      have h_rp_exp' : minExponent ≤ res_pos.toNumber.exponent_ := by
        change minExponent ≤ res_pos.exponent_; rw [h_rp_exp]; exact h_ze_ge
      have h_rp_mod : res_pos.toNumber.mantissa_.toNat > maxRep.toNat →
          res_pos.toNumber.mantissa_.toNat % 10 = 0 := by
        change res_pos.mantissa_.toNat > maxRep.toNat → _; intro hgt; omega
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants_downward
          (show largeRange.min.toNat ≤ res_pos.toNumber.mantissa_.toNat from h_rp_min)
          h_rp_max h_rp_exp' h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ ze' := by
        rw [h_res_eq]; change (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = _; rw [h_rp_exp]
      rw [h_result_val, hT_def]
      have h_factor : (res_pos.mantissa_.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'
          = ((res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)) * 10 ^ ze' := by ring
      have h_target_factor : ((zm.toNat : ℚ) - f) * 10 ^ ze' * (11 / (2 ^ 63 - 18 : ℚ))
          = (((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ))) * 10 ^ ze' := by ring
      rw [h_factor, h_target_factor, abs_mul, abs_of_nonneg h10ze_nn]
      apply mul_lt_mul_of_pos_right _ h10ze_pos
      calc |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)|
          ≤ 1 := h_full_ulp
        _ < ((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ)) := h_oneULP
    · -- SUB-CASE B2: res_pos.mantissa > maxRep, capAtMaxRep + doRoundUp fires.
      push_neg at h_rp_le_max
      set m : UInt64 := res_pos.mantissa_ with hm_def
      have hm_gt : maxRep < m := UInt64.lt_iff_toNat_lt.mpr h_rp_le_max
      have hm_le_maxMant : m.toNat ≤ largeRange.max.toNat := by
        rw [hmaxMant_v]; rcases h_rp_cases with h | h <;> rw [hm_def, h] <;> omega
      have hm_div_toNat : (m / 10).toNat = m.toNat / 10 := by
        rw [UInt64.toNat_div]; rfl
      have hm_div_ge : (mantissaFloor : ℕ) ≤ (m / 10).toNat := by
        rw [hm_div_toNat]; omega
      have hm_div_le : (m / 10).toNat ≤ maxRep.toNat := by
        rw [hm_div_toNat, hmaxRep_v, hmaxMant_v] at *; omega
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
        have h_norm2 : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
            res_pos.toNumber.exponent_ largeRange.min largeRange.max .downward = .ok result := h_norm
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
      have h_reduce := doNormalize_capAtMaxRep_reduce res_pos.toNumber.negative_ m res_pos.exponent_ .downward
        hm_gt hm_le_maxMant (h_rp_exp ▸ h_ze_ge) (h_rp_exp ▸ h_ze_lt_max)
      have h_norm2 : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
          res_pos.toNumber.exponent_ largeRange.min largeRange.max .downward = .ok result := h_norm
      rw [show res_pos.toNumber.mantissa_ = m from rfl,
          show res_pos.toNumber.exponent_ = res_pos.exponent_ from rfl, h_reduce] at h_norm2
      set G : Guard := (if res_pos.toNumber.negative_ then Guard.new.set_negative else Guard.new).push (m % 10) with hG_def
      cases hru : G.doRoundUp res_pos.toNumber.negative_ (m / 10) (res_pos.exponent_ + 1)
          largeRange.min largeRange.max .downward "Number::normalize 2" with
      | error err => rw [hru] at h_norm2; simp only [Except.map] at h_norm2; exact absurd h_norm2 (by intro hc; cases hc)
      | ok res2 =>
        rw [hru] at h_norm2
        simp only [Except.map] at h_norm2
        have h_result_eq : result = res2.toNumber := (Except.ok.inj h_norm2).symm
        have hres2_mant_ne : res2.mantissa_ ≠ 0 := by
          rw [h_result_eq] at hresult; exact hresult
        -- the FALSE-sign doRoundUp result.
        have h_false := doRoundUp_false_from_ok G res_pos.toNumber.negative_ (m / 10)
          (res_pos.exponent_ + 1) .downward "Number::normalize 2" res2 hru
        have hres2f_mant_ne : ({ negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ } : RoundResult).mantissa_ ≠ 0 := hres2_mant_ne
        -- doRoundUp value: bound by truncate/roundUp.  In all cases
        -- |res2_value - (m/10)*10^(ze'+1)| ≤ 10*10^ze' and the value is one of
        -- (m/10)*10^(ze'+1) or (m/10+1)*10^(ze'+1).  We bound directly.
        -- res2_value:
        set W : ℚ := (res2.mantissa_.toNat : ℚ) * 10 ^ res2.exponent_ with hW_def
        -- Bound |W - m.toNat * 10^ze'| ≤ 10 * 10^ze' using the value lemmas.
        have hVeq : (((m / 10).toNat : ℚ)) * 10 ^ (res_pos.exponent_ + 1)
            = ((m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_ := by
          rw [hm_div_toNat]
          have heucl : 10 * (m.toNat / 10) + m.toNat % 10 = m.toNat := Nat.div_add_mod m.toNat 10
          rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_one]
          have : ((m.toNat / 10 : ℕ) : ℚ) * 10 = (m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ) := by
            have hc : ((10 * (m.toNat / 10) + m.toNat % 10 : ℕ) : ℚ) = (m.toNat : ℚ) := by rw [heucl]
            push_cast at hc ⊢; linarith
          rw [show ((m.toNat / 10 : ℕ) : ℚ) * (10 ^ res_pos.exponent_ * 10)
                = (((m.toNat / 10 : ℕ) : ℚ) * 10) * 10 ^ res_pos.exponent_ from by ring, this]
        have hmod_lt : (m.toNat % 10 : ℕ) < 10 := Nat.mod_lt _ (by norm_num)
        have hmod_q_lt : ((m.toNat % 10 : ℕ) : ℚ) < 10 := by exact_mod_cast hmod_lt
        have hmod_q_nn : (0 : ℚ) ≤ ((m.toNat % 10 : ℕ) : ℚ) := by positivity
        -- doRoundUp value characterization (truncate or roundUp-noCusp).
        have h_no_cusp : (m / 10).toNat + 1 ≤ maxRep.toNat := by
          rw [hm_div_toNat, hmaxRep_v]; rw [hmaxMant_v] at hm_le_maxMant; omega
        have hWval : W = ((m / 10).toNat : ℚ) * 10 ^ (res_pos.exponent_ + 1)
            ∨ W = (((m / 10).toNat : ℚ) + 1) * 10 ^ (res_pos.exponent_ + 1) := by
          by_cases h_ru : G.shouldRoundUp_downward
          · right
            have := doRoundUp_value_downward_roundUp_noCusp G false (m / 10) (res_pos.exponent_ + 1)
              h_ru h_no_cusp "Number::normalize 2"
              { negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ }
              h_false hres2f_mant_ne
            simp only at this; rw [hW_def]; exact this
          · left
            have := doRoundUp_value_downward_truncate G false (m / 10) (res_pos.exponent_ + 1)
              h_ru "Number::normalize 2"
              { negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ }
              h_false hres2f_mant_ne
            simp only at this; rw [hW_def]; exact this
        -- |W - m.toNat * 10^ze'| ≤ 10*10^ze'.
        have h10rp_nn : (0 : ℚ) ≤ 10 ^ res_pos.exponent_ := le_of_lt (zpow_pos (by norm_num) _)
        have hWbound : |W - (m.toNat : ℚ) * 10 ^ res_pos.exponent_| ≤ 10 * 10 ^ res_pos.exponent_ := by
          rcases hWval with h | h
          · rw [h, hVeq]
            rw [show ((m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_ - (m.toNat : ℚ) * 10 ^ res_pos.exponent_
                  = (- ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_ from by ring,
                abs_mul, abs_of_nonneg h10rp_nn]
            apply mul_le_mul_of_nonneg_right _ h10rp_nn
            rw [abs_neg, abs_of_nonneg hmod_q_nn]; linarith
          · rw [h]
            have h1 : (((m / 10).toNat : ℚ) + 1) * 10 ^ (res_pos.exponent_ + 1)
                = ((m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ) + 10) * 10 ^ res_pos.exponent_ := by
              have := hVeq
              rw [show (((m / 10).toNat : ℚ) + 1) * 10 ^ (res_pos.exponent_ + 1)
                    = ((m / 10).toNat : ℚ) * 10 ^ (res_pos.exponent_ + 1) + 10 ^ (res_pos.exponent_ + 1) from by ring]
              rw [hVeq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_one]; ring
            rw [h1]
            rw [show ((m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ) + 10) * 10 ^ res_pos.exponent_ - (m.toNat : ℚ) * 10 ^ res_pos.exponent_
                  = (10 - ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_ from by ring,
                abs_mul, abs_of_nonneg h10rp_nn]
            apply mul_le_mul_of_nonneg_right _ h10rp_nn
            rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 10 - ((m.toNat % 10 : ℕ) : ℚ))]; linarith
        rw [h_rp_exp] at hWbound hVeq
        -- result value = W.
        have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = W := by
          rw [h_result_eq, hW_def]; rfl
        rw [h_result_val, hT_def]
        -- m.toNat = res_pos.mantissa_.toNat.
        have hm_eq_rp : (m.toNat : ℚ) = (res_pos.mantissa_.toNat : ℚ) := by rw [hm_def]
        have h_half : |(m.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
          rw [hm_eq_rp]; exact h_full_ulp
        have hm_le_zm_nat : m.toNat ≤ zm.toNat := by
          have : m.toNat = res_pos.mantissa_.toNat := by rw [hm_def]
          rw [this]; rcases h_rp_cases with h | h <;> omega
        have hm_le_zm : (m.toNat : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hm_le_zm_nat
        have hm_ge_maxRep_nat : maxRep.toNat < m.toNat := by rw [hm_def]; exact h_rp_le_max
        have hm_pos_q : (maxRepNat : ℚ) < (m.toNat : ℚ) := by
          rw [hmaxRep_v] at hm_ge_maxRep_nat; exact_mod_cast hm_ge_maxRep_nat
        have hzm_q_gt_maxRep : (maxRepNat : ℚ) < (zm.toNat : ℚ) := by
          have : (maxRepNat : ℕ) < zm.toNat := by rw [hmaxRep_v] at h_zm_gt_maxRep; exact h_zm_gt_maxRep
          exact_mod_cast this
        -- triangle split.
        have hsplit : |W - ((zm.toNat : ℚ) - f) * 10 ^ ze'|
            ≤ |W - (m.toNat : ℚ) * 10 ^ ze'|
              + |(m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'| :=
          abs_sub_le _ _ _
        have hVT : |(m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'| ≤ 1 * 10 ^ ze' := by
          rw [show (m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'
                = ((m.toNat : ℚ) - ((zm.toNat : ℚ) - f)) * 10 ^ ze' from by ring,
              abs_mul, abs_of_nonneg h10ze_nn]
          exact mul_le_mul_of_nonneg_right h_half h10ze_nn
        calc |W - ((zm.toNat : ℚ) - f) * 10 ^ ze'|
            ≤ |W - (m.toNat : ℚ) * 10 ^ ze'|
                + |(m.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) - f) * 10 ^ ze'| := hsplit
          _ ≤ 10 * 10 ^ ze' + 1 * 10 ^ ze' := by
              apply add_le_add hWbound hVT
          _ < ((zm.toNat : ℚ) - f) * 10 ^ ze' * (11 / (2 ^ 63 - 18 : ℚ)) := by
              rw [show (10 : ℚ) * 10 ^ ze' + 1 * 10 ^ ze' = 11 * 10 ^ ze' from by ring]
              rw [show ((zm.toNat : ℚ) - f) * 10 ^ ze' * (11 / (2 ^ 63 - 18 : ℚ))
                    = (((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ))) * 10 ^ ze' from by ring]
              apply mul_lt_mul_of_pos_right _ h10ze_pos
              rw [show ((zm.toNat : ℚ) - f) * (11 / (2 ^ 63 - 18 : ℚ))
                    = (11 * ((zm.toNat : ℚ) - f)) / (2 ^ 63 - 18 : ℚ) from by ring]
              rw [lt_div_iff₀ h_denom_pos]
              nlinarith [hzm_q_gt_maxRep, hf_nn, hf_lt]

set_option maxHeartbeats 1600000 in
-- reuses the full diff-sign tight case analysis with extra sign tracking; elaboration-heavy
/-- **Rounding direction** for `Number.operator_add` under `.downward`, diff-sign
branch, restricted to non-negative truth. Under `0 ≤ x.toRat + y.toRat` the
positive result is rounded **down** toward (never past) the truth:
`result.toRat ≤ x.toRat + y.toRat`.

The key new ingredient over the magnitude bound is that when `.downward` does not
round the mantissa down (`res_pos.mantissa = zm`), the diff-sign sticky-bit fact
forces `f = 0` (`diff_sign_f_zero_of_no_roundDown_downward`), so the result lands
exactly on the truth rather than overshooting it; when it does round down, the
mantissa `zm - 1 ≤ zm - f` is below the truth. -/
theorem operator_add_rounding_bound_diff_sign_downward_dir (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_truth_nn : 0 ≤ x.toRat + y.toRat) :
    result.toRat ≤ x.toRat + y.toRat := by
  obtain ⟨zm, ze', f, zn, g, res_pos, hf_rep, hzm_gt, hzm_lt, habs_xy_eq, h_rdown,
          h_norm, hsign, hsbit_raw⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .downward hx hy hx_mant_ne hy_mant_ne
      h_diff_sign h_not_zero hok hresult
  have hf_nn : 0 ≤ f := represents_nonneg hf_rep
  have hf_lt : f < 1 := represents_lt_one hf_rep
  have h10ze_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze_pos
  have hzm_ge : (mantissaFloor : ℕ) ≤ zm.toNat := le_of_lt hzm_gt
  have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by
    have : (mantissaFloorSucc : ℕ) ≤ zm.toNat := hzm_gt
    exact_mod_cast this
  -- truth ≥ 0 forces zn = false (positive result).
  have h_truth_pos : 0 < ((zm.toNat : ℚ) - f) * 10 ^ ze' := by
    apply mul_pos _ h10ze_pos; linarith
  have hzn_false : zn = false := by
    cases hzn : zn with
    | false => rfl
    | true =>
      have htruth_np : x.toRat + y.toRat ≤ 0 := hsign.1 hzn
      have hpos : 0 < |x.toRat + y.toRat| := habs_xy_eq ▸ h_truth_pos
      rw [abs_pos] at hpos
      exact absurd (le_antisymm htruth_np h_truth_nn) hpos
  have hsbit_fact : g.sbit_ = true ∨ (g.digits_ = 0 ∧ g.xbit_ = false) := hsbit_raw hzn_false
  -- result is positive; reduce to value ≤ truth.
  have h_norm' : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
      res_pos.toNumber.exponent_ largeRange.min largeRange.max .downward = .ok result := h_norm
  have hres_pos_mant_ne : res_pos.toNumber.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result h_norm hresult
  have h_res_pos_neg : res_pos.toNumber.negative_ = zn := by
    rw [← h_rdown] at hres_pos_mant_ne ⊢
    unfold RoundResult.toNumber Guard.doRoundDown Guard.bringIntoRange at hres_pos_mant_ne ⊢
    simp only [] at hres_pos_mant_ne ⊢
    split_ifs at hres_pos_mant_ne ⊢ <;> first | rfl | (exact absurd rfl hres_pos_mant_ne)
  have h_result_neg : result.negative_ = false := by
    rw [doNormalize_preserves_negative h_norm' hresult, h_res_pos_neg, hzn_false]
  have h_result_nn : 0 ≤ result.toRat := Number.toRat_nonneg_of_nonnegative result h_result_neg
  rw [show result.toRat = |result.toRat| from (abs_of_nonneg h_result_nn).symm,
      show x.toRat + y.toRat = |x.toRat + y.toRat| from (abs_of_nonneg h_truth_nn).symm,
      habs_xy_eq, abs_toRat_eq result]
  rw [hzn_false] at h_rdown
  have h_rd_mant_ne : (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_ ≠ 0 := by
    rw [h_rdown]; exact hres_pos_mant_ne
  have h_ze_ge : minExponent ≤ ze' :=
    doRoundDown_input_exp_ge_minExp_of_mant_ne g false zm ze' largeRange.min .downward h_rd_mant_ne
  have h_mant_si : res_pos.mantissa_ = (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_ := by
    rw [← h_rdown]
  have h_exp_si : res_pos.exponent_ = (g.doRoundDown false zm ze' largeRange.min .downward).exponent_ := by
    rw [← h_rdown]
  set T : ℚ := ((zm.toNat : ℚ) - f) * 10 ^ ze' with hT_def
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_toNat_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := by decide
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := by decide
  have h_m_pos : 1 ≤ zm.toNat := by omega
  have hsub : (zm - 1).toNat = zm.toNat - 1 := m_sub_one_no_underflow h_m_pos
  -- f = 0 when no round-down fires.
  have hf_zero_of_no_rd : ¬ g.shouldRoundUp_downward → f = 0 := fun h =>
    diff_sign_f_zero_of_no_roundDown_downward g f hf_rep hsbit_fact h
  -- Main case split (mirrors the tight proof structure).
  by_cases h_zm_le_max : zm.toNat ≤ maxRep.toNat
  · by_cases h_ze_gt : minExponent < ze'
    · -- CASE A: normalize is identity.
      have h_inv := doRoundDown_output_in_range_of_floor g false zm ze' .downward hzm_ge h_zm_le_max h_ze_gt
      simp only at h_inv
      obtain ⟨h_rp_min, h_rp_max, h_rp_exp, h_rp_mod⟩ := h_inv
      rw [← h_mant_si] at h_rp_min h_rp_max h_rp_mod
      rw [← h_exp_si] at h_rp_exp
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants_downward h_rp_min h_rp_max h_rp_exp h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
        rw [h_res_eq]; rfl
      rw [h_result_val, hT_def]
      -- result value ≤ (zm - f)·10^ze'.
      by_cases h_rd : g.shouldRoundUp_downward
      · have hval := doRoundDown_value_downward_roundDown g false zm ze' h_rd (by omega) h_ze_gt
        simp only at hval
        rw [← h_mant_si, ← h_exp_si] at hval
        rw [hval]
        apply mul_le_mul_of_nonneg_right _ h10ze_nn; linarith
      · have hval := doRoundDown_value_downward_truncate g false zm ze' h_rd h_ze_gt
        simp only at hval
        rw [← h_mant_si, ← h_exp_si] at hval
        rw [hval]
        have hf0 : f = 0 := hf_zero_of_no_rd h_rd
        apply mul_le_mul_of_nonneg_right _ h10ze_nn; rw [hf0]; linarith
    · -- BOUNDARY: ze' = minExponent.
      have h_ze_eq : ze' = minExponent := le_antisymm (not_lt.mp h_ze_gt) h_ze_ge
      have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_ ≠ 0 := h_rd_mant_ne
      have h_struct : (res_pos.mantissa_.toNat = zm.toNat - 1 ∧ g.shouldRoundUp_downward
            ∨ res_pos.mantissa_.toNat = zm.toNat ∧ ¬ g.shouldRoundUp_downward)
          ∧ largeRange.min.toNat ≤ res_pos.mantissa_.toNat
          ∧ res_pos.exponent_ = minExponent := by
        have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_.toNat := by
          rw [h_mant_si]
        rw [hmant_eq_si, h_exp_si]
        rw [h_ze_eq] at h_rdf_mant_ne ⊢
        by_cases h_rd : g.shouldRoundUp_downward
        · have h_rd_bool : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
            roundUp_bool_dn_true g zm h_rd
          by_cases h_m1_lt : (zm - 1) < largeRange.min
          · exfalso
            have h_le_min : zm.toNat ≤ largeRange.min.toNat := by
              rw [UInt64.lt_iff_toNat_lt, hsub] at h_m1_lt; rw [hminMant_v] at h_m1_lt ⊢; omega
            have h_mul10 : ((zm - 1) * 10).toNat = (zm.toNat - 1) * 10 :=
              m_sub_one_mul_ten_no_overflow h_m_pos h_le_min
            have h_not_resc2 : ¬ ((zm - 1) * 10) < largeRange.min := by
              rw [UInt64.lt_iff_toNat_lt, h_mul10, hminMant_v]; omega
            have : (g.doRoundDown false zm minExponent largeRange.min .downward).mantissa_ = 0 := by
              unfold Guard.doRoundDown Guard.bringIntoRange
              simp only [h_rd_bool, if_true, if_pos h_m1_lt, if_neg h_not_resc2]
              rw [if_pos (show (minExponent - 1 : Int) < minExponent by omega)]
            exact h_rdf_mant_ne this
          · have hres : g.doRoundDown false zm minExponent largeRange.min .downward =
                { negative_ := false, mantissa_ := zm - 1, exponent_ := minExponent } := by
              unfold Guard.doRoundDown
              simp only [h_rd_bool, if_true, if_neg h_m1_lt]
              exact bringIntoRange_value_inRange false (zm - 1) minExponent largeRange.min h_m1_lt (by omega)
            rw [hres]
            have h_m1_ge : largeRange.min.toNat ≤ (zm - 1).toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m1_lt; omega
            exact ⟨Or.inl ⟨hsub, h_rd⟩, h_m1_ge, rfl⟩
        · have h_rd_false : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = false :=
            roundUp_bool_dn_false g zm h_rd
          by_cases h_m_lt : zm < largeRange.min
          · exfalso
            have h_le_min : zm.toNat ≤ largeRange.min.toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
            have h_m10 : (zm * 10).toNat = zm.toNat * 10 := mul_ten_no_overflow_of_lt_lr_min h_m_lt
            have h_not_resc2 : ¬ (zm * 10) < largeRange.min := by
              rw [UInt64.lt_iff_toNat_lt, h_m10, hminMant_v]; omega
            have : (g.doRoundDown false zm minExponent largeRange.min .downward).mantissa_ = 0 := by
              unfold Guard.doRoundDown Guard.bringIntoRange
              simp only [h_rd_false, Bool.false_eq_true, if_false, if_pos h_m_lt]
              rw [if_pos (show (minExponent - 1 : Int) < minExponent by omega)]
            exact h_rdf_mant_ne this
          · have hres : g.doRoundDown false zm minExponent largeRange.min .downward =
                { negative_ := false, mantissa_ := zm, exponent_ := minExponent } := by
              unfold Guard.doRoundDown
              simp only [h_rd_false, Bool.false_eq_true, if_false]
              exact bringIntoRange_value_inRange false zm minExponent largeRange.min h_m_lt (by omega)
            rw [hres]
            have h_m_ge : largeRange.min.toNat ≤ zm.toNat := by
              rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
            exact ⟨Or.inr ⟨rfl, h_rd⟩, h_m_ge, rfl⟩
      obtain ⟨h_mant_cases, h_rp_min, h_rp_exp⟩ := h_struct
      have h_rp_max : res_pos.toNumber.mantissa_.toNat ≤ largeRange.max.toNat := by
        change res_pos.mantissa_.toNat ≤ largeRange.max.toNat
        rcases h_mant_cases with ⟨h, _⟩ | ⟨h, _⟩ <;> rw [h] <;> rw [hmaxMant_v] <;> omega
      have h_rp_exp' : minExponent ≤ res_pos.toNumber.exponent_ := by
        change minExponent ≤ res_pos.exponent_; rw [h_rp_exp]
      have h_rp_mod : res_pos.toNumber.mantissa_.toNat > maxRep.toNat →
          res_pos.toNumber.mantissa_.toNat % 10 = 0 := by
        change res_pos.mantissa_.toNat > maxRep.toNat → _
        rcases h_mant_cases with ⟨h, _⟩ | ⟨h, _⟩ <;> rw [h] <;> intro hgt <;> rw [hmaxRep_v] at hgt <;> omega
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants_downward
          (show largeRange.min.toNat ≤ res_pos.toNumber.mantissa_.toNat from h_rp_min)
          h_rp_max h_rp_exp' h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ minExponent := by
        rw [h_res_eq]; change (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = _; rw [h_rp_exp]
      rw [h_result_val, hT_def, h_ze_eq]
      have h10min_nn : (0 : ℚ) ≤ 10 ^ (minExponent : Int) := le_of_lt (zpow_pos (by norm_num) _)
      have h_mant_le : (res_pos.mantissa_.toNat : ℚ) ≤ (zm.toNat : ℚ) - f := by
        rcases h_mant_cases with ⟨h, _⟩ | ⟨h, hrd⟩
        · rw [h, Nat.cast_sub h_m_pos, Nat.cast_one]; linarith
        · rw [h, hf_zero_of_no_rd hrd]; linarith
      apply mul_le_mul_of_nonneg_right h_mant_le h10min_nn
  · -- CASE B: zm > maxRep.
    push_neg at h_zm_le_max
    have h_zm_gt_maxRep : maxRep.toNat < zm.toNat := h_zm_le_max
    have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_ ≠ 0 := h_rd_mant_ne
    have h_struct : res_pos.exponent_ = ze'
        ∧ (res_pos.mantissa_.toNat = zm.toNat - 1 ∧ g.shouldRoundUp_downward
           ∨ res_pos.mantissa_.toNat = zm.toNat ∧ ¬ g.shouldRoundUp_downward)
        ∧ (maxRep.toNat ≤ res_pos.mantissa_.toNat) := by
      have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .downward).mantissa_.toNat := by
        rw [h_mant_si]
      rw [hmant_eq_si, h_exp_si]
      by_cases h_rd : g.shouldRoundUp_downward
      · have h_rd_bool : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
          roundUp_bool_dn_true g zm h_rd
        have h_m1_not_lt : ¬ (zm - 1) < largeRange.min := by
          rw [UInt64.lt_iff_toNat_lt, hsub]; omega
        have hres : g.doRoundDown false zm ze' largeRange.min .downward =
            { negative_ := false, mantissa_ := zm - 1, exponent_ := ze' } := by
          unfold Guard.doRoundDown
          simp only [h_rd_bool, if_true, if_neg h_m1_not_lt]
          exact bringIntoRange_value_inRange false (zm - 1) ze' largeRange.min h_m1_not_lt (by omega)
        rw [hres]
        exact ⟨rfl, Or.inl ⟨hsub, h_rd⟩, by rw [hsub]; omega⟩
      · have h_rd_false : ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = false :=
          roundUp_bool_dn_false g zm h_rd
        have h_m_not_lt : ¬ zm < largeRange.min := by rw [UInt64.lt_iff_toNat_lt]; omega
        have hres : g.doRoundDown false zm ze' largeRange.min .downward =
            { negative_ := false, mantissa_ := zm, exponent_ := ze' } := by
          unfold Guard.doRoundDown
          simp only [h_rd_false, Bool.false_eq_true, if_false]
          exact bringIntoRange_value_inRange false zm ze' largeRange.min h_m_not_lt (by omega)
        rw [hres]
        exact ⟨rfl, Or.inr ⟨rfl, h_rd⟩, le_of_lt h_zm_gt_maxRep⟩
    obtain ⟨h_rp_exp, h_rp_cases, h_rp_ge_maxRep⟩ := h_struct
    have h_rp_min : largeRange.min.toNat ≤ res_pos.mantissa_.toNat := by
      rw [hmaxRep_v] at h_rp_ge_maxRep; rw [hminMant_v]; omega
    by_cases h_rp_le_max : res_pos.mantissa_.toNat ≤ maxRep.toNat
    · -- SUB-CASE B1: normalize identity.
      have h_rp_max : res_pos.toNumber.mantissa_.toNat ≤ largeRange.max.toNat := by
        change res_pos.mantissa_.toNat ≤ largeRange.max.toNat; omega
      have h_rp_exp' : minExponent ≤ res_pos.toNumber.exponent_ := by
        change minExponent ≤ res_pos.exponent_; rw [h_rp_exp]; exact h_ze_ge
      have h_rp_mod : res_pos.toNumber.mantissa_.toNat > maxRep.toNat →
          res_pos.toNumber.mantissa_.toNat % 10 = 0 := by
        change res_pos.mantissa_.toNat > maxRep.toNat → _; intro hgt; omega
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants_downward
          (show largeRange.min.toNat ≤ res_pos.toNumber.mantissa_.toNat from h_rp_min)
          h_rp_max h_rp_exp' h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ ze' := by
        rw [h_res_eq]; change (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = _; rw [h_rp_exp]
      rw [h_result_val, hT_def]
      have h_mant_le : (res_pos.mantissa_.toNat : ℚ) ≤ (zm.toNat : ℚ) - f := by
        rcases h_rp_cases with ⟨h, _⟩ | ⟨h, hrd⟩
        · rw [h, Nat.cast_sub h_m_pos, Nat.cast_one]; linarith
        · rw [h, hf_zero_of_no_rd hrd]; linarith
      apply mul_le_mul_of_nonneg_right h_mant_le h10ze_nn
    · -- SUB-CASE B2: capAtMaxRep + doRoundUp fires.
      push_neg at h_rp_le_max
      -- In this sub-case the no-round-down branch is impossible: it would give
      -- res_pos.mantissa = zm > maxRep with f = 0, and the doRoundUp on (m/10)
      -- could only round down further, never above truth.  We bound directly.
      set m : UInt64 := res_pos.mantissa_ with hm_def
      have hm_gt : maxRep < m := UInt64.lt_iff_toNat_lt.mpr h_rp_le_max
      have hm_le_maxMant : m.toNat ≤ largeRange.max.toNat := by
        rw [hmaxMant_v]; rcases h_rp_cases with ⟨h, _⟩ | ⟨h, _⟩ <;> rw [hm_def, h] <;> omega
      have hm_div_toNat : (m / 10).toNat = m.toNat / 10 := by rw [UInt64.toNat_div]; rfl
      have hm_le_zm_nat : m.toNat ≤ zm.toNat := by
        have : m.toNat = res_pos.mantissa_.toNat := by rw [hm_def]
        rw [this]; rcases h_rp_cases with ⟨h, _⟩ | ⟨h, _⟩ <;> omega
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
        have h_norm2 : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
            res_pos.toNumber.exponent_ largeRange.min largeRange.max .downward = .ok result := h_norm
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
      have h_reduce := doNormalize_capAtMaxRep_reduce res_pos.toNumber.negative_ m res_pos.exponent_ .downward
        hm_gt hm_le_maxMant (h_rp_exp ▸ h_ze_ge) (h_rp_exp ▸ h_ze_lt_max)
      have h_norm2 : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
          res_pos.toNumber.exponent_ largeRange.min largeRange.max .downward = .ok result := h_norm
      rw [show res_pos.toNumber.mantissa_ = m from rfl,
          show res_pos.toNumber.exponent_ = res_pos.exponent_ from rfl, h_reduce] at h_norm2
      set G : Guard := (if res_pos.toNumber.negative_ then Guard.new.set_negative else Guard.new).push (m % 10) with hG_def
      cases hru : G.doRoundUp res_pos.toNumber.negative_ (m / 10) (res_pos.exponent_ + 1)
          largeRange.min largeRange.max .downward "Number::normalize 2" with
      | error err => rw [hru] at h_norm2; simp only [Except.map] at h_norm2; exact absurd h_norm2 (by intro hc; cases hc)
      | ok res2 =>
        rw [hru] at h_norm2
        simp only [Except.map] at h_norm2
        have h_result_eq : result = res2.toNumber := (Except.ok.inj h_norm2).symm
        have hres2_mant_ne : res2.mantissa_ ≠ 0 := by rw [h_result_eq] at hresult; exact hresult
        have h_false := doRoundUp_false_from_ok G res_pos.toNumber.negative_ (m / 10)
          (res_pos.exponent_ + 1) .downward "Number::normalize 2" res2 hru
        have hres2f_mant_ne : ({ negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ } : RoundResult).mantissa_ ≠ 0 := hres2_mant_ne
        set W : ℚ := (res2.mantissa_.toNat : ℚ) * 10 ^ res2.exponent_ with hW_def
        have h_no_cusp : (m / 10).toNat + 1 ≤ maxRep.toNat := by
          rw [hm_div_toNat, hmaxRep_v]; rw [hmaxMant_v] at hm_le_maxMant; omega
        have hVeq : (((m / 10).toNat : ℚ)) * 10 ^ (res_pos.exponent_ + 1)
            = ((m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_ := by
          rw [hm_div_toNat]
          have heucl : 10 * (m.toNat / 10) + m.toNat % 10 = m.toNat := Nat.div_add_mod m.toNat 10
          rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_one]
          have : ((m.toNat / 10 : ℕ) : ℚ) * 10 = (m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ) := by
            have hc : ((10 * (m.toNat / 10) + m.toNat % 10 : ℕ) : ℚ) = (m.toNat : ℚ) := by rw [heucl]
            push_cast at hc ⊢; linarith
          rw [show ((m.toNat / 10 : ℕ) : ℚ) * (10 ^ res_pos.exponent_ * 10)
                = (((m.toNat / 10 : ℕ) : ℚ) * 10) * 10 ^ res_pos.exponent_ from by ring, this]
        have hmod_lt : (m.toNat % 10 : ℕ) < 10 := Nat.mod_lt _ (by norm_num)
        have hmod_q_lt : ((m.toNat % 10 : ℕ) : ℚ) < 10 := by exact_mod_cast hmod_lt
        have hmod_q_nn : (0 : ℚ) ≤ ((m.toNat % 10 : ℕ) : ℚ) := by positivity
        have h10rp_nn : (0 : ℚ) ≤ 10 ^ res_pos.exponent_ := le_of_lt (zpow_pos (by norm_num) _)
        -- doRoundUp value: truncate or roundUp-noCusp; in both cases W ≤ m.toNat * 10^ze'.
        have hW_le_m : W ≤ (m.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
          by_cases h_ru : G.shouldRoundUp_downward
          · -- roundUp: W = (m/10 + 1)·10^(ze'+1).  This requires the digit m%10 to be
            -- nonzero (round-up needs guard content); then W = m - m%10 + 10 ≤ ... but
            -- since m%10 ≥ 1 we still have W ≤ m·10^ze' iff 10 ≤ m%10 — false.  However
            -- in this diff-sign downward setting roundUp on the carried guard cannot
            -- exceed the floor: we use the value lemma and the no-overshoot fact.
            have hWval := doRoundUp_value_downward_roundUp_noCusp G false (m / 10) (res_pos.exponent_ + 1)
              h_ru h_no_cusp "Number::normalize 2"
              { negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ }
              h_false hres2f_mant_ne
            simp only at hWval
            -- This branch is unreachable: the `capAtMaxRep` guard `G` is built from
            -- the (positive) result sign, so its base is `Guard.new` with `sbit_ = false`,
            -- and `push` preserves `sbit_`.  Hence `¬ G.shouldRoundUp_downward`.
            exfalso
            have hrespos_neg_false : res_pos.toNumber.negative_ = false := by
              change res_pos.negative_ = false
              have : res_pos.negative_ = zn := h_res_pos_neg
              rw [this, hzn_false]
            have hG_sbit_false : G.sbit_ = false := by
              rw [hG_def, hrespos_neg_false]
              simp only [Bool.false_eq_true, if_false]
              rfl
            obtain ⟨hG_sbit_true, _⟩ := h_ru
            rw [hG_sbit_false] at hG_sbit_true
            exact Bool.noConfusion hG_sbit_true
          · have hWval := doRoundUp_value_downward_truncate G false (m / 10) (res_pos.exponent_ + 1)
              h_ru "Number::normalize 2"
              { negative_ := false, mantissa_ := res2.mantissa_, exponent_ := res2.exponent_ }
              h_false hres2f_mant_ne
            simp only at hWval
            rw [hW_def, hWval, hVeq]
            have : ((m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_
                ≤ (m.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
              apply mul_le_mul_of_nonneg_right _ h10rp_nn; linarith
            exact this
        have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = W := by
          rw [h_result_eq, hW_def]; rfl
        rw [h_result_val, hT_def]
        rw [h_rp_exp] at hW_le_m
        -- W ≤ m·10^ze' ≤ zm·10^ze'.  Need ≤ (zm - f)·10^ze'; use f ≤ ... no: need m ≤ zm - f.
        -- m ∈ {zm, zm-1}: if m = zm-1 then m ≤ zm - f (f ≤ 1); if m = zm then f = 0.
        have hm_le_T_mant : (m.toNat : ℚ) ≤ (zm.toNat : ℚ) - f := by
          rcases h_rp_cases with ⟨h, _⟩ | ⟨h, hrd⟩
          · have : m.toNat = zm.toNat - 1 := by rw [hm_def]; exact h
            rw [this, Nat.cast_sub h_m_pos, Nat.cast_one]; linarith
          · have : m.toNat = zm.toNat := by rw [hm_def]; exact h
            rw [this, hf_zero_of_no_rd hrd]; linarith
        calc W ≤ (m.toNat : ℚ) * 10 ^ ze' := hW_le_m
          _ ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze' :=
              mul_le_mul_of_nonneg_right hm_le_T_mant h10ze_nn

end XRPL.Model.Protocol

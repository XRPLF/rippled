import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.DiffSignTail
import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts.DiffSignRepresents
import XRPL.Properties.Protocol.Number.Common.Helpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Local (file-private) form: for `.towards_zero` the `roundUp` boolean inside
`doRoundDown`/`doRoundUp` is always `false` (it always truncates toward zero). -/
private lemma roundUp_bool_tz_false (g : Guard) (m : UInt64) :
    ((g.round .towards_zero == 1) || ((g.round .towards_zero == 0) && (m % 2 == 1))) = false := by
  rw [round_towards_zero_eq_neg_one]; rfl

set_option maxHeartbeats 1600000 in
-- Large case analysis (alignment cases) over the full doRoundDown + normalize
-- pipeline for `.towards_zero`.  Simpler than `.downward`: towards_zero always
-- truncates, so there are no round-up sub-cases.
theorem operator_add_rounding_bound_diff_sign_towards_zero_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  obtain ⟨zm, ze', f, zn, g, res_pos, hf_rep, hzm_gt, hzm_lt, habs_xy_eq, h_rdown,
          h_norm, hsign, _⟩ :=
    operator_add_algorithmic_facts_diff_sign_represents x y result .towards_zero hx hy hx_mant_ne hy_mant_ne
      h_diff_sign h_not_zero hok hresult
  have h_norm' : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
      res_pos.toNumber.exponent_ largeRange.min largeRange.max .towards_zero = .ok result := h_norm
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
  have h_rd_mant_ne : (g.doRoundDown zn zm ze' largeRange.min .towards_zero).mantissa_ ≠ 0 := by
    rw [h_rdown]; exact hres_pos_mant_ne
  have h_ze_ge : minExponent ≤ ze' :=
    doRoundDown_input_exp_ge_minExp_of_mant_ne g zn zm ze' largeRange.min .towards_zero h_rd_mant_ne
  -- Bridge res_pos to the sign-false doRoundDown.
  have h_mant_si : res_pos.mantissa_ = (g.doRoundDown false zm ze' largeRange.min .towards_zero).mantissa_ := by
    rw [← h_rdown]; exact doRoundDown_mantissa_sign_indep g zn zm ze' largeRange.min .towards_zero
  have h_exp_si : res_pos.exponent_ = (g.doRoundDown false zm ze' largeRange.min .towards_zero).exponent_ := by
    rw [← h_rdown]; exact doRoundDown_exponent_sign_indep g zn zm ze' largeRange.min .towards_zero
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
  -- The `roundUp` boolean is always false for towards_zero.
  have h_rd_false : ((g.round .towards_zero == 1) || ((g.round .towards_zero == 0) && (zm % 2 == 1))) = false :=
    roundUp_bool_tz_false g zm
  -- Main case split.
  by_cases h_zm_le_max : zm.toNat ≤ maxRep.toNat
  · by_cases h_ze_gt : minExponent < ze'
    · -- CASE A: supTight applies directly; normalize is identity.
      have h_inv := doRoundDown_output_in_range_of_floor g false zm ze' .towards_zero hzm_ge h_zm_le_max h_ze_gt
      simp only at h_inv
      obtain ⟨h_rp_min, h_rp_max, h_rp_exp, h_rp_mod⟩ := h_inv
      rw [← h_mant_si] at h_rp_min h_rp_max h_rp_mod
      rw [← h_exp_si] at h_rp_exp
      have h_res_eq : result = res_pos.toNumber := by
        apply Number.normalize_eq_of_invariants_towards_zero
          (n := res_pos.toNumber) (result := result) h_rp_min h_rp_max h_rp_exp h_rp_mod h_norm
      have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = V := by
        rw [h_res_eq]; rfl
      rw [h_result_val, hV_def, hT_def]
      have h_sup := doRoundDown_rounds_towards_zero_supTight g zm ze' f hf_rep hzm_ge h_zm_le_max h_ze_gt
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
      have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .towards_zero).mantissa_ ≠ 0 := by
        rw [← h_mant_si]; exact hres_pos_mant_ne
      -- Structure: res_pos = (zm, minExp), full-ULP bound (towards_zero never rounds up).
      have h_struct : res_pos.mantissa_.toNat = zm.toNat
          ∧ largeRange.min.toNat ≤ res_pos.mantissa_.toNat
          ∧ res_pos.exponent_ = minExponent
          ∧ |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
        have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .towards_zero).mantissa_.toNat := by
          rw [h_mant_si]
        rw [hmant_eq_si, h_exp_si]
        rw [h_ze_eq] at h_rdf_mant_ne ⊢
        by_cases h_m_lt : zm < largeRange.min
        · exfalso
          have : (g.doRoundDown false zm minExponent largeRange.min .towards_zero).mantissa_ = 0 := by
            unfold Guard.doRoundDown Guard.bringIntoRange
            simp only [h_rd_false, Bool.false_eq_true, if_false, if_pos h_m_lt]
            rw [if_pos (show (minExponent - 1 : Int) < minExponent by omega)]
          exact h_rdf_mant_ne this
        · have hres : g.doRoundDown false zm minExponent largeRange.min .towards_zero =
              { negative_ := false, mantissa_ := zm, exponent_ := minExponent } := by
            unfold Guard.doRoundDown
            simp only [h_rd_false, Bool.false_eq_true, if_false]
            exact bringIntoRange_value_inRange false zm minExponent largeRange.min h_m_lt (by omega)
          rw [hres]
          have h_m_ge : largeRange.min.toNat ≤ zm.toNat := by
            rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
          have h_full : |(zm.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
            rw [abs_le]; constructor <;> linarith only [hf_nn, hf_lt]
          exact ⟨rfl, h_m_ge, rfl, h_full⟩
      obtain ⟨h_mant_eq, h_rp_min, h_rp_exp, h_full_ulp⟩ := h_struct
      have h_rp_max : res_pos.toNumber.mantissa_.toNat ≤ largeRange.max.toNat := by
        change res_pos.mantissa_.toNat ≤ largeRange.max.toNat
        rw [h_mant_eq, hmaxMant_v]; omega
      have h_rp_exp' : minExponent ≤ res_pos.toNumber.exponent_ := by
        change minExponent ≤ res_pos.exponent_; rw [h_rp_exp]
      have h_rp_mod : res_pos.toNumber.mantissa_.toNat > maxRep.toNat →
          res_pos.toNumber.mantissa_.toNat % 10 = 0 := by
        change res_pos.mantissa_.toNat > maxRep.toNat → _
        rw [h_mant_eq]; intro hgt; rw [hmaxRep_v] at hgt; omega
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants_towards_zero
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
    have h_rdf_mant_ne : (g.doRoundDown false zm ze' largeRange.min .towards_zero).mantissa_ ≠ 0 := by
      rw [← h_mant_si]; exact hres_pos_mant_ne
    -- Structure: res_pos.exponent_ = ze', res_pos.mantissa = zm (towards_zero never
    -- rounds up), full-ULP bound, res_pos.mantissa.toNat ≥ maxRep.toNat.
    have h_struct : res_pos.exponent_ = ze'
        ∧ res_pos.mantissa_.toNat = zm.toNat
        ∧ (maxRep.toNat ≤ res_pos.mantissa_.toNat)
        ∧ |(res_pos.mantissa_.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
      have hmant_eq_si : res_pos.mantissa_.toNat = (g.doRoundDown false zm ze' largeRange.min .towards_zero).mantissa_.toNat := by
        rw [h_mant_si]
      rw [hmant_eq_si, h_exp_si]
      have h_m_not_lt : ¬ zm < largeRange.min := by rw [UInt64.lt_iff_toNat_lt]; omega
      have hres : g.doRoundDown false zm ze' largeRange.min .towards_zero =
          { negative_ := false, mantissa_ := zm, exponent_ := ze' } := by
        unfold Guard.doRoundDown
        simp only [h_rd_false, Bool.false_eq_true, if_false]
        exact bringIntoRange_value_inRange false zm ze' largeRange.min h_m_not_lt (by omega)
      rw [hres]
      refine ⟨rfl, rfl, le_of_lt h_zm_gt_maxRep, ?_⟩
      rw [abs_le]; constructor <;> linarith only [hf_nn, hf_lt]
    obtain ⟨h_rp_exp, h_rp_eq, h_rp_ge_maxRep, h_full_ulp⟩ := h_struct
    have h10ze_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze_pos
    have h_rp_min : largeRange.min.toNat ≤ res_pos.mantissa_.toNat := by omega
    by_cases h_rp_le_max : res_pos.mantissa_.toNat ≤ maxRep.toNat
    · -- SUB-CASE B1: res_pos.mantissa ≤ maxRep, normalize identity.
      have h_rp_max : res_pos.toNumber.mantissa_.toNat ≤ largeRange.max.toNat := by
        change res_pos.mantissa_.toNat ≤ largeRange.max.toNat; omega
      have h_rp_exp' : minExponent ≤ res_pos.toNumber.exponent_ := by
        change minExponent ≤ res_pos.exponent_; rw [h_rp_exp]; exact h_ze_ge
      have h_rp_mod : res_pos.toNumber.mantissa_.toNat > maxRep.toNat →
          res_pos.toNumber.mantissa_.toNat % 10 = 0 := by
        change res_pos.mantissa_.toNat > maxRep.toNat → _; intro hgt; omega
      have h_res_eq : result = res_pos.toNumber :=
        Number.normalize_eq_of_invariants_towards_zero
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
        rw [hmaxMant_v, hm_def, h_rp_eq]; omega
      have hm_div_toNat : (m / 10).toNat = m.toNat / 10 := by
        rw [UInt64.toNat_div]; rfl
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
            res_pos.toNumber.exponent_ largeRange.min largeRange.max .towards_zero = .ok result := h_norm
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
      have h_reduce := doNormalize_capAtMaxRep_reduce res_pos.toNumber.negative_ m res_pos.exponent_ .towards_zero
        hm_gt hm_le_maxMant (h_rp_exp ▸ h_ze_ge) (h_rp_exp ▸ h_ze_lt_max)
      have h_norm2 : doNormalize res_pos.toNumber.negative_ res_pos.toNumber.mantissa_
          res_pos.toNumber.exponent_ largeRange.min largeRange.max .towards_zero = .ok result := h_norm
      rw [show res_pos.toNumber.mantissa_ = m from rfl,
          show res_pos.toNumber.exponent_ = res_pos.exponent_ from rfl, h_reduce] at h_norm2
      set G : Guard := (if res_pos.toNumber.negative_ then Guard.new.set_negative else Guard.new).push (m % 10) with hG_def
      cases hru : G.doRoundUp res_pos.toNumber.negative_ (m / 10) (res_pos.exponent_ + 1)
          largeRange.min largeRange.max .towards_zero "Number::normalize 2" with
      | error err => rw [hru] at h_norm2; simp only [Except.map] at h_norm2; exact absurd h_norm2 (by intro hc; cases hc)
      | ok res2 =>
        rw [hru] at h_norm2
        simp only [Except.map] at h_norm2
        have h_result_eq : result = res2.toNumber := (Except.ok.inj h_norm2).symm
        have hres2_mant_ne : res2.mantissa_ ≠ 0 := by
          rw [h_result_eq] at hresult; exact hresult
        -- doRoundUp value: for towards_zero it ALWAYS truncates.
        set W : ℚ := (res2.mantissa_.toNat : ℚ) * 10 ^ res2.exponent_ with hW_def
        have hWtrunc : W = ((m / 10).toNat : ℚ) * 10 ^ (res_pos.exponent_ + 1) := by
          have := doRoundUp_value_towards_zero_truncate G res_pos.toNumber.negative_ (m / 10)
            (res_pos.exponent_ + 1) "Number::normalize 2" res2 hru hres2_mant_ne
          simp only at this; rw [hW_def]; exact this
        -- (m/10)*10^(ze'+1) = (m.toNat - m.toNat%10) * 10^ze'.
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
        -- |W - m.toNat * 10^ze'| ≤ 10*10^ze'.
        have hWbound : |W - (m.toNat : ℚ) * 10 ^ res_pos.exponent_| ≤ 10 * 10 ^ res_pos.exponent_ := by
          rw [hWtrunc, hVeq]
          rw [show ((m.toNat : ℚ) - ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_ - (m.toNat : ℚ) * 10 ^ res_pos.exponent_
                = (- ((m.toNat % 10 : ℕ) : ℚ)) * 10 ^ res_pos.exponent_ from by ring,
              abs_mul, abs_of_nonneg h10rp_nn]
          apply mul_le_mul_of_nonneg_right _ h10rp_nn
          rw [abs_neg, abs_of_nonneg hmod_q_nn]; linarith
        rw [h_rp_exp] at hWbound hVeq
        -- result value = W.
        have h_result_val : (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ = W := by
          rw [h_result_eq, hW_def]; rfl
        rw [h_result_val, hT_def]
        -- m.toNat = res_pos.mantissa_.toNat = zm.toNat.
        have hm_eq_rp : (m.toNat : ℚ) = (res_pos.mantissa_.toNat : ℚ) := by rw [hm_def]
        have h_half : |(m.toNat : ℚ) - ((zm.toNat : ℚ) - f)| ≤ 1 := by
          rw [hm_eq_rp]; exact h_full_ulp
        have hm_eq_zm_nat : m.toNat = zm.toNat := by rw [hm_def]; exact h_rp_eq
        have hm_le_zm : (m.toNat : ℚ) ≤ (zm.toNat : ℚ) := by
          rw [hm_eq_zm_nat]
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

end XRPL.Model.Protocol

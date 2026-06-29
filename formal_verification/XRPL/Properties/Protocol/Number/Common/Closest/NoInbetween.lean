import XRPL.Properties.Protocol.Number.Common.Notation
-- Operand-generic no-inbetween frames over the (zm, ze, f, g) doRoundUp state.
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Common.Closest.Gap


namespace XRPL.Model.Protocol

/-! # No-inbetween frames

Each `operator`'s discrete-rounding proof reduces to the same statement about
the pre-`doRoundUp` state `(zm, ze', f, g)`: no normalized Number sits strictly
between the rounded result and the exact value `±(zm + f)·10^ze'`. These frames
prove that once and for all, parameterized over the operand-specific pieces:

* `truth` — the exact rational value of the operation,
* `h_truth_abs` — `|truth| = (zm + f)·10^ze'` (from the op's AlgorithmicFacts),
* `h_res_nn` / `h_res_np` — result-sign transfer (`truth` sign ⟹ result sign),
* `h_sbit_t` / `h_sbit_f` — guard sign-bit transfer (directed modes only),
* `loc` — the op's `doRoundUp` error-location string.

Consumers (Mul, Normalize, …) destructure their facts bundle and apply these. -/

/-- `.towards_zero`, below form: with `result ≤ truth`, nothing normalized lies
in `(result, truth]`. -/
theorem no_inbetween_below_towards_zero_frame (result : Number) (truth : ℚ)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) (loc : String)
    (_hzm_ge : mantissaFloor ≤ zm.toNat)
    (hzm_le : zm.toNat ≤ maxRepUp.toNat)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_truth_abs : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze')
    (h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .towards_zero loc = .ok res_pos)
    (h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (hzm_succ : mantissaFloorSucc ≤ zm.toNat)
    (h_res_nn : 0 < truth → (0 : ℚ) ≤ result.toRat)
    (h_le : result.toRat ≤ truth) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ truth) := by
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxRU : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_lt_m h_m_le_truth
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- Cusp range: maxRep < zm ≤ maxRepUp. `.towards_zero` never fires, so the
    -- fired legs of the cusp value cases are refuted outright.
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, h_val, h_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .towards_zero
      h_zm_gt_rep hzm_le loc res_pos h_rup_pos hres_pos_mant_ne
    rw [h_val] at h_result_abs
    have h_maxRU_v : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_maxR_q : (maxRep.toNat : ℚ) = maxRepNat := by rw [maxRep_val]; norm_num
    have hzm_q_lb : (9223372036854775808 : ℚ) ≤ (zm.toNat : ℚ) := by
      have h : 9223372036854775808 ≤ zm.toNat := by rw [maxRep_val] at h_zm_gt_rep; omega
      exact_mod_cast h
    have hzm_q_ub : (zm.toNat : ℚ) ≤ maxRepCuspTarget := by
      have h : zm.toNat ≤ 9223372036854775810 := by omega
      exact_mod_cast h
    rcases h_cases with ⟨hv, hzm_lt_up, _⟩ | ⟨hv, hsub⟩ | ⟨hv, _, hb_true⟩
    · -- v = maxRep (truncate clamp).
      rw [hv] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · -- the clamp sits strictly above truth — h_le refuted.
        have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_neg h_result_neg] at h_result_abs
        have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
      · -- forbidden interval sits inside the cusp gap.
        have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
        apply no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos
        · rw [h_maxR_q]; linarith
        · have h_zm_le9 : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
            have h : zm.toNat ≤ 9223372036854775809 := by
              rw [h_maxRU_v] at hzm_lt_up; omega
            exact_mod_cast h
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (maxRepCuspTarget : ℚ) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
    · -- v = maxRepUp: only the kept/dead legs survive (`.towards_zero` cannot fire).
      rcases hsub with ⟨_, _⟩ | ⟨_, hb_true⟩
      swap
      · rw [roundUp_bool_towards_zero_false (g.pushOverflow zm .towards_zero) zm] at hb_true
        exact absurd hb_true Bool.false_ne_true
      have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
      rw [hv'] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
        have h_m_neg : m.toRat < 0 := lt_of_le_of_lt h_m_le_truth h_truth_neg
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_neg h_result_neg] at h_result_abs
        apply no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg
        · linarith
        · have h_lt : (maxRep.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [h_maxR_q]; linarith) h10ze_pos
          linarith
      · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
        have h_k_cast : ((9223372036854775810 : ℕ) : ℚ) = (9223372036854775810 : ℚ) := by
          norm_num
        apply no_normalized_in_open_ulp_gap_pos_zm ze' 9223372036854775810 (by norm_num)
          (by norm_num) m h_norm h_m_pos
        · rw [h_k_cast]; linarith
        · rw [h_k_cast]
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze'
              < ((9223372036854775810 : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
    · -- v = maxRepNat+13 requires a fired decision — impossible for `.towards_zero`.
      rw [roundUp_bool_towards_zero_false g zm] at hb_true
      exact absurd hb_true Bool.false_ne_true
  -- In-range: `.towards_zero` truncates, result value = zm · 10^ze'.
  have h_val := doRoundUp_value_towards_zero_truncate g false zm ze' h_zm_le_rep
    loc res_pos h_rup_pos hres_pos_mant_ne
  rw [h_val] at h_result_abs
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · -- Negative truth: truncation lands above it; h_le forces exactness, interval empty.
    have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
    rw [abs_of_neg h_truth_neg] at h_truth_abs
    rw [abs_of_neg h_result_neg] at h_result_abs
    have h_f_le : f ≤ 0 := by
      by_contra h_pos
      push_neg at h_pos
      have h_lt : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
        mul_lt_mul_of_pos_right (by linarith) h10ze_pos
      linarith
    have h_f_zero : f = 0 := le_antisymm h_f_le hf_nn
    rw [h_f_zero, add_zero] at h_truth_abs
    linarith
  · -- Positive truth: forbidden interval sits inside the ulp gap above zm.
    have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    rw [abs_of_nonneg h_truth_nn] at h_truth_abs
    have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
    rw [abs_of_nonneg h_result_nn] at h_result_abs
    have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
    have h_m_lo : (zm.toNat : ℚ) * 10 ^ ze' < m.toRat := by
      rw [← h_result_abs]; exact h_lt_m
    have h_m_hi : m.toRat < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
      calc m.toRat ≤ truth := h_m_le_truth
        _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
        _ < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
            apply mul_lt_mul_of_pos_right _ h10ze_pos; linarith
    exact no_normalized_in_open_ulp_gap_pos_zm ze' zm.toNat
      hzm_succ h_zm_lt m h_norm h_m_pos h_m_lo h_m_hi

/-- `.towards_zero`, above form: with `truth ≤ result`, nothing normalized lies
in `[truth, result)`. -/
theorem no_inbetween_above_towards_zero_frame (result : Number) (truth : ℚ)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) (loc : String)
    (_hzm_ge : mantissaFloor ≤ zm.toNat)
    (hzm_le : zm.toNat ≤ maxRepUp.toNat)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_truth_abs : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze')
    (h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .towards_zero loc = .ok res_pos)
    (h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (hzm_succ : mantissaFloorSucc ≤ zm.toNat)
    (h_res_np : truth < 0 → result.toRat ≤ 0)
    (h_ge : truth ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (truth ≤ m.toRat) := by
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxRU : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_m_lt h_truth_le_m
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- Cusp range, mirrored.
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, h_val, h_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .towards_zero
      h_zm_gt_rep hzm_le loc res_pos h_rup_pos hres_pos_mant_ne
    rw [h_val] at h_result_abs
    have h_maxRU_v : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_maxR_q : (maxRep.toNat : ℚ) = maxRepNat := by rw [maxRep_val]; norm_num
    have hzm_q_lb : (9223372036854775808 : ℚ) ≤ (zm.toNat : ℚ) := by
      have h : 9223372036854775808 ≤ zm.toNat := by rw [maxRep_val] at h_zm_gt_rep; omega
      exact_mod_cast h
    have hzm_q_ub : (zm.toNat : ℚ) ≤ maxRepCuspTarget := by
      have h : zm.toNat ≤ 9223372036854775810 := by omega
      exact_mod_cast h
    rcases h_cases with ⟨hv, hzm_lt_up, _⟩ | ⟨hv, hsub⟩ | ⟨hv, _, hb_true⟩
    · -- v = maxRep (truncate clamp).
      rw [hv] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · -- forbidden interval sits inside the cusp gap (negative side).
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
        have h_m_neg : m.toRat < 0 := lt_of_lt_of_le h_m_lt h_result_np
        rw [abs_of_nonpos h_result_np] at h_result_abs
        apply no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg
        · have h_zm_le9 : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
            have h : zm.toNat ≤ 9223372036854775809 := by
              rw [h_maxRU_v] at hzm_lt_up; omega
            exact_mod_cast h
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (maxRepCuspTarget : ℚ) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · rw [h_maxR_q]; linarith
      · -- positive truth: truncation cannot exceed it strictly enough — h_ge refuted.
        have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := le_trans (le_of_lt h_truth_pos) h_ge
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
    · -- v = maxRepUp: only the kept/dead legs survive.
      rcases hsub with ⟨_, _⟩ | ⟨_, hb_true⟩
      swap
      · rw [roundUp_bool_towards_zero_false (g.pushOverflow zm .towards_zero) zm] at hb_true
        exact absurd hb_true Bool.false_ne_true
      have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
      rw [hv'] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · rw [abs_of_neg h_truth_neg] at h_truth_abs
        have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
        have h_m_neg : m.toRat < 0 := lt_of_lt_of_le h_m_lt h_result_np
        rw [abs_of_nonpos h_result_np] at h_result_abs
        have h_k_cast : ((9223372036854775810 : ℕ) : ℚ) = (9223372036854775810 : ℚ) := by
          norm_num
        apply no_normalized_in_open_ulp_gap_neg_zm ze' 9223372036854775810 (by norm_num)
          (by norm_num) m h_norm h_m_neg
        · rw [h_k_cast]
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze'
              < ((9223372036854775810 : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · rw [h_k_cast]; linarith
      · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := le_trans (le_of_lt h_truth_pos) h_ge
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
        apply no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos
        · rw [h_maxR_q]
          have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · linarith
    · -- v = maxRepNat+13 requires a fired decision — impossible for `.towards_zero`.
      rw [roundUp_bool_towards_zero_false g zm] at hb_true
      exact absurd hb_true Bool.false_ne_true
  -- In-range: truncate.
  have h_val := doRoundUp_value_towards_zero_truncate g false zm ze' h_zm_le_rep
    loc res_pos h_rup_pos hres_pos_mant_ne
  rw [h_val] at h_result_abs
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · -- Negative truth: forbidden interval sits inside the ulp gap below −zm.
    rw [abs_of_neg h_truth_neg] at h_truth_abs
    have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
    have h_m_neg : m.toRat < 0 := lt_of_lt_of_le h_m_lt h_result_np
    rw [abs_of_nonpos h_result_np] at h_result_abs
    have h_neg_m_lo : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < m.toRat := by
      have h_truth_gt : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < truth := by
        have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
      linarith
    have h_neg_m_hi : m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := by
      have h_result_eq : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by linarith
      rw [h_result_eq] at h_m_lt; exact h_m_lt
    exact no_normalized_in_open_ulp_gap_neg_zm ze' zm.toNat
      hzm_succ h_zm_lt m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
  · -- Positive truth: h_ge forces exactness, interval empty.
    have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    rw [abs_of_nonneg h_truth_nn] at h_truth_abs
    have h_result_nn : (0 : ℚ) ≤ result.toRat := le_trans (le_of_lt h_truth_pos) h_ge
    rw [abs_of_nonneg h_result_nn] at h_result_abs
    have h_f_le : f ≤ 0 := by
      by_contra h_pos
      push_neg at h_pos
      have h_lt : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
        mul_lt_mul_of_pos_right (by linarith) h10ze_pos
      linarith
    have h_f_zero : f = 0 := le_antisymm h_f_le hf_nn
    rw [h_f_zero, add_zero] at h_truth_abs
    linarith

/-- `.to_nearest`, below form: with `result ≤ truth`, nothing normalized lies
in `(result, truth]`. -/
theorem no_inbetween_below_to_nearest_frame (result : Number) (truth : ℚ)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) (loc : String)
    (hzm_ge : mantissaFloor ≤ zm.toNat)
    (hzm_le : zm.toNat ≤ maxRepUp.toNat)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (hfloor_constr : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (h_truth_abs : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze')
    (h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res_pos)
    (h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (h_sru_pos : g.shouldRoundUp_to_nearest zm → 0 < f)
    (h_floor_force : zm.toNat = mantissaFloor → ¬ g.shouldRoundUp_to_nearest zm → False)
    (h_res_nn : 0 < truth → (0 : ℚ) ≤ result.toRat)
    (h_le : result.toRat ≤ truth) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ truth) := by
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxRU : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_lt_m h_m_le_truth
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- Cusp range: maxRep < zm ≤ maxRepUp.
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, h_val, h_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .to_nearest
      h_zm_gt_rep hzm_le loc res_pos h_rup_pos hres_pos_mant_ne
    rw [h_val] at h_result_abs
    have h_maxRU_v : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_maxR_q : (maxRep.toNat : ℚ) = maxRepNat := by rw [maxRep_val]; norm_num
    have hzm_q_lb : (9223372036854775808 : ℚ) ≤ (zm.toNat : ℚ) := by
      have h : 9223372036854775808 ≤ zm.toNat := by rw [maxRep_val] at h_zm_gt_rep; omega
      exact_mod_cast h
    have hzm_q_ub : (zm.toNat : ℚ) ≤ maxRepCuspTarget := by
      have h : zm.toNat ≤ 9223372036854775810 := by omega
      exact_mod_cast h
    rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
    · -- Negative truth: result.toRat = -(v · 10^ze').
      have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
      have h_m_neg : m.toRat < 0 := lt_of_le_of_lt h_m_le_truth h_truth_neg
      rw [abs_of_neg h_truth_neg] at h_truth_abs
      rw [abs_of_neg h_result_neg] at h_result_abs
      rcases h_cases with ⟨hv, _, _⟩ | ⟨hv, _⟩ | ⟨hv, hzm_eq_up, hb_true⟩
      · -- v = maxRep: the truncate clamp sits strictly above truth — h_le refuted.
        rw [hv] at h_result_abs
        have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
      · -- v = maxRepUp: the forbidden interval sits inside the cusp gap.
        have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        apply no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg
        · linarith
        · have h_lt : (maxRep.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [h_maxR_q]; linarith) h10ze_pos
          linarith
      · -- v = maxRepNat+13 (zm = maxRepUp, round fired → f > 0): the forbidden
        -- interval sits inside the upper cusp gap.
        have hv' : v = (twoPow63Add12 : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        have hzm_eq : zm = maxRepUp := UInt64.toNat.inj hzm_eq_up
        have h_round_1 : g.round .to_nearest = 1 := by
          rw [hzm_eq] at hb_true
          rw [show ((maxRepUp % 2 == 1) : Bool) = false from by decide,
              Bool.and_false, Bool.or_false] at hb_true
          exact beq_iff_eq.mp hb_true
        have h_f_pos : 0 < f := h_sru_pos (Or.inl h_round_1)
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepCuspTarget := by
          rw [hzm_eq_up, h_maxRU_v]; norm_num
        apply no_normalized_in_upper_cusp_gap_neg ze' m h_norm h_m_neg
        · linarith
        · have h_lt : (maxRepCuspTarget : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [hzm_q_eq]; linarith) h10ze_pos
          linarith
    · -- Positive truth.
      have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
      rw [abs_of_nonneg h_truth_nn] at h_truth_abs
      have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
      rw [abs_of_nonneg h_result_nn] at h_result_abs
      have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
      rcases h_cases with ⟨hv, hzm_lt_up, _⟩ | ⟨hv, _⟩ | ⟨hv, hzm_eq_up, _⟩
      · -- v = maxRep: the forbidden interval sits inside the cusp gap.
        rw [hv] at h_result_abs
        apply no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos
        · rw [h_maxR_q]; linarith
        · have h_zm_le9 : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
            have h : zm.toNat ≤ 9223372036854775809 := by
              rw [h_maxRU_v] at hzm_lt_up; omega
            exact_mod_cast h
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (maxRepCuspTarget : ℚ) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
      · -- v = maxRepUp: the forbidden interval sits inside the ulp gap above maxRepUp.
        have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        have h_k_cast : ((9223372036854775810 : ℕ) : ℚ) = (9223372036854775810 : ℚ) := by
          norm_num
        apply no_normalized_in_open_ulp_gap_pos_zm ze' 9223372036854775810 (by norm_num)
          (by norm_num) m h_norm h_m_pos
        · rw [h_k_cast]; linarith
        · rw [h_k_cast]
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze'
              < ((9223372036854775810 : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
      · -- v = maxRepNat+13: result sits strictly above truth — h_le refuted.
        have hv' : v = (twoPow63Add12 : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (twoPow63Add12 : ℚ) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
    have h_m_neg : m.toRat < 0 := lt_of_le_of_lt h_m_le_truth h_truth_neg
    have h_truth_abs_eq : |truth| = -truth := abs_of_neg h_truth_neg
    have h_result_abs_eq : |result.toRat| = -result.toRat := abs_of_neg h_result_neg
    rw [h_truth_abs_eq] at h_truth_abs
    rw [h_result_abs_eq] at h_result_abs
    by_cases h_ru : g.shouldRoundUp_to_nearest zm
    · have h_f_pos : 0 < f := h_sru_pos h_ru
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · have h_val := doRoundUp_value_to_nearest_roundUp_noCusp g zm ze' h_ru h_cusp loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_neg_m_lo : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < m.toRat := by
          have : -result.toRat = ((zm.toNat : ℚ) + 1) * 10 ^ ze' := h_result_abs
          have h_result_eq : result.toRat = -(((zm.toNat : ℚ) + 1) * 10 ^ ze') := by linarith
          rw [h_result_eq] at h_lt_m
          exact h_lt_m
        have h_neg_m_hi : m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := by
          calc m.toRat ≤ truth := h_m_le_truth
            _ = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by linarith [h_truth_abs]
            _ < -((zm.toNat : ℚ) * 10 ^ ze') := by
                have h_compare : (zm.toNat : ℚ) < (zm.toNat : ℚ) + f := by linarith
                have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
                  mul_lt_mul_of_pos_right h_compare h10ze_pos
                linarith
        by_cases h_zm_floor : zm.toNat = mantissaFloor
        · have h_f_ge : (8 : ℚ) / 10 ≤ f := hfloor_constr h_zm_floor
          have h_m_le_strong : m.toRat ≤ -(((zm.toNat : ℚ) + 8/10) * 10 ^ ze') := by
            calc m.toRat ≤ truth := h_m_le_truth
              _ = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by linarith [h_truth_abs]
              _ ≤ -(((zm.toNat : ℚ) + 8/10) * 10 ^ ze') := by
                  have : (zm.toNat : ℚ) + 8/10 ≤ (zm.toNat : ℚ) + f := by linarith
                  have h_mul : ((zm.toNat : ℚ) + 8/10) * 10 ^ ze' ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
                    mul_le_mul_of_nonneg_right this (le_of_lt h10ze_pos)
                  linarith
          exact no_normalized_in_ulp_gap_at_floor_neg ze' zm.toNat h_zm_floor h_zm_lt
            m h_norm h_m_neg h_neg_m_lo h_m_le_strong
        · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
          exact no_normalized_in_open_ulp_gap_neg_zm ze' zm.toNat
            h_zm_ge_strict h_zm_lt m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := by
          have h_eq_nat : zm.toNat = maxRep.toNat := by omega
          exact UInt64.toNat.inj h_eq_nat
        rcases h_ru with h_round1 | h_tie
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp_round1 g zm ze' h_zm_eq
            h_round1 loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          have h_fze_pos : (0 : ℚ) < f * 10 ^ ze' := mul_pos h_f_pos h10ze_pos
          have h_contr : (maxRep.toNat : ℚ) * 10 ^ ze' + f * 10 ^ ze' ≤ (maxRep.toNat : ℚ) * 10 ^ ze' := by
            have h1 : -result.toRat = (maxRep.toNat : ℚ) * 10 ^ ze' := h_result_abs
            have h2 : -truth = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
            have h3 : -truth ≤ -result.toRat := by linarith
            calc (maxRep.toNat : ℚ) * 10 ^ ze' + f * 10 ^ ze'
                = ((maxRep.toNat : ℚ) + f) * 10 ^ ze' := by ring
              _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by rw [h_zm_eq_q]
              _ = -truth := h2.symm
              _ ≤ -result.toRat := h3
              _ = (maxRep.toNat : ℚ) * 10 ^ ze' := h1
          linarith
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp g zm ze' h_zm_eq h_tie loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          have h_neg_m_lo : -((maxRepCuspTarget : ℚ) * 10 ^ ze') < m.toRat := by
            have h_result_eq : result.toRat = -((maxRepCuspTarget : ℚ) * 10 ^ ze') := by
              linarith [h_result_abs]
            rw [h_result_eq] at h_lt_m; exact h_lt_m
          have h_neg_m_hi : m.toRat < -((maxRep.toNat : ℚ) * 10 ^ ze') := by
            have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
            calc m.toRat ≤ truth := h_m_le_truth
              _ = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by linarith [h_truth_abs]
              _ < -((maxRep.toNat : ℚ) * 10 ^ ze') := by
                  rw [h_zm_eq_q]
                  have h_compare : (maxRep.toNat : ℚ) < (maxRep.toNat : ℚ) + f := by linarith
                  have h_compare_mul : (maxRep.toNat : ℚ) * 10 ^ ze' < ((maxRep.toNat : ℚ) + f) * 10 ^ ze' :=
                    mul_lt_mul_of_pos_right h_compare h10ze_pos
                  linarith
          exact no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
    · -- Empty interval: (zm+f)*10^ze' ≤ -m < zm*10^ze' is impossible since f ≥ 0.
      exfalso
      have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_neg_m_ge : ((zm.toNat : ℚ) + f) * 10 ^ ze' ≤ -m.toRat := by
        have := h_m_le_truth
        linarith [h_truth_abs]
      have h_neg_m_lt : -m.toRat < (zm.toNat : ℚ) * 10 ^ ze' := by
        have := h_lt_m
        linarith [h_result_abs]
      have h_chain : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (zm.toNat : ℚ) * 10 ^ ze' := by linarith
      have h_compare : (zm.toNat : ℚ) ≤ (zm.toNat : ℚ) + f := by linarith
      have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
        mul_le_mul_of_nonneg_right h_compare (le_of_lt h10ze_pos)
      linarith
  · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    have h_truth_abs_eq : |truth| = truth := abs_of_nonneg h_truth_nn
    rw [h_truth_abs_eq] at h_truth_abs
    have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
    have h_result_abs_eq : |result.toRat| = result.toRat := abs_of_nonneg h_result_nn
    rw [h_result_abs_eq] at h_result_abs
    by_cases h_ru : g.shouldRoundUp_to_nearest zm
    · -- shouldRoundUp_to_nearest: exfalso (result > truth) or cusp-gap (no normalized m).
      exfalso
      have h_f_pos : 0 < f := h_sru_pos h_ru
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · have h_val := doRoundUp_value_to_nearest_roundUp_noCusp g zm ze' h_ru h_cusp loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_compare : (zm.toNat : ℚ) + f < (zm.toNat : ℚ) + 1 := by linarith
        have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right h_compare h10ze_pos
        linarith [h_result_abs]
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
        have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
        rcases h_ru with h_round1 | h_tie
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp_round1 g zm ze' h_zm_eq
            h_round1 loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          have h_m_lo : (maxRep.toNat : ℚ) * 10 ^ ze' < m.toRat := by
            rw [← h_result_abs]; exact h_lt_m
          have h_m_hi : m.toRat < (maxRepCuspTarget : ℚ) * 10 ^ ze' := by
            have h_f_lt1 : f < 1 := hf_lt
            have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
            have h_maxR_v : maxRep.toNat = maxRepNat := maxRep_val
            calc m.toRat ≤ truth := h_m_le_truth
              _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
              _ < ((maxRep.toNat : ℚ) + 1) * 10 ^ ze' := by
                  apply mul_lt_mul_of_pos_right _ h10ze_pos; rw [← h_zm_eq_q]; linarith
              _ ≤ (maxRepCuspTarget : ℚ) * 10 ^ ze' := by
                  apply mul_le_mul_of_nonneg_right _ (le_of_lt h10ze_pos)
                  rw [h_maxR_v]; norm_num
          exact no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos h_m_lo h_m_hi
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp g zm ze' h_zm_eq h_tie loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          have h_maxR_v : maxRep.toNat = maxRepNat := maxRep_val
          have h_le_810 : (zm.toNat : ℚ) + 1 ≤ maxRepCuspTarget := by
            rw [show zm.toNat = maxRep.toNat from by rw [h_zm_eq], h_maxR_v]; norm_num
          have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith [mul_le_mul_of_nonneg_right h_le_810 (le_of_lt h10ze_pos), h_result_abs]
    · have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
      have h_m_lo : (zm.toNat : ℚ) * 10 ^ ze' < m.toRat := by
        rw [← h_result_abs]; exact h_lt_m
      have h_m_hi : m.toRat < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
        calc m.toRat ≤ truth := h_m_le_truth
          _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
          _ < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
              apply mul_lt_mul_of_pos_right _ h10ze_pos; linarith
      by_cases h_zm_floor : zm.toNat = mantissaFloor
      · -- At floor the round decision must have fired (`h_floor_force`).
        exact (h_floor_force h_zm_floor h_ru).elim
      · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
        exact no_normalized_in_open_ulp_gap_pos_zm ze' zm.toNat
          h_zm_ge_strict h_zm_lt m h_norm h_m_pos h_m_lo h_m_hi

/-- `.to_nearest`, above form: with `truth ≤ result`, nothing normalized lies
in `[truth, result)`. -/
theorem no_inbetween_above_to_nearest_frame (result : Number) (truth : ℚ)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) (loc : String)
    (hzm_ge : mantissaFloor ≤ zm.toNat)
    (hzm_le : zm.toNat ≤ maxRepUp.toNat)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (hfloor_constr : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (h_truth_abs : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze')
    (h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res_pos)
    (h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (h_sru_pos : g.shouldRoundUp_to_nearest zm → 0 < f)
    (h_floor_force : zm.toNat = mantissaFloor → ¬ g.shouldRoundUp_to_nearest zm → False)
    (h_res_np : truth < 0 → result.toRat ≤ 0)
    (h_res_nn : 0 < truth → (0 : ℚ) ≤ result.toRat)
    (h_ge : truth ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (truth ≤ m.toRat) := by
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxRU : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_m_lt h_truth_le_m
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- Cusp range: maxRep < zm ≤ maxRepUp, mirrored.
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, h_val, h_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .to_nearest
      h_zm_gt_rep hzm_le loc res_pos h_rup_pos hres_pos_mant_ne
    rw [h_val] at h_result_abs
    have h_maxRU_v : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_maxR_q : (maxRep.toNat : ℚ) = maxRepNat := by rw [maxRep_val]; norm_num
    have hzm_q_lb : (9223372036854775808 : ℚ) ≤ (zm.toNat : ℚ) := by
      have h : 9223372036854775808 ≤ zm.toNat := by rw [maxRep_val] at h_zm_gt_rep; omega
      exact_mod_cast h
    have hzm_q_ub : (zm.toNat : ℚ) ≤ maxRepCuspTarget := by
      have h : zm.toNat ≤ 9223372036854775810 := by omega
      exact_mod_cast h
    rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
    · -- Negative truth: result is nonpositive.
      have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
      have h_m_neg : m.toRat < 0 := lt_of_lt_of_le h_m_lt h_result_np
      rw [abs_of_neg h_truth_neg] at h_truth_abs
      rw [abs_of_nonpos h_result_np] at h_result_abs
      rcases h_cases with ⟨hv, hzm_lt_up, _⟩ | ⟨hv, _⟩ | ⟨hv, hzm_eq_up, _⟩
      · -- v = maxRep: the forbidden interval sits inside the cusp gap.
        rw [hv] at h_result_abs
        apply no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg
        · have h_zm_le9 : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
            have h : zm.toNat ≤ 9223372036854775809 := by
              rw [h_maxRU_v] at hzm_lt_up; omega
            exact_mod_cast h
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (maxRepCuspTarget : ℚ) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · rw [h_maxR_q]; linarith
      · -- v = maxRepUp: the forbidden interval sits inside the ulp gap below it.
        have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        have h_k_cast : ((9223372036854775810 : ℕ) : ℚ) = (9223372036854775810 : ℚ) := by
          norm_num
        apply no_normalized_in_open_ulp_gap_neg_zm ze' 9223372036854775810 (by norm_num)
          (by norm_num) m h_norm h_m_neg
        · rw [h_k_cast]
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze'
              < ((9223372036854775810 : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · rw [h_k_cast]; linarith
      · -- v = maxRepNat+13: result sits strictly below truth — h_ge refuted.
        have hv' : v = (twoPow63Add12 : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (twoPow63Add12 : ℚ) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
    · -- Positive truth: result is nonnegative.
      have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
      rw [abs_of_nonneg h_truth_nn] at h_truth_abs
      have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
      rw [abs_of_nonneg h_result_nn] at h_result_abs
      have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
      rcases h_cases with ⟨hv, _, _⟩ | ⟨hv, _⟩ | ⟨hv, hzm_eq_up, hb_true⟩
      · -- v = maxRep: result sits strictly below truth — h_ge refuted.
        rw [hv] at h_result_abs
        have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
      · -- v = maxRepUp: the forbidden interval sits inside the cusp gap.
        have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        apply no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos
        · rw [h_maxR_q]
          have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · linarith
      · -- v = maxRepNat+13 (zm = maxRepUp, round fired → f > 0): the forbidden
        -- interval sits inside the upper cusp gap.
        have hv' : v = (twoPow63Add12 : ℚ) := by rw [hv]; norm_num
        rw [hv'] at h_result_abs
        have hzm_eq : zm = maxRepUp := UInt64.toNat.inj hzm_eq_up
        have h_round_1 : g.round .to_nearest = 1 := by
          rw [hzm_eq] at hb_true
          rw [show ((maxRepUp % 2 == 1) : Bool) = false from by decide,
              Bool.and_false, Bool.or_false] at hb_true
          exact beq_iff_eq.mp hb_true
        have h_f_pos : 0 < f := h_sru_pos (Or.inl h_round_1)
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepCuspTarget := by
          rw [hzm_eq_up, h_maxRU_v]; norm_num
        apply no_normalized_in_upper_cusp_gap_pos ze' m h_norm h_m_pos
        · have h_lt : (maxRepCuspTarget : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [hzm_q_eq]; linarith) h10ze_pos
          linarith
        · linarith
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
    have h_m_np : m.toRat ≤ 0 := le_of_lt (lt_of_lt_of_le h_m_lt (by linarith : result.toRat ≤ 0))
    have h_truth_abs_eq : |truth| = -truth := abs_of_neg h_truth_neg
    have h_result_abs_eq : |result.toRat| = -result.toRat := by
      apply abs_of_nonpos h_result_np
    rw [h_truth_abs_eq] at h_truth_abs
    rw [h_result_abs_eq] at h_result_abs
    by_cases h_ru : g.shouldRoundUp_to_nearest zm
    · have h_f_pos : 0 < f := h_sru_pos h_ru
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · exfalso
        have h_val := doRoundUp_value_to_nearest_roundUp_noCusp g zm ze' h_ru h_cusp loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        have h_result_ge : ((zm.toNat : ℚ) + 1) * 10 ^ ze' ≤ |result.toRat| := by
          rw [h_result_abs_eq, h_result_abs, h_val]
        have h_compare : (zm.toNat : ℚ) + f < (zm.toNat : ℚ) + 1 := by linarith
        have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right h_compare h10ze_pos
        have h_abs_compare : |truth| < |result.toRat| := by
          rw [h_truth_abs_eq, h_truth_abs]; linarith
        have h_neg_compare : -truth < -result.toRat := by
          rw [← h_truth_abs_eq, ← h_result_abs_eq]; exact h_abs_compare
        linarith
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
        rcases h_ru with h_round1 | h_tie
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp_round1 g zm ze' h_zm_eq
            h_round1 loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          have h_maxR_v : maxRep.toNat = maxRepNat := maxRep_val
          have h_neg_m_hi : m.toRat < -((maxRep.toNat : ℚ) * 10 ^ ze') := by linarith
          have h_neg_m_lo : -((maxRepCuspTarget : ℚ) * 10 ^ ze') < m.toRat := by
            have h_truth_lb : -((maxRepCuspTarget : ℚ) * 10 ^ ze') < truth := by
              have h_truth_eq : truth = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by linarith [h_truth_abs]
              have h_zm_lt_cusp : (zm.toNat : ℚ) + f < (maxRepCuspTarget : ℚ) := by
                have h_zm_nat : (zm.toNat : ℚ) = (maxRepNat : ℚ) := by rw [h_zm_eq_q, h_maxR_v]; norm_num
                have h_cusp_nat : (maxRepCuspTarget : ℚ) = (maxRepNat : ℚ) + 3 := by norm_num
                rw [h_zm_nat, h_cusp_nat]; linarith
              have h_cusp_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (maxRepCuspTarget : ℚ) * 10 ^ ze' :=
                mul_lt_mul_of_pos_right h_zm_lt_cusp h10ze_pos
              linarith
            linarith
          have h_maxR_pos : (0 : ℚ) < (maxRep.toNat : ℚ) * 10 ^ ze' := by
            rw [← h_zm_eq_q]; exact mul_pos h_zm_pos_q h10ze_pos
          have h_m_neg : m.toRat < 0 := by linarith
          exact no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp g zm ze' h_zm_eq h_tie loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          have h_result_ge : ((zm.toNat : ℚ) + 1) * 10 ^ ze' ≤ |result.toRat| := by
            rw [h_result_abs_eq, h_result_abs, h_val]
            have h_maxR_v : maxRep.toNat = maxRepNat := maxRep_val
            have h_le_810 : (zm.toNat : ℚ) + 1 ≤ maxRepCuspTarget := by
              rw [show zm.toNat = maxRep.toNat from by rw [h_zm_eq], h_maxR_v]; norm_num
            exact mul_le_mul_of_nonneg_right h_le_810 (le_of_lt h10ze_pos)
          have h_compare : (zm.toNat : ℚ) + f < (zm.toNat : ℚ) + 1 := by linarith
          have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_compare h10ze_pos
          have h_abs_compare : |truth| < |result.toRat| := by
            rw [h_truth_abs_eq, h_truth_abs]; linarith
          have h_neg_compare : -truth < -result.toRat := by
            rw [← h_truth_abs_eq, ← h_result_abs_eq]; exact h_abs_compare
          linarith
    · have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_neg_m_lo : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < m.toRat := by
        have h_compare : (zm.toNat : ℚ) + f < (zm.toNat : ℚ) + 1 := by linarith
        have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right h_compare h10ze_pos
        have h_truth_gt : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < truth := by
          have : -truth = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
          linarith
        linarith
      have h_neg_m_hi : m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := by
        have : -result.toRat = (zm.toNat : ℚ) * 10 ^ ze' := h_result_abs
        have h_result_eq : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by linarith
        rw [h_result_eq] at h_m_lt; exact h_m_lt
      have h_m_neg_strict : m.toRat < 0 := by
        calc m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := h_neg_m_hi
          _ < 0 := by
              have : (0 : ℚ) < (zm.toNat : ℚ) * 10 ^ ze' := mul_pos h_zm_pos_q h10ze_pos
              linarith
      by_cases h_zm_floor : zm.toNat = mantissaFloor
      · -- At floor the round decision must have fired (`h_floor_force`).
        exact (h_floor_force h_zm_floor h_ru).elim
      · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
        exact no_normalized_in_open_ulp_gap_neg_zm ze' zm.toNat
          h_zm_ge_strict h_zm_lt m h_norm h_m_neg_strict h_neg_m_lo h_neg_m_hi
  · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    have h_truth_abs_eq : |truth| = truth := abs_of_nonneg h_truth_nn
    rw [h_truth_abs_eq] at h_truth_abs
    have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
    have h_result_abs_eq : |result.toRat| = result.toRat := abs_of_nonneg h_result_nn
    rw [h_result_abs_eq] at h_result_abs
    have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
    by_cases h_ru : g.shouldRoundUp_to_nearest zm
    · have h_f_pos : 0 < f := h_sru_pos h_ru
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · have h_val := doRoundUp_value_to_nearest_roundUp_noCusp g zm ze' h_ru h_cusp loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_m_lo : (zm.toNat : ℚ) * 10 ^ ze' < m.toRat := by
          have h_compare : (zm.toNat : ℚ) < (zm.toNat : ℚ) + f := by linarith
          have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_compare h10ze_pos
          have : ((zm.toNat : ℚ) + f) * 10 ^ ze' = truth := by linarith
          linarith
        have h_m_hi : m.toRat < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
          rw [← h_result_abs]; exact h_m_lt
        by_cases h_zm_floor : zm.toNat = mantissaFloor
        · have h_f_ge : (8 : ℚ) / 10 ≤ f := hfloor_constr h_zm_floor
          have h_m_strong : ((zm.toNat : ℚ) + 8/10) * 10 ^ ze' ≤ m.toRat := by
            calc ((zm.toNat : ℚ) + 8/10) * 10 ^ ze'
                ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
                  apply mul_le_mul_of_nonneg_right _ (le_of_lt h10ze_pos)
                  linarith
              _ = truth := by linarith
              _ ≤ m.toRat := h_truth_le_m
          exact no_normalized_in_ulp_gap_at_floor_pos ze' zm.toNat h_zm_floor h_zm_lt
            m h_norm h_m_pos h_m_strong h_m_hi
        · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
          exact no_normalized_in_open_ulp_gap_pos_zm ze' zm.toNat
            h_zm_ge_strict h_zm_lt m h_norm h_m_pos h_m_lo h_m_hi
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
        have h_m_lo : (maxRep.toNat : ℚ) * 10 ^ ze' < m.toRat := by
          have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          have h_compare : (maxRep.toNat : ℚ) < (maxRep.toNat : ℚ) + f := by linarith
          have h_compare_mul : (maxRep.toNat : ℚ) * 10 ^ ze' < ((maxRep.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_compare h10ze_pos
          calc (maxRep.toNat : ℚ) * 10 ^ ze'
              < ((maxRep.toNat : ℚ) + f) * 10 ^ ze' := h_compare_mul
            _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by rw [h_zm_eq_q]
            _ = truth := by linarith
            _ ≤ m.toRat := h_truth_le_m
        rcases h_ru with h_round1 | h_tie
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp_round1 g zm ze' h_zm_eq
            h_round1 loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          linarith
        · have h_val := doRoundUp_value_to_nearest_roundUp_cusp g zm ze' h_zm_eq h_tie loc res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          have h_m_hi : m.toRat < (maxRepCuspTarget : ℚ) * 10 ^ ze' := by
            rw [← h_result_abs]; exact h_m_lt
          exact no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos h_m_lo h_m_hi
    · -- No round-up + truth ≤ result forces truth = result, making truth ≤ m < result empty.
      exfalso
      have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_compare : (zm.toNat : ℚ) ≤ (zm.toNat : ℚ) + f := by linarith
      have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
        mul_le_mul_of_nonneg_right h_compare (le_of_lt h10ze_pos)
      have h_result_le_truth : result.toRat ≤ truth := by linarith [h_result_abs, h_truth_abs]
      have h_eq : result.toRat = truth := le_antisymm h_result_le_truth h_ge
      rw [h_eq] at h_m_lt
      linarith

/-- `.downward`, below form: with `result ≤ truth`, nothing normalized lies
in `(result, truth]`. (`.downward` needs only this side — the direction always
holds.) -/
theorem no_inbetween_below_downward_frame (result : Number) (truth : ℚ)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) (loc : String)
    (_hzm_ge : mantissaFloor ≤ zm.toNat)
    (hzm_le : zm.toNat ≤ maxRepUp.toNat)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_truth_abs : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze')
    (h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .downward loc = .ok res_pos)
    (h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (h_sru_pos : g.shouldRoundUp_downward → 0 < f)
    (h_content_zero : g.digits_ = 0 → g.xbit_ = false → f = 0)
    (hzm_succ : mantissaFloorSucc ≤ zm.toNat)
    (h_res_nn : 0 < truth → (0 : ℚ) ≤ result.toRat)
    (h_sbit_t : truth < 0 → g.sbit_ = true)
    (h_sbit_f : 0 < truth → g.sbit_ = false)
    (h_le : result.toRat ≤ truth) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ truth) := by
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxRU : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_lt_m h_m_le_truth
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- Cusp range: maxRep < zm ≤ maxRepUp.
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, h_val, h_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .downward
      h_zm_gt_rep hzm_le loc res_pos h_rup_pos hres_pos_mant_ne
    rw [h_val] at h_result_abs
    have h_maxRU_v : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_maxR_q : (maxRep.toNat : ℚ) = maxRepNat := by rw [maxRep_val]; norm_num
    have hzm_q_lb : (9223372036854775808 : ℚ) ≤ (zm.toNat : ℚ) := by
      have h : 9223372036854775808 ≤ zm.toNat := by rw [maxRep_val] at h_zm_gt_rep; omega
      exact_mod_cast h
    have hzm_q_ub : (zm.toNat : ℚ) ≤ maxRepCuspTarget := by
      have h : zm.toNat ≤ 9223372036854775810 := by omega
      exact_mod_cast h
    rcases h_cases with ⟨hv, hzm_lt_up, _⟩ | ⟨hv, _⟩ | ⟨hv, hzm_eq_up, hb_true⟩
    · -- v = maxRep (truncate clamp).
      rw [hv] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · -- the clamp sits strictly above truth — h_le refuted.
        have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_neg h_result_neg] at h_result_abs
        have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
      · -- forbidden interval sits inside the cusp gap.
        have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
        apply no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos
        · rw [h_maxR_q]; linarith
        · have h_zm_le9 : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
            have h : zm.toNat ≤ 9223372036854775809 := by
              rw [h_maxRU_v] at hzm_lt_up; omega
            exact_mod_cast h
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (maxRepCuspTarget : ℚ) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
    · -- v = maxRepUp.
      have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
      rw [hv'] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
        have h_m_neg : m.toRat < 0 := lt_of_le_of_lt h_m_le_truth h_truth_neg
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_neg h_result_neg] at h_result_abs
        apply no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg
        · linarith
        · have h_lt : (maxRep.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [h_maxR_q]; linarith) h10ze_pos
          linarith
      · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
        have h_k_cast : ((9223372036854775810 : ℕ) : ℚ) = (9223372036854775810 : ℚ) := by
          norm_num
        apply no_normalized_in_open_ulp_gap_pos_zm ze' 9223372036854775810 (by norm_num)
          (by norm_num) m h_norm h_m_pos
        · rw [h_k_cast]; linarith
        · rw [h_k_cast]
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze'
              < ((9223372036854775810 : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
    · -- v = maxRepNat+13 (zm = maxRepUp, round fired → f > 0).
      have hv' : v = (twoPow63Add12 : ℚ) := by rw [hv]; norm_num
      rw [hv'] at h_result_abs
      have h_r1 : g.round .downward = 1 := by
        have h_ne0 : g.round .downward ≠ 0 := by
          unfold Guard.round; split_ifs <;> simp_all (config := { decide := true })
        simp only [Bool.or_eq_true, Bool.and_eq_true, beq_iff_eq] at hb_true
        rcases hb_true with h | ⟨h, _⟩
        · exact h
        · exact absurd h h_ne0
      have hf_pos : 0 < f := h_sru_pos ((round_downward_eq_one_iff g).mp h_r1)
      have hzm_q_eq : (zm.toNat : ℚ) = maxRepCuspTarget := by
        rw [hzm_eq_up, h_maxRU_v]; norm_num
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
        have h_m_neg : m.toRat < 0 := lt_of_le_of_lt h_m_le_truth h_truth_neg
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_neg h_result_neg] at h_result_abs
        apply no_normalized_in_upper_cusp_gap_neg ze' m h_norm h_m_neg
        · linarith
        · have h_lt : (maxRepCuspTarget : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [hzm_q_eq]; linarith) h10ze_pos
          linarith
      · -- result sits strictly above truth — h_le refuted.
        have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (twoPow63Add12 : ℚ) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
  -- In-range: the round decision is driven by the sign bit.
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · -- Negative truth: sbit = true.
    have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
    have h_m_neg : m.toRat < 0 := lt_of_le_of_lt h_m_le_truth h_truth_neg
    rw [abs_of_neg h_truth_neg] at h_truth_abs
    rw [abs_of_neg h_result_neg] at h_result_abs
    have h_sbit : g.sbit_ = true := h_sbit_t h_truth_neg
    by_cases h_sru : g.shouldRoundUp_downward
    · -- Fired: result lands on the next grid point below.
      have hf_pos : 0 < f := h_sru_pos h_sru
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · have h_val := doRoundUp_value_downward_roundUp_noCusp g false zm ze' h_sru h_cusp
          loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_neg_m_lo : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < m.toRat := by
          have h_result_eq : result.toRat = -(((zm.toNat : ℚ) + 1) * 10 ^ ze') := by linarith
          rw [h_result_eq] at h_lt_m
          exact h_lt_m
        have h_neg_m_hi : m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := by
          have h_lt : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        exact no_normalized_in_open_ulp_gap_neg_zm ze' zm.toNat
          hzm_succ h_zm_lt m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
        have h_val := doRoundUp_value_downward_roundUp_cusp g false zm ze' h_zm_eq h_sru
          loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_neg_m_lo : -((maxRepCuspTarget : ℚ) * 10 ^ ze') < m.toRat := by
          have h_result_eq : result.toRat = -((maxRepCuspTarget : ℚ) * 10 ^ ze') := by linarith
          rw [h_result_eq] at h_lt_m
          exact h_lt_m
        have h_neg_m_hi : m.toRat < -((maxRep.toNat : ℚ) * 10 ^ ze') := by
          have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          have h_lt : (maxRep.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
            apply mul_lt_mul_of_pos_right _ h10ze_pos
            rw [← h_zm_eq_q]
            linarith
          linarith
        exact no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
    · -- Dead decision with the sign bit set: the guard is empty — exact result.
      obtain ⟨hd, hxb⟩ := content_empty_of_not_shouldRoundUp_downward g h_sbit h_sru
      have hf_zero : f = 0 := h_content_zero hd hxb
      have h_val := doRoundUp_value_downward_truncate g false zm ze'
        h_sru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      rw [hf_zero, add_zero] at h_truth_abs
      linarith
  · -- Positive truth: sbit = false — pure truncation.
    have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    rw [abs_of_nonneg h_truth_nn] at h_truth_abs
    have h_result_nn : (0 : ℚ) ≤ result.toRat := h_res_nn h_truth_pos
    rw [abs_of_nonneg h_result_nn] at h_result_abs
    have h_sbit_false : g.sbit_ = false := h_sbit_f h_truth_pos
    have h_not_sru : ¬ g.shouldRoundUp_downward := fun h =>
      Bool.noConfusion (h_sbit_false ▸ h.1)
    have h_val := doRoundUp_value_downward_truncate g false zm ze'
      h_not_sru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
    simp only at h_val
    rw [h_val] at h_result_abs
    have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
    have h_m_lo : (zm.toNat : ℚ) * 10 ^ ze' < m.toRat := by
      rw [← h_result_abs]; exact h_lt_m
    have h_m_hi : m.toRat < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
      calc m.toRat ≤ truth := h_m_le_truth
        _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
        _ < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
            apply mul_lt_mul_of_pos_right _ h10ze_pos; linarith
    exact no_normalized_in_open_ulp_gap_pos_zm ze' zm.toNat
      hzm_succ h_zm_lt m h_norm h_m_pos h_m_lo h_m_hi

/-- `.upward`, above form: with `truth ≤ result`, nothing normalized lies
in `[truth, result)`. (`.upward` needs only this side — the direction always
holds.) -/
theorem no_inbetween_above_upward_frame (result : Number) (truth : ℚ)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) (loc : String)
    (_hzm_ge : mantissaFloor ≤ zm.toNat)
    (hzm_le : zm.toNat ≤ maxRepUp.toNat)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_truth_abs : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze')
    (h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .upward loc = .ok res_pos)
    (h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (h_sru_pos : g.shouldRoundUp_upward → 0 < f)
    (h_content_zero : g.digits_ = 0 → g.xbit_ = false → f = 0)
    (hzm_succ : mantissaFloorSucc ≤ zm.toNat)
    (h_res_np : truth < 0 → result.toRat ≤ 0)
    (h_sbit_t : truth < 0 → g.sbit_ = true)
    (h_sbit_f : 0 < truth → g.sbit_ = false)
    (h_ge : truth ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (truth ≤ m.toRat) := by
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxRU : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_m_lt h_truth_le_m
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- Cusp range: maxRep < zm ≤ maxRepUp.
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, h_val, h_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .upward
      h_zm_gt_rep hzm_le loc res_pos h_rup_pos hres_pos_mant_ne
    rw [h_val] at h_result_abs
    have h_maxRU_v : maxRepUp.toNat = maxRepCuspTarget := rfl
    have h_maxR_q : (maxRep.toNat : ℚ) = maxRepNat := by rw [maxRep_val]; norm_num
    have hzm_q_lb : (9223372036854775808 : ℚ) ≤ (zm.toNat : ℚ) := by
      have h : 9223372036854775808 ≤ zm.toNat := by rw [maxRep_val] at h_zm_gt_rep; omega
      exact_mod_cast h
    have hzm_q_ub : (zm.toNat : ℚ) ≤ maxRepCuspTarget := by
      have h : zm.toNat ≤ 9223372036854775810 := by omega
      exact_mod_cast h
    rcases h_cases with ⟨hv, hzm_lt_up, _⟩ | ⟨hv, _⟩ | ⟨hv, hzm_eq_up, hb_true⟩
    · -- v = maxRep (truncate clamp).
      rw [hv] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · -- forbidden interval sits inside the cusp gap.
        have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
        have h_m_neg : m.toRat < 0 := lt_of_lt_of_le h_m_lt h_result_np
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_nonpos h_result_np] at h_result_abs
        apply no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg
        · have h_zm_le9 : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
            have h : zm.toNat ≤ 9223372036854775809 := by
              rw [h_maxRU_v] at hzm_lt_up; omega
            exact_mod_cast h
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (maxRepCuspTarget : ℚ) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · rw [h_maxR_q]; linarith
      · -- the clamp sits strictly below truth — h_ge refuted.
        have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := le_trans (le_of_lt h_truth_pos) h_ge
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_lt : (maxRepNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by linarith) h10ze_pos
        linarith
    · -- v = maxRepUp.
      have hv' : v = (maxRepCuspTarget : ℚ) := by rw [hv]; norm_num
      rw [hv'] at h_result_abs
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
        have h_m_neg : m.toRat < 0 := lt_of_lt_of_le h_m_lt h_result_np
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_nonpos h_result_np] at h_result_abs
        have h_k_cast : ((9223372036854775810 : ℕ) : ℚ) = (9223372036854775810 : ℚ) := by
          norm_num
        apply no_normalized_in_open_ulp_gap_neg_zm ze' 9223372036854775810 (by norm_num)
          (by norm_num) m h_norm h_m_neg
        · rw [h_k_cast]
          have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze'
              < ((9223372036854775810 : ℚ) + 1) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        · rw [h_k_cast]; linarith
      · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := le_trans (le_of_lt h_truth_pos) h_ge
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
        apply no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos
        · have h_lt : (maxRep.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [h_maxR_q]; linarith) h10ze_pos
          linarith
        · linarith
    · -- v = maxRepNat+13 (zm = maxRepUp, round fired → f > 0).
      have hv' : v = (twoPow63Add12 : ℚ) := by rw [hv]; norm_num
      rw [hv'] at h_result_abs
      have h_r1 : g.round .upward = 1 := by
        have h_ne0 : g.round .upward ≠ 0 := by
          unfold Guard.round; split_ifs <;> simp_all (config := { decide := true })
        simp only [Bool.or_eq_true, Bool.and_eq_true, beq_iff_eq] at hb_true
        rcases hb_true with h | ⟨h, _⟩
        · exact h
        · exact absurd h h_ne0
      have hf_pos : 0 < f := h_sru_pos ((round_upward_eq_one_iff g).mp h_r1)
      have hzm_q_eq : (zm.toNat : ℚ) = maxRepCuspTarget := by
        rw [hzm_eq_up, h_maxRU_v]; norm_num
      rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
      · -- result sits strictly below truth — h_ge refuted.
        have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
        rw [abs_of_neg h_truth_neg] at h_truth_abs
        rw [abs_of_nonpos h_result_np] at h_result_abs
        have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (twoPow63Add12 : ℚ) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right (by rw [hzm_q_eq]; linarith) h10ze_pos
        linarith
      · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
        rw [abs_of_nonneg h_truth_nn] at h_truth_abs
        have h_result_nn : (0 : ℚ) ≤ result.toRat := le_trans (le_of_lt h_truth_pos) h_ge
        rw [abs_of_nonneg h_result_nn] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
        apply no_normalized_in_upper_cusp_gap_pos ze' m h_norm h_m_pos
        · have h_lt : (maxRepCuspTarget : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by rw [hzm_q_eq]; linarith) h10ze_pos
          linarith
        · linarith
  -- In-range: the round decision is driven by the sign bit.
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · -- Negative truth: sbit = true — pure truncation.
    have h_result_np : result.toRat ≤ 0 := h_res_np h_truth_neg
    have h_m_neg : m.toRat < 0 := lt_of_lt_of_le h_m_lt h_result_np
    rw [abs_of_neg h_truth_neg] at h_truth_abs
    rw [abs_of_nonpos h_result_np] at h_result_abs
    have h_sbit : g.sbit_ = true := h_sbit_t h_truth_neg
    have h_not_sru : ¬ g.shouldRoundUp_upward := fun h =>
      Bool.noConfusion (h_sbit ▸ h.1)
    have h_val := doRoundUp_value_upward_truncate g false zm ze'
      h_not_sru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
    simp only at h_val
    rw [h_val] at h_result_abs
    have h_neg_m_lo : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < m.toRat := by
      have h_lt : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
        mul_lt_mul_of_pos_right (by linarith) h10ze_pos
      linarith
    have h_neg_m_hi : m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := by
      have h_result_eq : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by linarith
      rw [h_result_eq] at h_m_lt
      exact h_m_lt
    exact no_normalized_in_open_ulp_gap_neg_zm ze' zm.toNat
      hzm_succ h_zm_lt m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
  · -- Positive truth: sbit = false.
    have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    rw [abs_of_nonneg h_truth_nn] at h_truth_abs
    have h_result_nn : (0 : ℚ) ≤ result.toRat := le_trans (le_of_lt h_truth_pos) h_ge
    rw [abs_of_nonneg h_result_nn] at h_result_abs
    have h_sbit_false : g.sbit_ = false := h_sbit_f h_truth_pos
    by_cases h_sru : g.shouldRoundUp_upward
    · -- Fired: result lands on the next grid point above.
      have hf_pos : 0 < f := h_sru_pos h_sru
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · have h_val := doRoundUp_value_upward_roundUp_noCusp g false zm ze' h_sru h_cusp
          loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
        have h_m_lo : (zm.toNat : ℚ) * 10 ^ ze' < m.toRat := by
          have h_lt : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right (by linarith) h10ze_pos
          linarith
        have h_m_hi : m.toRat < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
          rw [← h_result_abs]; exact h_m_lt
        exact no_normalized_in_open_ulp_gap_pos_zm ze' zm.toNat
          hzm_succ h_zm_lt m h_norm h_m_pos h_m_lo h_m_hi
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
        have h_val := doRoundUp_value_upward_roundUp_cusp g false zm ze' h_zm_eq h_sru
          loc res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
        apply no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos
        · have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          have h_lt : (maxRep.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
            apply mul_lt_mul_of_pos_right _ h10ze_pos
            rw [← h_zm_eq_q]
            linarith
          linarith
        · linarith
    · -- Dead decision with the sign bit clear: the guard is empty — exact result.
      obtain ⟨hd, hxb⟩ := content_empty_of_not_shouldRoundUp_upward g h_sbit_false h_sru
      have hf_zero : f = 0 := h_content_zero hd hxb
      have h_val := doRoundUp_value_upward_truncate g false zm ze'
        h_sru h_zm_le_rep loc res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      rw [hf_zero, add_zero] at h_truth_abs
      linarith

end XRPL.Model.Protocol

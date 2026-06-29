import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.ProofTactics
import XRPL.Properties.Approx
import XRPL.Properties.Protocol.Number.Add.Common.Downward.BoundProof
import XRPL.Properties.Protocol.Number.Add.Common.Downward.WitnessTrace
import XRPL.Properties.Protocol.Number.Add.Common.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Add.Common.Upward.WitnessTrace
import XRPL.Properties.Protocol.Number.Add.Common.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Add.Common.TowardsZero.WitnessTrace
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.WitnessTrace


namespace XRPL.Model.Protocol

set_option maxHeartbeats 1600000 in
-- Two-sign × in-range/cusp leaf navigation with nlinarith over 2^63-scale literals.
/-- `RoundsWithin`-shaped restatement of `operator_add_rounding_bound_same_sign_downward`,
for **both** operand signs.

For non-negative operands the guard's sign bit is clear, so no round-up ever
fires and the algorithm truncates the magnitude (the cusp range clamps to
`maxRep`, also below the truth). For negative operands the sign bit is set, so
the round decision fires exactly when the guard has content (`f > 0`),
rounding the magnitude up — which is downward in value; when it does not fire
the guard content is empty (`f = 0`) and the result is exact. -/
theorem operator_add_rounds_same_sign_downward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .downward (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_sbit, _, _, _, _⟩ :=
    operator_add_algorithmic_facts_same_sign_downward x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_approx : (RatValued.toRat result : ℚ) = result.toRat := rfl
  have h_result_abs_via_self :
      (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
        = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    have habs := abs_toRat_eq result
    rw [h_result_abs] at habs
    exact habs.symm
  have hgP_sbit : (g.pushOverflow zm .downward).sbit_ = g.sbit_ := by
    simp only [Guard.pushOverflow, Guard.push]
    split_ifs <;> rfl
  have h_eps_nn : (0 : ℚ) ≤ 10 / ((2 ^ 63 + 2 : ℚ)) := by norm_num
  by_cases h_pos : x.negative_ = false
  · -- ===== non-negative operands =====
    have hy_pos : y.negative_ = false := h_same_sign ▸ h_pos
    have h_result_pos : result.negative_ = false := h_sign.trans h_pos
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x h_pos
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy_pos
    have h_truth_nn : 0 ≤ x.toRat + y.toRat := by linarith
    have h_abs_truth : |x.toRat + y.toRat| = x.toRat + y.toRat := abs_of_nonneg h_truth_nn
    have h_result_eq_abs : result.toRat = |result.toRat| := by
      have h_result_nn : 0 ≤ result.toRat := Number.toRat_nonneg_of_nonnegative result h_result_pos
      rw [abs_of_nonneg h_result_nn]
    -- Truth via positive form.
    have h_xy_signed : x.toRat + y.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
      have := habs_xy_eq; rw [h_abs_truth] at this; exact this
    have h_result_signed : result.toRat = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [h_result_eq_abs]; exact h_result_abs
    -- For positive operands, g.sbit_ = false.
    have h_g_sbit_false : g.sbit_ = false := h_sbit.trans h_pos
    -- The downward round decision never fires when sbit is clear.
    have h_bool_false : ∀ g' : Guard, g'.sbit_ = false →
        (((g'.round .downward == 1) || ((g'.round .downward == 0) && (zm % 2 == 1))) = false) := by
      intro g' hsb
      have hr_ne1 : g'.round .downward ≠ 1 := by
        intro h1
        have hsru := (round_downward_eq_one_iff g').mp h1
        have h := hsru.1
        rw [hsb] at h
        exact Bool.noConfusion h
      have hr_ne0 : g'.round .downward ≠ 0 := by
        unfold Guard.round
        split_ifs <;> decide
      rw [show (g'.round .downward == 1) = false from beq_eq_false_iff_ne.mpr hr_ne1,
          show (g'.round .downward == 0) = false from beq_eq_false_iff_ne.mpr hr_ne0,
          Bool.false_and]
      rfl
    have h_no_sru : ¬ g.shouldRoundUp_downward := by
      intro h
      have : g.sbit_ = true := h.1
      rw [h_g_sbit_false] at this; exact Bool.noConfusion this
    by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
    · -- In-range: doRoundUp truncates.
      have h_tr_val := doRoundUp_value_downward_truncate g false zm ze' h_no_sru h_zm_le_rep
        "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_tr_val
      refine ⟨?_, ?_⟩
      · -- Direction
        rw [h_approx, h_result_signed, h_xy_signed, h_tr_val]
        nlinarith [h10ze'_nn, hf_nn]
      · -- Magnitude
        rw [h_approx, h_abs_truth, h_result_signed, h_xy_signed, h_tr_val]
        have h_diff : ((zm.toNat : ℚ) + f) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
            = f * 10 ^ ze' := by ring
        rw [h_diff]
        have h_inner : f ≤ (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_denom_val]
          rw [show (((zm.toNat : ℚ) + f)) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          have h10zm : 9223372036854775800 ≤ 10 * (zm.toNat : ℚ) := by linarith
          have hf_bound : f * 9223372036854775800 ≤ 9223372036854775800 := by
            nlinarith [hf_lt1]
          linarith
        calc f * 10 ^ ze'
            ≤ (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
              mul_le_mul_of_nonneg_right h_inner h10ze'_nn
          _ = (((zm.toNat : ℚ) + f)) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
    · -- Cusp range: maxRep < zm ≤ maxRepUp; truncate clamps stay below the truth.
      push_neg at h_zm_le_rep
      obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .downward
        h_zm_le_rep hzm_le_maxRep "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
      obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_maxRep
      rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
      · -- v = maxRepNat: clamp to maxRep, below the truth.
        subst hv
        refine ⟨?_, ?_⟩
        · rw [h_approx, h_result_signed, h_xy_signed, hv_val]
          exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze'_nn
        · rw [h_approx, h_abs_truth, h_result_signed, h_xy_signed, hv_val]
          rw [show ((zm.toNat : ℚ) + f) * 10 ^ ze' - (maxRepNat : ℚ) * 10 ^ ze'
                = (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze' from by ring]
          have h_inner : ((zm.toNat : ℚ) + f) - maxRepNat
              ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
            rw [h_denom_val, show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
                le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hzm_q_gt, hzm_q_le3, hf_nn, hf_lt1]
          calc (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze'
              ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
                mul_le_mul_of_nonneg_right h_inner h10ze'_nn
            _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
      · -- v = maxRepNat + 3: the round decision is dead (sbit clear), so zm = maxRepUp.
        subst hv
        have hzm_eq : zm.toNat = maxRepUp.toNat := by
          rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
          · exact h
          · rw [h_bool_false (g.pushOverflow zm .downward) (by rw [hgP_sbit]; exact h_g_sbit_false)] at h
            exact absurd h Bool.noConfusion
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
          rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
        refine ⟨?_, ?_⟩
        · rw [h_approx, h_result_signed, h_xy_signed, hv_val, hzm_q_eq]
          exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze'_nn
        · rw [h_approx, h_abs_truth, h_result_signed, h_xy_signed, hv_val, hzm_q_eq]
          rw [show (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze' - ((maxRepNat : ℚ) + 3) * 10 ^ ze'
                = f * 10 ^ ze' from by ring]
          have h_inner : f ≤ (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
            rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
                le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hf_nn, hf_lt1]
          calc f * 10 ^ ze'
              ≤ (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
                mul_le_mul_of_nonneg_right h_inner h10ze'_nn
            _ = (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
      · -- v = maxRepNat + 13: requires the round decision, dead when sbit is clear.
        rw [h_bool_false g h_g_sbit_false] at hfire
        exact absurd hfire Bool.noConfusion
  · -- ===== negative operands: the magnitude rounds up, the value rounds down =====
    have h_neg : x.negative_ = true := by
      cases hxn : x.negative_
      · exact absurd hxn h_pos
      · rfl
    have hy_neg : y.negative_ = true := h_same_sign ▸ h_neg
    have h_result_neg : result.negative_ = true := h_sign.trans h_neg
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x h_neg
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy_neg
    have h_truth_np : x.toRat + y.toRat ≤ 0 := by linarith
    have h_abs_truth : |x.toRat + y.toRat| = -(x.toRat + y.toRat) := abs_of_nonpos h_truth_np
    have h_truth_signed : x.toRat + y.toRat = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
      have h1 := habs_xy_eq; rw [h_abs_truth] at h1; linarith
    have h_result_signed : result.toRat
        = -((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_) := by
      have h2 := Number.toRat_of_neg result h_result_neg
      rw [h2, h_result_abs_via_self]
    have h_g_sbit_true : g.sbit_ = true := h_sbit.trans h_neg
    -- Rewriting the goal into magnitude form.
    have h_truth_abs_eq : |x.toRat + y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_xy_eq
    by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
    · -- ===== in range: zm ≤ maxRep =====
      by_cases h_sru : g.shouldRoundUp_downward
      · -- round-up fires: f > 0 and the output magnitude exceeds the truth's.
        have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_downward g f hf_rep h_sru
        by_cases h_cusp : zm = maxRep
        · -- E1 cusp: zm = maxRep, output magnitude maxRepCuspTarget = maxRepNat + 3.
          have h_cusp_val := doRoundUp_value_downward_roundUp_cusp g false zm ze' h_cusp h_sru
            "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_cusp_val
          have hzm_eq_maxRep_q : (zm.toNat : ℚ) = maxRepNat := by
            rw [show zm.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]; norm_num
          have h_result_val : result.toRat = -((maxRepCuspTarget : ℚ) * 10 ^ ze') := by
            rw [h_result_signed, h_cusp_val]
          refine ⟨?_, ?_⟩
          · rw [h_approx, h_result_val, h_truth_signed, hzm_eq_maxRep_q]
            have h_inner : ((maxRepNat : ℚ) + f) ≤ maxRepCuspTarget := by
              rw [show (maxRepCuspTarget : ℚ) = maxRepNat + 3 from by norm_num]
              linarith [hf_lt1]
            nlinarith [h10ze'_nn, h_inner]
          · rw [h_approx, h_abs_truth, h_result_val, h_truth_signed, hzm_eq_maxRep_q]
            rw [show -(((maxRepNat : ℚ) + f) * 10 ^ ze') - -((maxRepCuspTarget : ℚ) * 10 ^ ze')
                  = ((maxRepCuspTarget : ℚ) - (maxRepNat + f)) * 10 ^ ze' from by ring,
                show -(-(((maxRepNat : ℚ) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                  = (((maxRepNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_le_mul_of_nonneg_right _ h10ze'_nn
            rw [h_denom_val, show ((maxRepNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * ((maxRepNat : ℚ) + f) / maxRepCuspTarget from by ring,
                le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hf_nn, hf_lt1]
        · -- no cusp: zm < maxRep, output magnitude zm + 1.
          have hzm_lt_maxRep : zm.toNat < maxRep.toNat := by
            have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
            omega
          have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by omega
          have h_nc_val := doRoundUp_value_downward_roundUp_noCusp g false zm ze' h_sru h_no_cusp
            "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_nc_val
          have h_result_val : result.toRat = -(((zm.toNat : ℚ) + 1) * 10 ^ ze') := by
            rw [h_result_signed, h_nc_val]
          refine ⟨?_, ?_⟩
          · rw [h_approx, h_result_val, h_truth_signed]
            have h_inner : ((zm.toNat : ℚ) + f) ≤ ((zm.toNat : ℚ) + 1) := by linarith
            nlinarith [h10ze'_nn, h_inner]
          · rw [h_approx, h_abs_truth, h_result_val, h_truth_signed]
            rw [show -(((zm.toNat : ℚ) + f) * 10 ^ ze') - -(((zm.toNat : ℚ) + 1) * 10 ^ ze')
                  = (1 - f) * 10 ^ ze' from by ring,
                show -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                  = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_le_mul_of_nonneg_right _ h10ze'_nn
            by_cases h_floor : zm.toNat = mantissaFloor
            · have hf_ge : (8 : ℚ) / 10 ≤ f := h_floor_constraint h_floor
              have hzm_eq_floor_q : (zm.toNat : ℚ) = mantissaFloor := by
                rw [h_floor]; norm_num
              rw [hzm_eq_floor_q, h_denom_val,
                  show ((mantissaFloor : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                    = 10 * (mantissaFloor + f) / maxRepCuspTarget from by ring,
                  le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
              nlinarith [hf_ge, hf_lt1]
            · have h_zm_gt : mantissaFloorSucc ≤ zm.toNat := by omega
              have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_gt
              rw [h_denom_val,
                  show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                    = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
                  le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
              nlinarith [hzm_q_gt, hf_nn, hf_lt1, hf_pos]
      · -- no round-up: sbit is set, so the guard content is empty and f = 0; exact.
        obtain ⟨h_dig0, h_xbit0⟩ :=
          content_empty_of_not_shouldRoundUp_downward g h_g_sbit_true h_sru
        have hf_zero : f = 0 :=
          represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep
        have h_tr_val := doRoundUp_value_downward_truncate g false zm ze' h_sru h_zm_le_rep
          "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_tr_val
        have h_result_val : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by
          rw [h_result_signed, h_tr_val]
        have h_truth_eq_result : x.toRat + y.toRat = result.toRat := by
          rw [h_result_val, h_truth_signed, hf_zero]; ring
        refine ⟨le_of_eq h_truth_eq_result.symm, ?_⟩
        rw [h_approx, ← h_truth_eq_result, show (x.toRat + y.toRat) - (x.toRat + y.toRat) = 0 from by ring]
        exact mul_nonneg (abs_nonneg _) h_eps_nn
    · -- ===== cusp range: maxRep < zm ≤ maxRepUp =====
      push_neg at h_zm_le_rep
      obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .downward
        h_zm_le_rep hzm_le_maxRep "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
      obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_maxRep
      have h_result_val : result.toRat = -(v * 10 ^ ze') := by
        rw [h_result_signed, hv_val]
      rcases hv_cases with ⟨hv, hzm_lt_up, hbool⟩ | ⟨hv, hcoup⟩ | ⟨hv, hzm_eq, _⟩
      · -- v = maxRepNat: impossible for negatives (the pushed-guard decision fires).
        exfalso
        obtain ⟨hdig_pos, hsb⟩ := pushOverflow_cusp_interior_facts g zm .downward h_zm_le_rep hzm_lt_up
        have h1 : (g.pushOverflow zm .downward).round .downward = 1 :=
          (round_downward_eq_one_iff _).mpr ⟨by rw [hsb]; exact h_g_sbit_true, Or.inl hdig_pos⟩
        rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hbool
        exact Bool.noConfusion hbool
      · -- v = maxRepNat + 3: either exact (zm = maxRepUp, dead decision ⇒ f = 0)
        -- or the fired cusp-interior clamp (zm < maxRepUp ⇒ magnitude grows).
        subst hv
        rcases hcoup with ⟨hzm_eq, hdead⟩ | ⟨hzm_lt_up, _⟩
        · -- zm = maxRepUp with a dead round decision: f = 0 and the result is exact.
          have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
            rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
          have hf_zero : f = 0 := by
            rcases hdead with hb1 | hb2
            · obtain ⟨h_dig0, h_xbit0⟩ :=
                roundUp_bool_downward_false_content g zm h_g_sbit_true hb1
              exact represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep
            · have h_push_sbit : (g.push (maxRepUp % 10)).sbit_ = g.sbit_ := rfl
              obtain ⟨h_dig0', h_xbit0'⟩ :=
                roundUp_bool_downward_false_content (g.push (maxRepUp % 10)) (maxRepUp / 10)
                  (h_push_sbit.trans h_g_sbit_true) hb2
              obtain ⟨h_dig0, h_xbit0⟩ := push_content_empty g (maxRepUp % 10) h_dig0' h_xbit0'
              exact represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep
          have h_truth_eq_result : x.toRat + y.toRat = result.toRat := by
            rw [h_result_val, h_truth_signed, hf_zero, hzm_q_eq]; ring
          refine ⟨le_of_eq h_truth_eq_result.symm, ?_⟩
          rw [h_approx, ← h_truth_eq_result,
              show (x.toRat + y.toRat) - (x.toRat + y.toRat) = 0 from by ring]
          exact mul_nonneg (abs_nonneg _) h_eps_nn
        · -- fired clamp at the cusp interior: magnitude grows to maxRepNat + 3.
          have hzm_q_lt3 : (zm.toNat : ℚ) ≤ maxRepNat + 2 := by
            have h2 : zm.toNat ≤ 9223372036854775809 := by
              rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hzm_lt_up
              omega
            calc (zm.toNat : ℚ) ≤ ((9223372036854775809 : ℕ) : ℚ) := by exact_mod_cast h2
              _ = maxRepNat + 2 := by norm_num
          refine ⟨?_, ?_⟩
          · rw [h_approx, h_result_val, h_truth_signed]
            have h_inner : ((zm.toNat : ℚ) + f) ≤ maxRepNat + 3 := by
              linarith [hf_lt1, hzm_q_lt3]
            nlinarith [h10ze'_nn, h_inner]
          · rw [h_approx, h_abs_truth, h_result_val, h_truth_signed]
            rw [show -(((zm.toNat : ℚ) + f) * 10 ^ ze') - -(((maxRepNat : ℚ) + 3) * 10 ^ ze')
                  = (((maxRepNat : ℚ) + 3) - ((zm.toNat : ℚ) + f)) * 10 ^ ze' from by ring,
                show -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                  = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_le_mul_of_nonneg_right _ h10ze'_nn
            rw [h_denom_val, show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
                le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hzm_q_gt, hf_nn]
      · -- v = maxRepNat + 13: zm = maxRepUp, the magnitude grows by 10 - f.
        subst hv
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
          rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
        refine ⟨?_, ?_⟩
        · rw [h_approx, h_result_val, h_truth_signed, hzm_q_eq]
          have h_inner : (((maxRepNat : ℚ) + 3) + f) ≤ maxRepNat + 13 := by
            linarith [hf_lt1]
          nlinarith [h10ze'_nn, h_inner]
        · rw [h_approx, h_abs_truth, h_result_val, h_truth_signed, hzm_q_eq]
          rw [show -((((maxRepNat : ℚ) + 3) + f) * 10 ^ ze') - -(((maxRepNat : ℚ) + 13) * 10 ^ ze')
                = (10 - f) * 10 ^ ze' from by ring,
              show -(-((((maxRepNat : ℚ) + 3) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                = ((((maxRepNat : ℚ) + 3) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
          apply mul_le_mul_of_nonneg_right _ h10ze'_nn
          rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
              le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hf_nn]

/-- `RoundsWithin`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_downward_tight`. Covers both same-sign and diff-sign
branches with the uniform magnitude scale `11/(2^63 - 18)`, **unconditionally**:
the same-sign direction is `operator_add_rounds_same_sign_downward` (both
operand signs), and the diff-sign direction is
`operator_add_rounding_bound_diff_sign_downward_dir` (via the recover-loop
digit-exactness fact `0 ≤ δ` and the `doNormalize128` direction keystone). -/
theorem operator_add_rounds_downward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .downward (11 / (2 ^ 63 - 18 : ℚ)) := by
  have h_dir : result.toRat ≤ x.toRat + y.toRat := by
    by_cases h_sign : x.negative_ = y.negative_
    · exact (operator_add_rounds_same_sign_downward_proof x y result hx hy hx_mant_ne hy_mant_ne
        h_sign h_not_zero hok).1
    · exact operator_add_rounding_bound_diff_sign_downward_dir x y result hx hy
        hx_mant_ne hy_mant_ne h_sign h_not_zero hok hresult
  refine ⟨h_dir, ?_⟩
  have h_bound := operator_add_rounding_bound_downward_tight x y result hx hy
    hx_mant_ne hy_mant_ne h_not_zero hok hresult
  have h_abs_eq : |result.toRat - (x.toRat + y.toRat)|
      = (x.toRat + y.toRat) - result.toRat := by
    rw [show result.toRat - (x.toRat + y.toRat)
        = -((x.toRat + y.toRat) - result.toRat) from by ring]
    rw [abs_neg, abs_of_nonneg (by linarith)]
  rw [h_abs_eq] at h_bound
  rw [show (RatValued.toRat result : ℚ) = result.toRat from rfl]
  linarith

set_option maxHeartbeats 1600000 in
-- Two-sign × in-range/cusp leaf navigation with nlinarith over 2^63-scale literals.
/-- `RoundsWithin`-shaped restatement of `operator_add_rounding_bound_same_sign_upward`,
for **both** operand signs.

For non-negative operands the guard's sign bit is clear, so the round decision
fires exactly when the guard has content (`f > 0`), rounding the magnitude — and
hence the value — up; when it does not fire the guard content is empty (`f = 0`)
and the result is exact. For negative operands the sign bit is set, so no
round-up ever fires and the algorithm truncates the magnitude (the cusp range
clamps to `maxRep`, also at or below the truth's magnitude), which is upward in
value. -/
theorem operator_add_rounds_same_sign_upward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_sbit, _, _, _, _⟩ :=
    operator_add_algorithmic_facts_same_sign_upward x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_approx : (RatValued.toRat result : ℚ) = result.toRat := rfl
  have h_result_abs_via_self :
      (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
        = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    have habs := abs_toRat_eq result
    rw [h_result_abs] at habs
    exact habs.symm
  have hgP_sbit : (g.pushOverflow zm .upward).sbit_ = g.sbit_ := by
    simp only [Guard.pushOverflow, Guard.push]
    split_ifs <;> rfl
  have h_eps_nn : (0 : ℚ) ≤ 10 / ((2 ^ 63 + 2 : ℚ)) := by norm_num
  by_cases h_pos : x.negative_ = false
  · -- ===== non-negative operands: the magnitude rounds up, the value rounds up =====
    have hy_pos : y.negative_ = false := h_same_sign ▸ h_pos
    have h_result_pos : result.negative_ = false := h_sign.trans h_pos
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x h_pos
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy_pos
    have h_truth_nn : 0 ≤ x.toRat + y.toRat := by linarith
    have h_abs_truth : |x.toRat + y.toRat| = x.toRat + y.toRat := abs_of_nonneg h_truth_nn
    have h_xy_signed : x.toRat + y.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
      have := habs_xy_eq; rw [h_abs_truth] at this; exact this
    have h_result_signed : result.toRat = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      have h2 := Number.toRat_of_nonneg result h_result_pos
      rw [h2, h_result_abs_via_self]
    have h_g_sbit_false : g.sbit_ = false := h_sbit.trans h_pos
    by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
    · -- ===== in range: zm ≤ maxRep =====
      by_cases h_sru : g.shouldRoundUp_upward
      · -- round-up fires: f > 0 and the output magnitude exceeds the truth's.
        have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_upward g f hf_rep h_sru
        by_cases h_cusp : zm = maxRep
        · -- E1 cusp: zm = maxRep, output magnitude maxRepCuspTarget = maxRepNat + 3.
          have h_cusp_val := doRoundUp_value_upward_roundUp_cusp g false zm ze' h_cusp h_sru
            "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_cusp_val
          have hzm_eq_maxRep_q : (zm.toNat : ℚ) = maxRepNat := by
            rw [show zm.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]; norm_num
          have h_result_val : result.toRat = (maxRepCuspTarget : ℚ) * 10 ^ ze' := by
            rw [h_result_signed, h_cusp_val]
          refine ⟨?_, ?_⟩
          · rw [h_approx, h_result_val, h_xy_signed, hzm_eq_maxRep_q]
            have h_inner : ((maxRepNat : ℚ) + f) ≤ maxRepCuspTarget := by
              rw [show (maxRepCuspTarget : ℚ) = maxRepNat + 3 from by norm_num]
              linarith [hf_lt1]
            exact mul_le_mul_of_nonneg_right h_inner h10ze'_nn
          · rw [h_approx, h_abs_truth, h_result_val, h_xy_signed, hzm_eq_maxRep_q]
            rw [show (maxRepCuspTarget : ℚ) * 10 ^ ze' - ((maxRepNat : ℚ) + f) * 10 ^ ze'
                  = ((maxRepCuspTarget : ℚ) - (maxRepNat + f)) * 10 ^ ze' from by ring,
                show ((maxRepNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ)))
                  = (((maxRepNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_le_mul_of_nonneg_right _ h10ze'_nn
            rw [h_denom_val, show ((maxRepNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * ((maxRepNat : ℚ) + f) / maxRepCuspTarget from by ring,
                le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hf_nn, hf_lt1]
        · -- no cusp: zm < maxRep, output magnitude zm + 1.
          have hzm_lt_maxRep : zm.toNat < maxRep.toNat := by
            have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
            omega
          have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by omega
          have h_nc_val := doRoundUp_value_upward_roundUp_noCusp g false zm ze' h_sru h_no_cusp
            "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_nc_val
          have h_result_val : result.toRat = ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
            rw [h_result_signed, h_nc_val]
          refine ⟨?_, ?_⟩
          · rw [h_approx, h_result_val, h_xy_signed]
            exact mul_le_mul_of_nonneg_right (by linarith [hf_lt1]) h10ze'_nn
          · rw [h_approx, h_abs_truth, h_result_val, h_xy_signed]
            rw [show ((zm.toNat : ℚ) + 1) * 10 ^ ze' - ((zm.toNat : ℚ) + f) * 10 ^ ze'
                  = (1 - f) * 10 ^ ze' from by ring,
                show ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ)))
                  = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_le_mul_of_nonneg_right _ h10ze'_nn
            by_cases h_floor : zm.toNat = mantissaFloor
            · have hf_ge : (8 : ℚ) / 10 ≤ f := h_floor_constraint h_floor
              have hzm_eq_floor_q : (zm.toNat : ℚ) = mantissaFloor := by
                rw [h_floor]; norm_num
              rw [hzm_eq_floor_q, h_denom_val,
                  show ((mantissaFloor : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                    = 10 * (mantissaFloor + f) / maxRepCuspTarget from by ring,
                  le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
              nlinarith [hf_ge, hf_lt1]
            · have h_zm_gt : mantissaFloorSucc ≤ zm.toNat := by omega
              have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_gt
              rw [h_denom_val,
                  show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                    = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
                  le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
              nlinarith [hzm_q_gt, hf_nn, hf_lt1, hf_pos]
      · -- no round-up: sbit is clear, so the guard content is empty and f = 0; exact.
        obtain ⟨h_dig0, h_xbit0⟩ :=
          content_empty_of_not_shouldRoundUp_upward g h_g_sbit_false h_sru
        have hf_zero : f = 0 :=
          represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep
        have h_tr_val := doRoundUp_value_upward_truncate g false zm ze' h_sru h_zm_le_rep
          "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_tr_val
        have h_result_val : result.toRat = (zm.toNat : ℚ) * 10 ^ ze' := by
          rw [h_result_signed, h_tr_val]
        have h_truth_eq_result : x.toRat + y.toRat = result.toRat := by
          rw [h_result_val, h_xy_signed, hf_zero]; ring
        refine ⟨le_of_eq h_truth_eq_result, ?_⟩
        rw [h_approx, ← h_truth_eq_result,
            show (x.toRat + y.toRat) - (x.toRat + y.toRat) = 0 from by ring]
        exact mul_nonneg (abs_nonneg _) h_eps_nn
    · -- ===== cusp range: maxRep < zm ≤ maxRepUp =====
      push_neg at h_zm_le_rep
      obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .upward
        h_zm_le_rep hzm_le_maxRep "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
      obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_maxRep
      have h_result_val : result.toRat = v * 10 ^ ze' := by
        rw [h_result_signed, hv_val]
      rcases hv_cases with ⟨hv, hzm_lt_up, hbool⟩ | ⟨hv, hcoup⟩ | ⟨hv, hzm_eq, _⟩
      · -- v = maxRepNat: impossible for non-negatives (the pushed-guard decision fires).
        exfalso
        obtain ⟨hdig_pos, hsb⟩ := pushOverflow_cusp_interior_facts g zm .upward h_zm_le_rep hzm_lt_up
        have h1 : (g.pushOverflow zm .upward).round .upward = 1 :=
          (round_upward_eq_one_iff _).mpr ⟨by rw [hsb]; exact h_g_sbit_false, Or.inl hdig_pos⟩
        rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hbool
        exact Bool.noConfusion hbool
      · -- v = maxRepNat + 3: either exact (zm = maxRepUp, dead decision ⇒ f = 0)
        -- or the fired cusp-interior clamp (zm < maxRepUp ⇒ magnitude grows).
        subst hv
        rcases hcoup with ⟨hzm_eq, hdead⟩ | ⟨hzm_lt_up, _⟩
        · -- zm = maxRepUp with a dead round decision: f = 0 and the result is exact.
          have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
            rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
          have hf_zero : f = 0 := by
            rcases hdead with hb1 | hb2
            · obtain ⟨h_dig0, h_xbit0⟩ :=
                roundUp_bool_upward_false_content g zm h_g_sbit_false hb1
              exact represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep
            · have h_push_sbit : (g.push (maxRepUp % 10)).sbit_ = g.sbit_ := rfl
              obtain ⟨h_dig0', h_xbit0'⟩ :=
                roundUp_bool_upward_false_content (g.push (maxRepUp % 10)) (maxRepUp / 10)
                  (h_push_sbit.trans h_g_sbit_false) hb2
              obtain ⟨h_dig0, h_xbit0⟩ := push_content_empty g (maxRepUp % 10) h_dig0' h_xbit0'
              exact represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep
          have h_truth_eq_result : x.toRat + y.toRat = result.toRat := by
            rw [h_result_val, h_xy_signed, hf_zero, hzm_q_eq]; ring
          refine ⟨le_of_eq h_truth_eq_result, ?_⟩
          rw [h_approx, ← h_truth_eq_result,
              show (x.toRat + y.toRat) - (x.toRat + y.toRat) = 0 from by ring]
          exact mul_nonneg (abs_nonneg _) h_eps_nn
        · -- fired clamp at the cusp interior: magnitude grows to maxRepNat + 3.
          have hzm_q_lt3 : (zm.toNat : ℚ) ≤ maxRepNat + 2 := by
            have h2 : zm.toNat ≤ 9223372036854775809 := by
              rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hzm_lt_up
              omega
            calc (zm.toNat : ℚ) ≤ ((9223372036854775809 : ℕ) : ℚ) := by exact_mod_cast h2
              _ = maxRepNat + 2 := by norm_num
          refine ⟨?_, ?_⟩
          · rw [h_approx, h_result_val, h_xy_signed]
            exact mul_le_mul_of_nonneg_right (by linarith [hf_lt1, hzm_q_lt3]) h10ze'_nn
          · rw [h_approx, h_abs_truth, h_result_val, h_xy_signed]
            rw [show ((maxRepNat : ℚ) + 3) * 10 ^ ze' - ((zm.toNat : ℚ) + f) * 10 ^ ze'
                  = (((maxRepNat : ℚ) + 3) - ((zm.toNat : ℚ) + f)) * 10 ^ ze' from by ring,
                show ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ)))
                  = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_le_mul_of_nonneg_right _ h10ze'_nn
            rw [h_denom_val, show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
                le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hzm_q_gt, hf_nn]
      · -- v = maxRepNat + 13: zm = maxRepUp, the magnitude grows by 10 - f.
        subst hv
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
          rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
        refine ⟨?_, ?_⟩
        · rw [h_approx, h_result_val, h_xy_signed, hzm_q_eq]
          exact mul_le_mul_of_nonneg_right (by linarith [hf_lt1]) h10ze'_nn
        · rw [h_approx, h_abs_truth, h_result_val, h_xy_signed, hzm_q_eq]
          rw [show ((maxRepNat : ℚ) + 13) * 10 ^ ze' - (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze'
                = (10 - f) * 10 ^ ze' from by ring,
              show (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ)))
                = ((((maxRepNat : ℚ) + 3) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
          apply mul_le_mul_of_nonneg_right _ h10ze'_nn
          rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
              le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hf_nn]
  · -- ===== negative operands: the magnitude truncates, the value rounds up =====
    have h_neg : x.negative_ = true := by
      cases hxn : x.negative_
      · exact absurd hxn h_pos
      · rfl
    have hy_neg : y.negative_ = true := h_same_sign ▸ h_neg
    have h_result_neg : result.negative_ = true := h_sign.trans h_neg
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x h_neg
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy_neg
    have h_truth_np : x.toRat + y.toRat ≤ 0 := by linarith
    have h_abs_truth : |x.toRat + y.toRat| = -(x.toRat + y.toRat) := abs_of_nonpos h_truth_np
    have h_truth_signed : x.toRat + y.toRat = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
      have h1 := habs_xy_eq; rw [h_abs_truth] at h1; linarith
    have h_result_signed : result.toRat
        = -((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_) := by
      have h2 := Number.toRat_of_neg result h_result_neg
      rw [h2, h_result_abs_via_self]
    have h_g_sbit_true : g.sbit_ = true := h_sbit.trans h_neg
    -- The upward round decision never fires when sbit is set.
    have h_bool_false : ∀ (g' : Guard) (m : UInt64), g'.sbit_ = true →
        (((g'.round .upward == 1) || ((g'.round .upward == 0) && (m % 2 == 1))) = false) := by
      intro g' m hsb
      have hr_ne1 : g'.round .upward ≠ 1 := by
        intro h1
        have hsru := (round_upward_eq_one_iff g').mp h1
        have h := hsru.1
        rw [hsb] at h
        exact Bool.noConfusion h
      have hr_ne0 : g'.round .upward ≠ 0 := by
        unfold Guard.round
        split_ifs <;> decide
      rw [show (g'.round .upward == 1) = false from beq_eq_false_iff_ne.mpr hr_ne1,
          show (g'.round .upward == 0) = false from beq_eq_false_iff_ne.mpr hr_ne0,
          Bool.false_and]
      rfl
    have h_no_sru : ¬ g.shouldRoundUp_upward := by
      intro h
      have : g.sbit_ = false := h.1
      rw [h_g_sbit_true] at this; exact Bool.noConfusion this
    by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
    · -- In-range: doRoundUp truncates; -(zm·10^ze') sits at or above the truth.
      have h_tr_val := doRoundUp_value_upward_truncate g false zm ze' h_no_sru h_zm_le_rep
        "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_tr_val
      have h_result_val : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by
        rw [h_result_signed, h_tr_val]
      refine ⟨?_, ?_⟩
      · -- Direction
        rw [h_approx, h_result_val, h_truth_signed]
        have h_inner : (zm.toNat : ℚ) ≤ (zm.toNat : ℚ) + f := by linarith
        nlinarith [h10ze'_nn, h_inner]
      · -- Magnitude
        rw [h_approx, h_abs_truth, h_result_val, h_truth_signed]
        rw [show -((zm.toNat : ℚ) * 10 ^ ze') - -(((zm.toNat : ℚ) + f) * 10 ^ ze')
              = f * 10 ^ ze' from by ring,
            show -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
              = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
        apply mul_le_mul_of_nonneg_right _ h10ze'_nn
        rw [h_denom_val,
            show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
              = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
            le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
        have h10zm : 9223372036854775800 ≤ 10 * (zm.toNat : ℚ) := by linarith
        nlinarith [hf_lt1, hf_nn]
    · -- Cusp range: maxRep < zm ≤ maxRepUp; truncate clamps stay at or below the
      -- truth's magnitude, hence at or above the truth.
      push_neg at h_zm_le_rep
      obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .upward
        h_zm_le_rep hzm_le_maxRep "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
      obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_maxRep
      have h_result_val : result.toRat = -(v * 10 ^ ze') := by
        rw [h_result_signed, hv_val]
      rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
      · -- v = maxRepNat: clamp to maxRep, magnitude below the truth's.
        subst hv
        refine ⟨?_, ?_⟩
        · rw [h_approx, h_result_val, h_truth_signed]
          have h_inner : (maxRepNat : ℚ) ≤ (zm.toNat : ℚ) + f := by linarith [hf_nn]
          nlinarith [h10ze'_nn, h_inner]
        · rw [h_approx, h_abs_truth, h_result_val, h_truth_signed]
          rw [show -((maxRepNat : ℚ) * 10 ^ ze') - -(((zm.toNat : ℚ) + f) * 10 ^ ze')
                = (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze' from by ring,
              show -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
          apply mul_le_mul_of_nonneg_right _ h10ze'_nn
          rw [h_denom_val, show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
              le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hzm_q_gt, hzm_q_le3, hf_nn, hf_lt1]
      · -- v = maxRepNat + 3: the round decision is dead (sbit set), so zm = maxRepUp.
        subst hv
        have hzm_eq : zm.toNat = maxRepUp.toNat := by
          rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
          · exact h
          · rw [h_bool_false (g.pushOverflow zm .upward) zm (by rw [hgP_sbit]; exact h_g_sbit_true)] at h
            exact absurd h Bool.noConfusion
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
          rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
        refine ⟨?_, ?_⟩
        · rw [h_approx, h_result_val, h_truth_signed, hzm_q_eq]
          have h_inner : ((maxRepNat : ℚ) + 3) ≤ ((maxRepNat : ℚ) + 3) + f := by linarith
          nlinarith [h10ze'_nn, h_inner]
        · rw [h_approx, h_abs_truth, h_result_val, h_truth_signed, hzm_q_eq]
          rw [show -(((maxRepNat : ℚ) + 3) * 10 ^ ze') - -((((maxRepNat : ℚ) + 3) + f) * 10 ^ ze')
                = f * 10 ^ ze' from by ring,
              show -(-((((maxRepNat : ℚ) + 3) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                = ((((maxRepNat : ℚ) + 3) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
          apply mul_le_mul_of_nonneg_right _ h10ze'_nn
          rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
              le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hf_nn, hf_lt1]
      · -- v = maxRepNat + 13: requires the round decision, dead when sbit is set.
        rw [h_bool_false g zm h_g_sbit_true] at hfire
        exact absurd hfire Bool.noConfusion

/-- `RoundsWithin`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_upward_tight`. Covers both same-sign and diff-sign
branches with the uniform magnitude scale `11/(2^63 - 18)`, **unconditionally**:
the same-sign direction is `operator_add_rounds_same_sign_upward` (both operand
signs), and the diff-sign direction is
`operator_add_rounding_bound_diff_sign_upward_dir` (via the recover-loop
digit-exactness fact `0 ≤ δ` and the `doNormalize128` direction keystone). -/
theorem operator_add_rounds_upward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .upward (11 / (2 ^ 63 - 18 : ℚ)) := by
  have h_dir : x.toRat + y.toRat ≤ result.toRat := by
    by_cases h_sign : x.negative_ = y.negative_
    · exact (operator_add_rounds_same_sign_upward_proof x y result hx hy hx_mant_ne hy_mant_ne
        h_sign h_not_zero hok).1
    · exact operator_add_rounding_bound_diff_sign_upward_dir x y result hx hy
        hx_mant_ne hy_mant_ne h_sign h_not_zero hok hresult
  refine ⟨h_dir, ?_⟩
  have h_bound := operator_add_rounding_bound_upward_tight x y result hx hy
    hx_mant_ne hy_mant_ne h_not_zero hok hresult
  have h_abs_eq : |result.toRat - (x.toRat + y.toRat)| = result.toRat - (x.toRat + y.toRat) :=
    abs_of_nonneg (by linarith)
  rw [h_abs_eq] at h_bound
  rw [show (RatValued.toRat result : ℚ) = result.toRat from rfl]
  exact le_of_lt h_bound

/-- `RoundsWithin`-shaped restatement of `operator_add_rounding_bound_same_sign_towards_zero`. -/
theorem operator_add_rounds_same_sign_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨h_dir, h_mag⟩ := operator_add_rounding_bound_same_sign_towards_zero x y result hx hy
    hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok
  exact ⟨h_dir, le_of_lt h_mag⟩

/-- `RoundsWithin`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_towards_zero_tight`. Covers both same-sign and
diff-sign branches with the uniform magnitude scale `11/(2^63 - 18)`,
**unconditionally**: the same-sign direction is
`operator_add_rounds_same_sign_towards_zero`, and the diff-sign direction is
`operator_add_rounding_bound_diff_sign_towards_zero_dir`.

(Historical note: before the f765ab1 borrow, diff-sign `.towards_zero` could
round the magnitude *away* from zero — the pipeline truncated the un-borrowed
mantissa above the truth. The borrowed form `(zm - 1) + δ` with `δ ∈ [0,1]`
restored the direction, proved through the `doNormalize128` direction
keystone.) -/
theorem operator_add_rounds_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .towards_zero (11 / (2 ^ 63 - 18 : ℚ)) := by
  have h_dir : |result.toRat| ≤ |x.toRat + y.toRat| := by
    by_cases h_sign : x.negative_ = y.negative_
    · exact (operator_add_rounds_same_sign_towards_zero_proof x y result hx hy hx_mant_ne hy_mant_ne
        h_sign h_not_zero hok).1
    · exact operator_add_rounding_bound_diff_sign_towards_zero_dir x y result hx hy
        hx_mant_ne hy_mant_ne h_sign h_not_zero hok hresult
  refine ⟨h_dir, ?_⟩
  have h_bound := operator_add_rounding_bound_towards_zero_tight x y result hx hy
    hx_mant_ne hy_mant_ne h_not_zero hok hresult
  have h_le : |x.toRat + y.toRat| - |RatValued.toRat result|
      ≤ |result.toRat - (x.toRat + y.toRat)| := by
    have h1 := abs_sub_abs_le_abs_sub (x.toRat + y.toRat) result.toRat
    rw [show (RatValued.toRat result : ℚ) = result.toRat from rfl]
    calc |x.toRat + y.toRat| - |result.toRat|
        ≤ |(x.toRat + y.toRat) - result.toRat| := h1
      _ = |result.toRat - (x.toRat + y.toRat)| := abs_sub_comm _ _
  linarith

/-- `RoundsWithin`-shaped restatement of `operator_add_rounding_bound_same_sign_to_nearest`. -/
theorem operator_add_rounds_same_sign_to_nearest_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result) :
    RoundsWithin result (x.toRat + y.toRat) .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) :=
  operator_add_rounding_bound_same_sign_to_nearest x y result hx hy hx_mant_ne hy_mant_ne
    h_same_sign h_not_zero hok

/-- `RoundsWithin`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_to_nearest_tight`. Covers both same-sign and diff-sign
branches with the uniform `ε = 6/(2^63 - 3)`, with no input-magnitude hypothesis. -/
theorem operator_add_rounds_to_nearest_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) .to_nearest (6 / (2 ^ 63 - 3 : ℚ)) :=
  operator_add_rounding_bound_to_nearest_tight x y result hx hy hx_mant_ne hy_mant_ne
    h_not_zero hok hresult

end XRPL.Model.Protocol

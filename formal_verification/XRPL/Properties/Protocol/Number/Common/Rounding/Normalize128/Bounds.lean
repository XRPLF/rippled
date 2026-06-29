import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128.Facts
import XRPL.Properties.Protocol.Number.Common.ProofTactics

namespace XRPL.Model.Protocol

/-! ## The master rounding bound for `doNormalize128` (to_nearest) -/

set_option maxHeartbeats 3200000 in
-- Four-stage pipeline composition over large UInt128 terms needs a raised budget.
/-- Rounding bound for `doNormalize128` against the true value `(M + δ) * 10^e`,
where `δ ∈ [0,1]` is the sticky tail (`δ = 0` iff the sticky flag is off). When
the flag is set, the tail-vs-mantissa ratio `δ·10^20 ≤ M` (the diff-sign add
caller has `M ≥ 10^20`; div's correction-free residual has `δ < 10⁻⁵` against
`M ≥ 10^16`) keeps the shadow slip relatively negligible — the ratio is
preserved by the scale-up stage. The constant `6/(2^63 - 3)` leaves room above
the supTight `5/(2^63 + 7)` for the slip. -/
theorem doNormalize128_rounds_to_nearest
    (zn : Bool) (M : UInt128) (e : Int) (δ : ℚ) (sticky : Bool)
    (hδ_low : 0 ≤ δ) (hδ_le : δ ≤ 1)
    (hsticky_zero : sticky = false → δ = 0)
    (hM_pos : 1 ≤ M.toNat) (hM_lt : M.toNat < 10 ^ 23)
    (hδM : sticky = true → δ * 10 ^ 20 ≤ (M.toNat : ℚ))
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max .to_nearest sticky = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    |(|result.toRat|) - ((M.toNat : ℚ) + δ) * 10 ^ e|
      ≤ ((M.toNat : ℚ) + δ) * 10 ^ e * (6 / (2 ^ 63 - 3 : ℚ))
    ∧ result.negative_ = zn := by
  have hminM_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxM_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  -- M ≠ 0.
  have hM_ne : ¬ (M == 0) = true := by
    intro h
    have : M = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at hM_pos
    simp at hM_pos
  unfold doNormalize128 at hok
  rw [if_neg hM_ne] at hok
  simp only [] at hok
  -- scaleUp stage.
  rcases hsu : doNormalize128.scaleUp largeRange.min M e with ⟨M₁, e₁⟩
  rw [hsu] at hok
  simp only [] at hok
  -- Unified post-scaleUp facts: the value is exact and the tail rescales to δ₁
  -- (δ₁ = δ·10^(e−e₁) ≤ 1 since either scaleUp is the identity or M₁ < 10^19
  -- with the tail-vs-mantissa ratio δ·10^20 ≤ M preserved by the rescale).
  obtain ⟨hval_su, hM₁_pos, hM₁_lt, he₁_le, hM₁_size⟩ :
      ((M₁.toNat : ℚ) * 10 ^ e₁ = (M.toNat : ℚ) * 10 ^ e)
      ∧ 1 ≤ M₁.toNat ∧ M₁.toNat < 10 ^ 23 ∧ e₁ ≤ e
      ∧ (M₁ = M ∧ e₁ = e ∨ M₁.toNat < 10 ^ 19) := by
    have hfacts := doNormalize128_scaleUp_facts largeRange.min M e hminM_v hM_pos hM_lt
    rw [hsu] at hfacts
    exact hfacts
  have h10e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  have h10e₁_pos : (0 : ℚ) < (10 : ℚ) ^ e₁ := zpow_pos (by norm_num) _
  set δ₁ : ℚ := δ * 10 ^ (e - e₁) with hδ₁_def
  have hδ₁_nn : 0 ≤ δ₁ :=
    mul_nonneg hδ_low (le_of_lt (zpow_pos (by norm_num) _))
  have hδ₁_zero : sticky = false → δ₁ = 0 := by
    intro hst
    rw [hδ₁_def, hsticky_zero hst, zero_mul]
  have hval₁ : ((M₁.toNat : ℚ) + δ₁) * 10 ^ e₁ = ((M.toNat : ℚ) + δ) * 10 ^ e := by
    rw [hδ₁_def, add_mul, add_mul, hval_su]
    congr 1
    rw [show δ * 10 ^ (e - e₁) * 10 ^ e₁ = δ * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
        ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
  have hδ₁M : δ₁ * 10 ^ 20 ≤ (M₁.toNat : ℚ) := by
    by_cases hst : sticky = true
    · have h := hδM hst
      have lhs_eq : δ₁ * 10 ^ 20 * 10 ^ e₁ = δ * 10 ^ 20 * 10 ^ e := by
        rw [hδ₁_def,
            show δ * 10 ^ (e - e₁) * 10 ^ 20 * 10 ^ e₁
              = δ * 10 ^ 20 * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
            ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
      have hfin : δ₁ * 10 ^ 20 * 10 ^ e₁ ≤ (M₁.toNat : ℚ) * 10 ^ e₁ := by
        rw [lhs_eq, hval_su]
        exact mul_le_mul_of_nonneg_right h (le_of_lt h10e_pos)
      exact le_of_mul_le_mul_right hfin h10e₁_pos
    · rw [Bool.not_eq_true] at hst
      rw [hδ₁_zero hst, zero_mul]
      exact Nat.cast_nonneg _
  have hδ₁_le : δ₁ ≤ 1 := by
    rcases hM₁_size with ⟨_, heeq⟩ | hlt19
    · rw [hδ₁_def, heeq, sub_self, zpow_zero, mul_one]
      exact hδ_le
    · have hM₁q : (M₁.toNat : ℚ) < 10 ^ 19 := by exact_mod_cast hlt19
      linarith [hδ₁M, hM₁q]
  -- Initial guard: represents with the appropriate shadow.
  set g₀ : Guard := (if sticky = true
      then (if zn then Guard.new.set_negative else Guard.new).set_sticky
      else (if zn then Guard.new.set_negative else Guard.new)) with hg₀_def
  set ftilde₀ : ℚ := (if sticky = true then (1 : ℚ) / 10 ^ 17 else 0) with hft₀_def
  have hrep₀ : represents g₀ ftilde₀ := by
    rw [hg₀_def, hft₀_def]
    by_cases hst : sticky = true
    · rw [if_pos hst, if_pos hst]
      exact represents_sticky_initial128 zn
    · rw [if_neg hst, if_neg hst]
      exact represents_initial128 zn
  -- (`set g₀` has already abstracted the model's seeded guard inside `hok`.)
  -- scaleDown stage.
  cases hsd : doNormalize_scaleDown128 largeRange.max M₁ e₁ g₀ with
  | error err =>
    except_clash hsd hok
  | ok sd =>
    rw [hsd] at hok
    simp only [] at hok
    obtain ⟨φ₂, ftilde₂, h2nn, h2lt, h2rep, h2slip, h2val, h2le, h2lt22, h2exp, h2sbit, h2xbit,
            _, _⟩ :=
      doNormalize_scaleDown128_repr largeRange.max M₁ e₁ g₀ hmaxM_v δ₁ ftilde₀
        hδ₁_nn hδ₁_le hrep₀ hM₁_lt sd hsd
    -- Underflow check must be false (else the result is zero).
    by_cases hund : (sd.2.1 < minExponent || sd.1 < toUInt128 largeRange.min) = true
    · rw [if_pos hund] at hok
      exfalso; apply hres
      have := Except.ok.inj hok
      rw [← this]
      rfl
    · rw [if_neg hund] at hok
      rw [Bool.not_eq_true, Bool.or_eq_false_iff] at hund
      obtain ⟨hund1, hund2⟩ := hund
      have he₂_ge : minExponent ≤ sd.2.1 := by
        by_contra h
        push_neg at h
        exact absurd (decide_eq_true h) (by rw [hund1]; simp)
      have hM₂_ge : 1000000000000000000 ≤ sd.1.toNat := by
        by_contra h
        push_neg at h
        apply absurd (decide_eq_true (show sd.1 < toUInt128 largeRange.min from by
          rw [BitVec.lt_def, toNat_toUInt128, hminM_v]; exact h))
        rw [hund2]; simp
      -- Convert to UInt64.
      have hM₂_fit : sd.1.toNat < 2 ^ 64 := by
        rw [hmaxM_v] at h2le
        omega
      have hM₂u_toNat : (toUInt64 sd.1).toNat = sd.1.toNat := toNat_toUInt64 hM₂_fit
      -- capAtMaxRep stage.
      cases hcap : doNormalize_capAtMaxRep (toUInt64 sd.1) sd.2.1 sd.2.2 with
      | error err =>
        except_clash hcap hok
      | ok cp =>
        rw [hcap] at hok
        simp only [] at hok
        obtain ⟨φ₃, ftilde₃, h3nn, h3lt, h3rep, h3slip, h3val, h3floor, h3le, h3exp, h3sbit, h3xbit,
                _, _⟩ :=
          doNormalize_capAtMaxRep_repr (toUInt64 sd.1) sd.2.1 sd.2.2
            (by rw [hM₂u_toNat]; exact hM₂_ge)
            (by rw [hM₂u_toNat]; rw [hmaxM_v] at h2le; exact h2le)
            φ₂ ftilde₂ h2nn h2lt h2rep cp hcap
        -- Final doRoundUp.
        cases hru : cp.2.2.doRoundUp zn cp.1 cp.2.1 largeRange.min largeRange.max
            .to_nearest "Number::normalize 2" with
        | error err =>
          except_clash hru hok
        | ok res =>
          rw [hru] at hok
          have h_result : result = res.toNumber := (Except.ok.inj hok).symm
          have hres_mant : res.mantissa_ ≠ 0 := by
            rw [h_result] at hres
            exact hres
          -- Positive-sign doRoundUp for the supTight lemma.
          set res_pos : RoundResult :=
            { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
            with hres_pos_def
          have h_rup_pos : cp.2.2.doRoundUp false cp.1 cp.2.1 largeRange.min largeRange.max
              .to_nearest "Number::normalize 2" = .ok res_pos :=
            doRoundUp_false_from_ok cp.2.2 zn cp.1 cp.2.1 .to_nearest "Number::normalize 2" res hru
          have hres_pos_mant : res_pos.mantissa_ ≠ 0 := hres_mant
          have h_floor_le : (mantissaFloor : ℕ) ≤ cp.1.toNat := le_of_lt h3floor
          have h_floor_vac : cp.1.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ ftilde₃ := by
            intro h
            exfalso
            omega
          have h_sup := doRoundUp_rounds_to_nearest_supTight_upTo_maxRepUp
            cp.2.2 cp.1 cp.2.1 ftilde₃ h3rep h_floor_le h3le h_floor_vac
            "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
          -- |result.toRat| in terms of res.
          have h_abs : |result.toRat| = (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ := by
            rw [h_result, abs_toRat_eq res.toNumber]
            rfl
          have h_neg : result.negative_ = zn := by
            rw [h_result]
            exact doRoundUp_negative_of_mant_ne cp.2.2 zn cp.1 cp.2.1 _ _ _
              "Number::normalize 2" res hru hres_mant
          refine ⟨?_, h_neg⟩
          -- Assemble the triangle.
          set V : ℚ := ((M.toNat : ℚ) + δ) * 10 ^ e with hV_def
          set A : ℚ := ((cp.1.toNat : ℚ) + ftilde₃) * 10 ^ cp.2.1 with hA_def
          set W : ℚ := ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 with hW_def
          have hVW : W = V := by
            rw [← h3val, hM₂u_toNat, ← h2val, hval₁]
          have h_sup' : |(|result.toRat|) - A| ≤ A * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
            rw [h_abs, hA_def]
            exact h_sup
          -- Slip at the final scale, bounded uniformly via the tail-vs-mantissa
          -- ratio: |δ₁ − f̃₀| ≤ δ₁ + 10⁻¹⁷, δ₁·10²⁰ ≤ M₁, and M₁ + δ₁ ≥ 10¹⁸
          -- (the post-scaleDown mantissa is ≥ 10¹⁸ at a no-smaller exponent),
          -- so the slip is at most V/10¹⁹.
          have h_slip_chain : |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := by
            calc |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 ≤ |φ₂ - ftilde₂| * (10 : ℚ) ^ sd.2.1 := h3slip
              _ ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := h2slip
          -- |A - V| ≤ slip at scale.
          have hAV : |A - V| ≤ |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 := by
            rw [← hVW, hA_def, hW_def]
            rw [show ((cp.1.toNat : ℚ) + ftilde₃) * 10 ^ cp.2.1
                  - ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1
                = (ftilde₃ - φ₃) * 10 ^ cp.2.1 from by ring]
            rw [abs_mul, abs_of_nonneg (le_of_lt (zpow_pos (by norm_num : (0:ℚ) < 10) _))]
            rw [abs_sub_comm]
          have h_slip_small : |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1
              ≤ V * (1 / 10 ^ 19) := by
            have h_shadow : |δ₁ - ftilde₀| ≤ δ₁ + 1 / 10 ^ 17 := by
              have hft_nn : (0 : ℚ) ≤ ftilde₀ := by
                rw [hft₀_def]; split_ifs <;> positivity
              have hft_le : ftilde₀ ≤ 1 / 10 ^ 17 := by
                rw [hft₀_def]; split_ifs <;> norm_num
              rw [abs_le]
              constructor <;> linarith [hδ₁_nn]
            have hsd_q : (1000000000000000000 : ℚ) ≤ (sd.1.toNat : ℚ) := by
              exact_mod_cast hM₂_ge
            have hM₁δ_ge : (1000000000000000000 : ℚ) ≤ (M₁.toNat : ℚ) + δ₁ := by
              have h1 : (1000000000000000000 : ℚ) * (10 : ℚ) ^ e₁
                  ≤ ((M₁.toNat : ℚ) + δ₁) * 10 ^ e₁ := by
                rw [h2val]
                calc (1000000000000000000 : ℚ) * (10 : ℚ) ^ e₁
                    ≤ (1000000000000000000 : ℚ) * (10 : ℚ) ^ sd.2.1 :=
                      mul_le_mul_of_nonneg_left
                        (zpow_le_zpow_right₀ (by norm_num) h2exp) (by norm_num)
                  _ ≤ ((sd.1.toNat : ℚ) + φ₂) * 10 ^ sd.2.1 :=
                      mul_le_mul_of_nonneg_right (by linarith [h2nn])
                        (le_of_lt (zpow_pos (by norm_num) _))
              exact le_of_mul_le_mul_right h1 h10e₁_pos
            have h_core : (δ₁ + 1 / 10 ^ 17) * 10 ^ 19 ≤ (M₁.toNat : ℚ) + δ₁ := by
              linarith [hδ₁M, hδ₁_nn, hM₁δ_ge, hδ₁_le]
            calc |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1
                ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := h_slip_chain
              _ ≤ (δ₁ + 1 / 10 ^ 17) * (10 : ℚ) ^ e₁ :=
                  mul_le_mul_of_nonneg_right h_shadow (le_of_lt h10e₁_pos)
              _ ≤ (((M₁.toNat : ℚ) + δ₁) * (1 / 10 ^ 19)) * (10 : ℚ) ^ e₁ := by
                  apply mul_le_mul_of_nonneg_right _ (le_of_lt h10e₁_pos)
                  rw [show ((M₁.toNat : ℚ) + δ₁) * (1 / 10 ^ 19)
                        = ((M₁.toNat : ℚ) + δ₁) / 10 ^ 19 from by ring,
                      le_div_iff₀ (by positivity : (0 : ℚ) < (10 : ℚ) ^ (19 : ℕ))]
                  linarith [h_core]
              _ = (((M₁.toNat : ℚ) + δ₁) * (10 : ℚ) ^ e₁) * (1 / 10 ^ 19) := by ring
              _ = V * (1 / 10 ^ 19) := by rw [hval₁]
          -- A ≤ V + slip.
          have hA_le : A ≤ V + V * (1 / 10 ^ 19) := by
            have h1 : A - V ≤ |A - V| := le_abs_self _
            have h2 := le_trans hAV h_slip_small
            linarith
          have hV_pos : 0 < V := by
            rw [hV_def]
            have hM1 : (1 : ℚ) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_pos
            have : (0 : ℚ) < (M.toNat : ℚ) + δ := by linarith
            exact mul_pos this (zpow_pos (by norm_num) _)
          -- Final triangle.
          have h_denom : (((2 ^ 63 + 7 : ℕ)) : ℚ) = 9223372036854775815 := by push_cast; norm_num
          calc |(|result.toRat|) - V|
              ≤ |(|result.toRat|) - A| + |A - V| := abs_sub_le _ A _
            _ ≤ A * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) + V * (1 / 10 ^ 19) :=
                add_le_add h_sup' (le_trans hAV h_slip_small)
            _ ≤ (V + V * (1 / 10 ^ 19)) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) + V * (1 / 10 ^ 19) := by
                have hc : (0 : ℚ) ≤ 5 / ((2 ^ 63 + 7 : ℕ) : ℚ) := by
                  rw [h_denom]; norm_num
                exact add_le_add (mul_le_mul_of_nonneg_right hA_le hc) (le_refl _)
            _ ≤ V * (6 / (2 ^ 63 - 3 : ℚ)) := by
                rw [h_denom]
                rw [show ((2 : ℚ) ^ 63 - 3) = 9223372036854775805 from by norm_num]
                nlinarith [hV_pos]

/-! ## The mode-generic rounding bound for `doNormalize128` -/

set_option maxHeartbeats 3200000 in
-- Four-stage pipeline composition over large UInt128 terms needs a raised budget.
/-- Mode-generic rounding bound for `doNormalize128` against the true value
`(M + δ) * 10^e`. Identical pipeline to `doNormalize128_rounds_to_nearest`, but
the final `doRoundUp` stage uses the any-round bound `10/(2^63 − 8)` (the round
decision cannot be related to the fraction in directed modes), so the composed
constant is `11/(2^63 − 8)`. The directed (`downward`/`upward`/`towards_zero`)
diff-sign addition bounds consume this. -/
theorem doNormalize128_rounds_any
    (zn : Bool) (M : UInt128) (e : Int) (δ : ℚ) (sticky : Bool) (mode : rounding_mode)
    (hδ_low : 0 ≤ δ) (hδ_le : δ ≤ 1)
    (hsticky_zero : sticky = false → δ = 0)
    (hM_pos : 1 ≤ M.toNat) (hM_lt : M.toNat < 10 ^ 23)
    (hδM : sticky = true → δ * 10 ^ 20 ≤ (M.toNat : ℚ))
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max mode sticky = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    |(|result.toRat|) - ((M.toNat : ℚ) + δ) * 10 ^ e|
      ≤ ((M.toNat : ℚ) + δ) * 10 ^ e * (11 / (2 ^ 63 - 8 : ℚ))
    ∧ result.negative_ = zn := by
  have hminM_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxM_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  -- M ≠ 0.
  have hM_ne : ¬ (M == 0) = true := by
    intro h
    have : M = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at hM_pos
    simp at hM_pos
  unfold doNormalize128 at hok
  rw [if_neg hM_ne] at hok
  simp only [] at hok
  -- scaleUp stage.
  rcases hsu : doNormalize128.scaleUp largeRange.min M e with ⟨M₁, e₁⟩
  rw [hsu] at hok
  simp only [] at hok
  -- Unified post-scaleUp facts: the value is exact and the tail rescales to δ₁
  -- (δ₁ = δ·10^(e−e₁) ≤ 1 since either scaleUp is the identity or M₁ < 10^19
  -- with the tail-vs-mantissa ratio δ·10^20 ≤ M preserved by the rescale).
  obtain ⟨hval_su, hM₁_pos, hM₁_lt, he₁_le, hM₁_size⟩ :
      ((M₁.toNat : ℚ) * 10 ^ e₁ = (M.toNat : ℚ) * 10 ^ e)
      ∧ 1 ≤ M₁.toNat ∧ M₁.toNat < 10 ^ 23 ∧ e₁ ≤ e
      ∧ (M₁ = M ∧ e₁ = e ∨ M₁.toNat < 10 ^ 19) := by
    have hfacts := doNormalize128_scaleUp_facts largeRange.min M e hminM_v hM_pos hM_lt
    rw [hsu] at hfacts
    exact hfacts
  have h10e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  have h10e₁_pos : (0 : ℚ) < (10 : ℚ) ^ e₁ := zpow_pos (by norm_num) _
  set δ₁ : ℚ := δ * 10 ^ (e - e₁) with hδ₁_def
  have hδ₁_nn : 0 ≤ δ₁ :=
    mul_nonneg hδ_low (le_of_lt (zpow_pos (by norm_num) _))
  have hδ₁_zero : sticky = false → δ₁ = 0 := by
    intro hst
    rw [hδ₁_def, hsticky_zero hst, zero_mul]
  have hval₁ : ((M₁.toNat : ℚ) + δ₁) * 10 ^ e₁ = ((M.toNat : ℚ) + δ) * 10 ^ e := by
    rw [hδ₁_def, add_mul, add_mul, hval_su]
    congr 1
    rw [show δ * 10 ^ (e - e₁) * 10 ^ e₁ = δ * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
        ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
  have hδ₁M : δ₁ * 10 ^ 20 ≤ (M₁.toNat : ℚ) := by
    by_cases hst : sticky = true
    · have h := hδM hst
      have lhs_eq : δ₁ * 10 ^ 20 * 10 ^ e₁ = δ * 10 ^ 20 * 10 ^ e := by
        rw [hδ₁_def,
            show δ * 10 ^ (e - e₁) * 10 ^ 20 * 10 ^ e₁
              = δ * 10 ^ 20 * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
            ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
      have hfin : δ₁ * 10 ^ 20 * 10 ^ e₁ ≤ (M₁.toNat : ℚ) * 10 ^ e₁ := by
        rw [lhs_eq, hval_su]
        exact mul_le_mul_of_nonneg_right h (le_of_lt h10e_pos)
      exact le_of_mul_le_mul_right hfin h10e₁_pos
    · rw [Bool.not_eq_true] at hst
      rw [hδ₁_zero hst, zero_mul]
      exact Nat.cast_nonneg _
  have hδ₁_le : δ₁ ≤ 1 := by
    rcases hM₁_size with ⟨_, heeq⟩ | hlt19
    · rw [hδ₁_def, heeq, sub_self, zpow_zero, mul_one]
      exact hδ_le
    · have hM₁q : (M₁.toNat : ℚ) < 10 ^ 19 := by exact_mod_cast hlt19
      linarith [hδ₁M, hM₁q]
  -- Initial guard: represents with the appropriate shadow.
  set g₀ : Guard := (if sticky = true
      then (if zn then Guard.new.set_negative else Guard.new).set_sticky
      else (if zn then Guard.new.set_negative else Guard.new)) with hg₀_def
  set ftilde₀ : ℚ := (if sticky = true then (1 : ℚ) / 10 ^ 17 else 0) with hft₀_def
  have hrep₀ : represents g₀ ftilde₀ := by
    rw [hg₀_def, hft₀_def]
    by_cases hst : sticky = true
    · rw [if_pos hst, if_pos hst]
      exact represents_sticky_initial128 zn
    · rw [if_neg hst, if_neg hst]
      exact represents_initial128 zn
  -- (`set g₀` has already abstracted the model's seeded guard inside `hok`.)
  -- scaleDown stage.
  cases hsd : doNormalize_scaleDown128 largeRange.max M₁ e₁ g₀ with
  | error err =>
    except_clash hsd hok
  | ok sd =>
    rw [hsd] at hok
    simp only [] at hok
    obtain ⟨φ₂, ftilde₂, h2nn, h2lt, h2rep, h2slip, h2val, h2le, h2lt22, h2exp, h2sbit, h2xbit,
            _, _⟩ :=
      doNormalize_scaleDown128_repr largeRange.max M₁ e₁ g₀ hmaxM_v δ₁ ftilde₀
        hδ₁_nn hδ₁_le hrep₀ hM₁_lt sd hsd
    -- Underflow check must be false (else the result is zero).
    by_cases hund : (sd.2.1 < minExponent || sd.1 < toUInt128 largeRange.min) = true
    · rw [if_pos hund] at hok
      exfalso; apply hres
      have := Except.ok.inj hok
      rw [← this]
      rfl
    · rw [if_neg hund] at hok
      rw [Bool.not_eq_true, Bool.or_eq_false_iff] at hund
      obtain ⟨hund1, hund2⟩ := hund
      have he₂_ge : minExponent ≤ sd.2.1 := by
        by_contra h
        push_neg at h
        exact absurd (decide_eq_true h) (by rw [hund1]; simp)
      have hM₂_ge : 1000000000000000000 ≤ sd.1.toNat := by
        by_contra h
        push_neg at h
        apply absurd (decide_eq_true (show sd.1 < toUInt128 largeRange.min from by
          rw [BitVec.lt_def, toNat_toUInt128, hminM_v]; exact h))
        rw [hund2]; simp
      -- Convert to UInt64.
      have hM₂_fit : sd.1.toNat < 2 ^ 64 := by
        rw [hmaxM_v] at h2le
        omega
      have hM₂u_toNat : (toUInt64 sd.1).toNat = sd.1.toNat := toNat_toUInt64 hM₂_fit
      -- capAtMaxRep stage.
      cases hcap : doNormalize_capAtMaxRep (toUInt64 sd.1) sd.2.1 sd.2.2 with
      | error err =>
        except_clash hcap hok
      | ok cp =>
        rw [hcap] at hok
        simp only [] at hok
        obtain ⟨φ₃, ftilde₃, h3nn, h3lt, h3rep, h3slip, h3val, h3floor, h3le, h3exp, h3sbit, h3xbit,
                _, _⟩ :=
          doNormalize_capAtMaxRep_repr (toUInt64 sd.1) sd.2.1 sd.2.2
            (by rw [hM₂u_toNat]; exact hM₂_ge)
            (by rw [hM₂u_toNat]; rw [hmaxM_v] at h2le; exact h2le)
            φ₂ ftilde₂ h2nn h2lt h2rep cp hcap
        -- Final doRoundUp.
        cases hru : cp.2.2.doRoundUp zn cp.1 cp.2.1 largeRange.min largeRange.max
            mode "Number::normalize 2" with
        | error err =>
          except_clash hru hok
        | ok res =>
          rw [hru] at hok
          have h_result : result = res.toNumber := (Except.ok.inj hok).symm
          have hres_mant : res.mantissa_ ≠ 0 := by
            rw [h_result] at hres
            exact hres
          -- Positive-sign doRoundUp for the supTight lemma.
          set res_pos : RoundResult :=
            { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
            with hres_pos_def
          have h_rup_pos : cp.2.2.doRoundUp false cp.1 cp.2.1 largeRange.min largeRange.max
              mode "Number::normalize 2" = .ok res_pos :=
            doRoundUp_false_from_ok cp.2.2 zn cp.1 cp.2.1 mode "Number::normalize 2" res hru
          have hres_pos_mant : res_pos.mantissa_ ≠ 0 := hres_mant
          have h_floor_le : (mantissaFloor : ℕ) ≤ cp.1.toNat := le_of_lt h3floor
          have h_sup := doRoundUp_rounds_any_supTight_upTo_maxRepUp
            cp.2.2 cp.1 cp.2.1 ftilde₃ mode h3rep h_floor_le h3le
            "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
          -- |result.toRat| in terms of res.
          have h_abs : |result.toRat| = (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ := by
            rw [h_result, abs_toRat_eq res.toNumber]
            rfl
          have h_neg : result.negative_ = zn := by
            rw [h_result]
            exact doRoundUp_negative_of_mant_ne cp.2.2 zn cp.1 cp.2.1 _ _ _
              "Number::normalize 2" res hru hres_mant
          refine ⟨?_, h_neg⟩
          -- Assemble the triangle.
          set V : ℚ := ((M.toNat : ℚ) + δ) * 10 ^ e with hV_def
          set A : ℚ := ((cp.1.toNat : ℚ) + ftilde₃) * 10 ^ cp.2.1 with hA_def
          set W : ℚ := ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 with hW_def
          have hVW : W = V := by
            rw [← h3val, hM₂u_toNat, ← h2val, hval₁]
          have h_sup' : |(|result.toRat|) - A| ≤ A * (10 / (2 ^ 63 - 8 : ℚ)) := by
            rw [h_abs, hA_def]
            exact h_sup
          -- Slip at the final scale, bounded uniformly via the tail-vs-mantissa
          -- ratio: |δ₁ − f̃₀| ≤ δ₁ + 10⁻¹⁷, δ₁·10²⁰ ≤ M₁, and M₁ + δ₁ ≥ 10¹⁸
          -- (the post-scaleDown mantissa is ≥ 10¹⁸ at a no-smaller exponent),
          -- so the slip is at most V/10¹⁹.
          have h_slip_chain : |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := by
            calc |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 ≤ |φ₂ - ftilde₂| * (10 : ℚ) ^ sd.2.1 := h3slip
              _ ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := h2slip
          -- |A - V| ≤ slip at scale.
          have hAV : |A - V| ≤ |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 := by
            rw [← hVW, hA_def, hW_def]
            rw [show ((cp.1.toNat : ℚ) + ftilde₃) * 10 ^ cp.2.1
                  - ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1
                = (ftilde₃ - φ₃) * 10 ^ cp.2.1 from by ring]
            rw [abs_mul, abs_of_nonneg (le_of_lt (zpow_pos (by norm_num : (0:ℚ) < 10) _))]
            rw [abs_sub_comm]
          have h_slip_small : |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1
              ≤ V * (1 / 10 ^ 19) := by
            have h_shadow : |δ₁ - ftilde₀| ≤ δ₁ + 1 / 10 ^ 17 := by
              have hft_nn : (0 : ℚ) ≤ ftilde₀ := by
                rw [hft₀_def]; split_ifs <;> positivity
              have hft_le : ftilde₀ ≤ 1 / 10 ^ 17 := by
                rw [hft₀_def]; split_ifs <;> norm_num
              rw [abs_le]
              constructor <;> linarith [hδ₁_nn]
            have hsd_q : (1000000000000000000 : ℚ) ≤ (sd.1.toNat : ℚ) := by
              exact_mod_cast hM₂_ge
            have hM₁δ_ge : (1000000000000000000 : ℚ) ≤ (M₁.toNat : ℚ) + δ₁ := by
              have h1 : (1000000000000000000 : ℚ) * (10 : ℚ) ^ e₁
                  ≤ ((M₁.toNat : ℚ) + δ₁) * 10 ^ e₁ := by
                rw [h2val]
                calc (1000000000000000000 : ℚ) * (10 : ℚ) ^ e₁
                    ≤ (1000000000000000000 : ℚ) * (10 : ℚ) ^ sd.2.1 :=
                      mul_le_mul_of_nonneg_left
                        (zpow_le_zpow_right₀ (by norm_num) h2exp) (by norm_num)
                  _ ≤ ((sd.1.toNat : ℚ) + φ₂) * 10 ^ sd.2.1 :=
                      mul_le_mul_of_nonneg_right (by linarith [h2nn])
                        (le_of_lt (zpow_pos (by norm_num) _))
              exact le_of_mul_le_mul_right h1 h10e₁_pos
            have h_core : (δ₁ + 1 / 10 ^ 17) * 10 ^ 19 ≤ (M₁.toNat : ℚ) + δ₁ := by
              linarith [hδ₁M, hδ₁_nn, hM₁δ_ge, hδ₁_le]
            calc |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1
                ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := h_slip_chain
              _ ≤ (δ₁ + 1 / 10 ^ 17) * (10 : ℚ) ^ e₁ :=
                  mul_le_mul_of_nonneg_right h_shadow (le_of_lt h10e₁_pos)
              _ ≤ (((M₁.toNat : ℚ) + δ₁) * (1 / 10 ^ 19)) * (10 : ℚ) ^ e₁ := by
                  apply mul_le_mul_of_nonneg_right _ (le_of_lt h10e₁_pos)
                  rw [show ((M₁.toNat : ℚ) + δ₁) * (1 / 10 ^ 19)
                        = ((M₁.toNat : ℚ) + δ₁) / 10 ^ 19 from by ring,
                      le_div_iff₀ (by positivity : (0 : ℚ) < (10 : ℚ) ^ (19 : ℕ))]
                  linarith [h_core]
              _ = (((M₁.toNat : ℚ) + δ₁) * (10 : ℚ) ^ e₁) * (1 / 10 ^ 19) := by ring
              _ = V * (1 / 10 ^ 19) := by rw [hval₁]
          -- A ≤ V + slip.
          have hA_le : A ≤ V + V * (1 / 10 ^ 19) := by
            have h1 : A - V ≤ |A - V| := le_abs_self _
            have h2 := le_trans hAV h_slip_small
            linarith
          have hV_pos : 0 < V := by
            rw [hV_def]
            have hM1 : (1 : ℚ) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_pos
            have : (0 : ℚ) < (M.toNat : ℚ) + δ := by linarith
            exact mul_pos this (zpow_pos (by norm_num) _)
          -- Final triangle.
          calc |(|result.toRat|) - V|
              ≤ |(|result.toRat|) - A| + |A - V| := abs_sub_le _ A _
            _ ≤ A * (10 / (2 ^ 63 - 8 : ℚ)) + V * (1 / 10 ^ 19) :=
                add_le_add h_sup' (le_trans hAV h_slip_small)
            _ ≤ (V + V * (1 / 10 ^ 19)) * (10 / (2 ^ 63 - 8 : ℚ)) + V * (1 / 10 ^ 19) := by
                have hc : (0 : ℚ) ≤ 10 / (2 ^ 63 - 8 : ℚ) := by norm_num
                exact add_le_add (mul_le_mul_of_nonneg_right hA_le hc) (le_refl _)
            _ ≤ V * (11 / (2 ^ 63 - 8 : ℚ)) := by
                rw [show ((2 : ℚ) ^ 63 - 8) = 9223372036854775800 from by norm_num]
                nlinarith [hV_pos]

/-! ## The direction keystone for `doNormalize128` -/

set_option maxHeartbeats 3200000 in
-- Four-stage pipeline + per-mode directional leaf analysis over 2^63-scale literals.
/-- Directional behaviour of `doNormalize128` against the true value
`(M + δ) * 10^e`, `δ ∈ [0, 1]` (the diff-sign tail is nonnegative by the
recover-loop digit-exactness fact). The result lands on the correct side of
the signed truth `(if zn then -1 else 1) · (M + δ) · 10^e`:

* `.downward`     — `result ≤ truth`;
* `.upward`       — `truth ≤ result`;
* `.towards_zero` — `|result| ≤ |truth|`.

The `doRoundUp` decisions are driven by the guard shadow `f̃₃` while the truth
carries the honest tail `φ₃ ∈ [0,1]`; the two are reconciled by:
`sticky = true` keeps the guard's `xbit` set to the end (content nonzero, so
the away-side decision fires), and `sticky = false` makes the slip zero
(`φ₃ = f̃₃` exactly), so a dead decision forces an exact result. -/
theorem doNormalize128_rounds_direction
    (zn : Bool) (M : UInt128) (e : Int) (δ : ℚ) (sticky : Bool) (mode : rounding_mode)
    (hδ_low : 0 ≤ δ) (hδ_le : δ ≤ 1)
    (hsticky_zero : sticky = false → δ = 0)
    (hM_pos : 1 ≤ M.toNat) (hM_lt : M.toNat < 10 ^ 23)
    (hδM : sticky = true → δ * 10 ^ 20 ≤ (M.toNat : ℚ))
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max mode sticky = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    (mode = .downward →
      result.toRat ≤ (if zn = true then -(((M.toNat : ℚ) + δ) * 10 ^ e)
                      else ((M.toNat : ℚ) + δ) * 10 ^ e)) ∧
    (mode = .upward →
      (if zn = true then -(((M.toNat : ℚ) + δ) * 10 ^ e)
       else ((M.toNat : ℚ) + δ) * 10 ^ e) ≤ result.toRat) ∧
    (mode = .towards_zero →
      |result.toRat| ≤ ((M.toNat : ℚ) + δ) * 10 ^ e) := by
  have hminM_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxM_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  -- M ≠ 0.
  have hM_ne : ¬ (M == 0) = true := by
    intro h
    have : M = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at hM_pos
    simp at hM_pos
  unfold doNormalize128 at hok
  rw [if_neg hM_ne] at hok
  simp only [] at hok
  -- scaleUp stage.
  rcases hsu : doNormalize128.scaleUp largeRange.min M e with ⟨M₁, e₁⟩
  rw [hsu] at hok
  simp only [] at hok
  -- Unified post-scaleUp facts: the value is exact and the tail rescales to δ₁.
  obtain ⟨hval_su, hM₁_pos, hM₁_lt, he₁_le, hM₁_size⟩ :
      ((M₁.toNat : ℚ) * 10 ^ e₁ = (M.toNat : ℚ) * 10 ^ e)
      ∧ 1 ≤ M₁.toNat ∧ M₁.toNat < 10 ^ 23 ∧ e₁ ≤ e
      ∧ (M₁ = M ∧ e₁ = e ∨ M₁.toNat < 10 ^ 19) := by
    have hfacts := doNormalize128_scaleUp_facts largeRange.min M e hminM_v hM_pos hM_lt
    rw [hsu] at hfacts
    exact hfacts
  have h10e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  have h10e₁_pos : (0 : ℚ) < (10 : ℚ) ^ e₁ := zpow_pos (by norm_num) _
  set δ₁ : ℚ := δ * 10 ^ (e - e₁) with hδ₁_def
  have hδ₁_nn : 0 ≤ δ₁ :=
    mul_nonneg hδ_low (le_of_lt (zpow_pos (by norm_num) _))
  have hδ₁_zero : sticky = false → δ₁ = 0 := by
    intro hst
    rw [hδ₁_def, hsticky_zero hst, zero_mul]
  have hval₁ : ((M₁.toNat : ℚ) + δ₁) * 10 ^ e₁ = ((M.toNat : ℚ) + δ) * 10 ^ e := by
    rw [hδ₁_def, add_mul, add_mul, hval_su]
    congr 1
    rw [show δ * 10 ^ (e - e₁) * 10 ^ e₁ = δ * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
        ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
  have hδ₁M : δ₁ * 10 ^ 20 ≤ (M₁.toNat : ℚ) := by
    by_cases hst : sticky = true
    · have h := hδM hst
      have lhs_eq : δ₁ * 10 ^ 20 * 10 ^ e₁ = δ * 10 ^ 20 * 10 ^ e := by
        rw [hδ₁_def,
            show δ * 10 ^ (e - e₁) * 10 ^ 20 * 10 ^ e₁
              = δ * 10 ^ 20 * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
            ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
      have hfin : δ₁ * 10 ^ 20 * 10 ^ e₁ ≤ (M₁.toNat : ℚ) * 10 ^ e₁ := by
        rw [lhs_eq, hval_su]
        exact mul_le_mul_of_nonneg_right h (le_of_lt h10e_pos)
      exact le_of_mul_le_mul_right hfin h10e₁_pos
    · rw [Bool.not_eq_true] at hst
      rw [hδ₁_zero hst, zero_mul]
      exact Nat.cast_nonneg _
  have hδ₁_le : δ₁ ≤ 1 := by
    rcases hM₁_size with ⟨_, heeq⟩ | hlt19
    · rw [hδ₁_def, heeq, sub_self, zpow_zero, mul_one]
      exact hδ_le
    · have hM₁q : (M₁.toNat : ℚ) < 10 ^ 19 := by exact_mod_cast hlt19
      linarith [hδ₁M, hM₁q]
  -- Initial guard: represents with the appropriate shadow.
  set g₀ : Guard := (if sticky = true
      then (if zn then Guard.new.set_negative else Guard.new).set_sticky
      else (if zn then Guard.new.set_negative else Guard.new)) with hg₀_def
  set ftilde₀ : ℚ := (if sticky = true then (1 : ℚ) / 10 ^ 17 else 0) with hft₀_def
  have hrep₀ : represents g₀ ftilde₀ := by
    rw [hg₀_def, hft₀_def]
    by_cases hst : sticky = true
    · rw [if_pos hst, if_pos hst]
      exact represents_sticky_initial128 zn
    · rw [if_neg hst, if_neg hst]
      exact represents_initial128 zn
  -- scaleDown stage.
  cases hsd : doNormalize_scaleDown128 largeRange.max M₁ e₁ g₀ with
  | error err =>
    except_clash hsd hok
  | ok sd =>
    rw [hsd] at hok
    simp only [] at hok
    obtain ⟨φ₂, ftilde₂, h2nn, h2lt, h2rep, h2slip, h2val, h2le, h2lt22, h2exp, h2sbit, h2xbit,
            _, _⟩ :=
      doNormalize_scaleDown128_repr largeRange.max M₁ e₁ g₀ hmaxM_v δ₁ ftilde₀
        hδ₁_nn hδ₁_le hrep₀ hM₁_lt sd hsd
    by_cases hund : (sd.2.1 < minExponent || sd.1 < toUInt128 largeRange.min) = true
    · rw [if_pos hund] at hok
      exfalso; apply hres
      have := Except.ok.inj hok
      rw [← this]
      rfl
    · rw [if_neg hund] at hok
      rw [Bool.not_eq_true, Bool.or_eq_false_iff] at hund
      obtain ⟨hund1, hund2⟩ := hund
      have hM₂_ge : 1000000000000000000 ≤ sd.1.toNat := by
        by_contra h
        push_neg at h
        apply absurd (decide_eq_true (show sd.1 < toUInt128 largeRange.min from by
          rw [BitVec.lt_def, toNat_toUInt128, hminM_v]; exact h))
        rw [hund2]; simp
      have hM₂_fit : sd.1.toNat < 2 ^ 64 := by
        rw [hmaxM_v] at h2le
        omega
      have hM₂u_toNat : (toUInt64 sd.1).toNat = sd.1.toNat := toNat_toUInt64 hM₂_fit
      -- capAtMaxRep stage.
      cases hcap : doNormalize_capAtMaxRep (toUInt64 sd.1) sd.2.1 sd.2.2 with
      | error err =>
        except_clash hcap hok
      | ok cp =>
        rw [hcap] at hok
        simp only [] at hok
        obtain ⟨φ₃, ftilde₃, h3nn, h3lt, h3rep, h3slip, h3val, h3floor, h3le, h3exp, h3sbit, h3xbit,
                _, _⟩ :=
          doNormalize_capAtMaxRep_repr (toUInt64 sd.1) sd.2.1 sd.2.2
            (by rw [hM₂u_toNat]; exact hM₂_ge)
            (by rw [hM₂u_toNat]; rw [hmaxM_v] at h2le; exact h2le)
            φ₂ ftilde₂ h2nn h2lt h2rep cp hcap
        -- Final doRoundUp.
        cases hru : cp.2.2.doRoundUp zn cp.1 cp.2.1 largeRange.min largeRange.max
            mode "Number::normalize 2" with
        | error err =>
          except_clash hru hok
        | ok res =>
          rw [hru] at hok
          have h_result : result = res.toNumber := (Except.ok.inj hok).symm
          have hres_mant : res.mantissa_ ≠ 0 := by
            rw [h_result] at hres
            exact hres
          set res_pos : RoundResult :=
            { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
            with hres_pos_def
          have h_rup_pos : cp.2.2.doRoundUp false cp.1 cp.2.1 largeRange.min largeRange.max
              mode "Number::normalize 2" = .ok res_pos :=
            doRoundUp_false_from_ok cp.2.2 zn cp.1 cp.2.1 mode "Number::normalize 2" res hru
          have hres_pos_mant : res_pos.mantissa_ ≠ 0 := hres_mant
          have h_abs : |result.toRat| = (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ := by
            rw [h_result, abs_toRat_eq res.toNumber]
            rfl
          have h_neg : result.negative_ = zn := by
            rw [h_result]
            exact doRoundUp_negative_of_mant_ne cp.2.2 zn cp.1 cp.2.1 _ _ _
              "Number::normalize 2" res hru hres_mant
          -- The exact value equation at the doRoundUp stage.
          set V : ℚ := ((M.toNat : ℚ) + δ) * 10 ^ e with hV_def
          have hVW : ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 = V := by
            rw [← h3val, hM₂u_toNat, ← h2val, hval₁]
          -- sbit and xbit at the final guard.
          have hg₀_sbit : g₀.sbit_ = zn := by
            rw [hg₀_def]
            by_cases hst : sticky = true
            · rw [if_pos hst]; cases hzn : zn <;> rfl
            · rw [if_neg hst]; cases hzn : zn <;> rfl
          have hsbit₃ : cp.2.2.sbit_ = zn := by
            rw [h3sbit, h2sbit, hg₀_sbit]
          have hg₀_xbit : sticky = true → g₀.xbit_ = true := by
            intro hst
            rw [hg₀_def, if_pos hst]
            cases hzn : zn <;> rfl
          have hxbit₃ : sticky = true → cp.2.2.xbit_ = true :=
            fun hst => h3xbit (h2xbit (hg₀_xbit hst))
          -- sticky = false → no slip → φ₃ = f̃₃.
          have hφf : sticky = false → φ₃ = ftilde₃ := by
            intro hst
            have hft0 : ftilde₀ = 0 := by
              rw [hft₀_def, if_neg (by rw [hst]; exact Bool.false_ne_true)]
            have hδ0 : δ₁ = 0 := hδ₁_zero hst
            have h_zero : |δ₁ - ftilde₀| = 0 := by rw [hδ0, hft0]; norm_num
            have h_chain : |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 ≤ 0 := by
              calc |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1
                  ≤ |φ₂ - ftilde₂| * (10 : ℚ) ^ sd.2.1 := h3slip
                _ ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := h2slip
                _ = 0 := by rw [h_zero]; ring
            have h10pos : (0 : ℚ) < (10 : ℚ) ^ cp.2.1 := zpow_pos (by norm_num) _
            have habs_le : |φ₃ - ftilde₃| ≤ 0 := by
              by_contra hgt
              push_neg at hgt
              nlinarith [h_chain, h10pos]
            have habs0 : |φ₃ - ftilde₃| = 0 := le_antisymm habs_le (abs_nonneg _)
            have hd0 := abs_eq_zero.mp habs0
            linarith
          -- guard content empty → φ₃ = 0 (the value is exact).
          have hφ_zero_of_empty : cp.2.2.digits_ = 0 → cp.2.2.xbit_ = false → φ₃ = 0 := by
            intro hd hx
            have hft0 : ftilde₃ = 0 :=
              represents_eq_zero_of_digits_zero_xbit_false hd hx h3rep
            by_cases hst : sticky = true
            · exact absurd (hxbit₃ hst) (by rw [hx]; exact Bool.false_ne_true)
            · rw [Bool.not_eq_true] at hst
              rw [hφf hst, hft0]
          have h10cp_pos : (0 : ℚ) < (10 : ℚ) ^ cp.2.1 := zpow_pos (by norm_num) _
          have h10cp_nn : (0 : ℚ) ≤ (10 : ℚ) ^ cp.2.1 := le_of_lt h10cp_pos
          have hgP_sbit : (cp.2.2.pushOverflow cp.1 mode).sbit_ = cp.2.2.sbit_ := by
            simp only [Guard.pushOverflow, Guard.push]
            split_ifs <;> rfl
          -- result.toRat in signed form.
          have h_res_eq_pos : zn = false → result.toRat
              = (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ := by
            intro hznf
            have h_resneg : result.negative_ = false := h_neg.trans hznf
            have h_nn := Number.toRat_nonneg_of_nonnegative result h_resneg
            rw [← abs_of_nonneg h_nn]
            exact h_abs
          have h_res_eq_neg : zn = true → result.toRat
              = -((res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_) := by
            intro hznt
            have h_resneg : result.negative_ = true := h_neg.trans hznt
            have h_np := Number.toRat_nonpos_of_negative result h_resneg
            have h1 : result.toRat = -|result.toRat| := by
              rw [abs_of_nonpos h_np]; ring
            rw [h1, h_abs]
          refine ⟨?_, ?_, ?_⟩
          · -- ===== .downward: result ≤ truth =====
            intro hmode
            subst hmode
            -- Dead-decision helper for the truncating sign.
            have h_bool_false : ∀ g' : Guard, g'.sbit_ = false →
                (((g'.round .downward == 1)
                  || ((g'.round .downward == 0) && (cp.1 % 2 == 1))) = false) := by
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
            cases hzn : zn
            · -- zn = false: the magnitude truncates; result = RV ≤ W = V.
              have h_sbit_f : cp.2.2.sbit_ = false := hsbit₃.trans hzn
              have h_no_sru : ¬ cp.2.2.shouldRoundUp_downward := by
                intro h
                have := h.1
                rw [h_sbit_f] at this; exact Bool.noConfusion this
              have hRV_le : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
                  ≤ ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 := by
                by_cases h_zm_le_rep : cp.1.toNat ≤ maxRep.toNat
                · have h_tr := doRoundUp_value_downward_truncate cp.2.2 false cp.1 cp.2.1
                    h_no_sru h_zm_le_rep "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
                  rw [h_tr]
                  exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                · push_neg at h_zm_le_rep
                  obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases cp.2.2 cp.1
                    cp.2.1 .downward h_zm_le_rep h3le "Number::normalize 2" res_pos
                    h_rup_pos hres_pos_mant
                  rw [hv_val]
                  have hzm_q_gt : (maxRepNat : ℚ) < (cp.1.toNat : ℚ) := by
                    have : (maxRepNat : ℕ) < cp.1.toNat := by
                      rw [← maxRep_val]; exact h_zm_le_rep
                    exact_mod_cast this
                  rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
                  · subst hv
                    exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                  · subst hv
                    have hzm_eq : cp.1.toNat = maxRepUp.toNat := by
                      rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
                      · exact h
                      · rw [h_bool_false (cp.2.2.pushOverflow cp.1 .downward)
                            (by rw [hgP_sbit]; exact h_sbit_f)] at h
                        exact absurd h Bool.noConfusion
                    have hzm_q_eq : (cp.1.toNat : ℚ) = maxRepNat + 3 := by
                      rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
                    rw [hzm_q_eq]
                    exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                  · rw [h_bool_false cp.2.2 h_sbit_f] at hfire
                    exact absurd hfire Bool.noConfusion
              change result.toRat ≤ V
              rw [h_res_eq_pos hzn]
              calc (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_
                  = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := rfl
                _ ≤ ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 := hRV_le
                _ = V := hVW
            · -- zn = true: the magnitude rounds up (or is exact); -RV ≤ -W = -V.
              have h_sbit_t : cp.2.2.sbit_ = true := hsbit₃.trans hzn
              have hRV_ge : ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1
                  ≤ (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
                by_cases h_zm_le_rep : cp.1.toNat ≤ maxRep.toNat
                · by_cases h_sru : cp.2.2.shouldRoundUp_downward
                  · by_cases h_cusp : cp.1 = maxRep
                    · have h_val := doRoundUp_value_downward_roundUp_cusp cp.2.2 false cp.1
                        cp.2.1 h_cusp h_sru "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
                      rw [h_val]
                      have hzm_q : (cp.1.toNat : ℚ) = maxRepNat := by
                        rw [show cp.1.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]
                        norm_num
                      rw [hzm_q]
                      apply mul_le_mul_of_nonneg_right _ h10cp_nn
                      rw [show (maxRepCuspTarget : ℚ) = maxRepNat + 3 from by norm_num]
                      linarith [h3lt]
                    · have hzm_lt : cp.1.toNat < maxRep.toNat := by
                        have : cp.1.toNat ≠ maxRep.toNat :=
                          fun heq => h_cusp (UInt64.toNat_inj.mp heq)
                        omega
                      have h_val := doRoundUp_value_downward_roundUp_noCusp cp.2.2 false cp.1
                        cp.2.1 h_sru (by omega) "Number::normalize 2" res_pos
                        h_rup_pos hres_pos_mant
                      rw [h_val]
                      exact mul_le_mul_of_nonneg_right (by linarith [h3lt]) h10cp_nn
                  · obtain ⟨h_dig0, h_xbit0⟩ :=
                      content_empty_of_not_shouldRoundUp_downward cp.2.2 h_sbit_t h_sru
                    have hφ0 : φ₃ = 0 := hφ_zero_of_empty h_dig0 h_xbit0
                    have h_tr := doRoundUp_value_downward_truncate cp.2.2 false cp.1 cp.2.1
                      h_sru h_zm_le_rep "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
                    rw [h_tr, hφ0, add_zero]
                · push_neg at h_zm_le_rep
                  obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases cp.2.2 cp.1
                    cp.2.1 .downward h_zm_le_rep h3le "Number::normalize 2" res_pos
                    h_rup_pos hres_pos_mant
                  rw [hv_val]
                  rcases hv_cases with ⟨hv, hzm_lt_up, hbool⟩ | ⟨hv, hcoup⟩ | ⟨hv, hzm_eq, _⟩
                  · -- the truncating clamp is impossible: the pushed decision fires.
                    exfalso
                    obtain ⟨hdig_pos, hsb⟩ := pushOverflow_cusp_interior_facts cp.2.2 cp.1
                      .downward h_zm_le_rep hzm_lt_up
                    have h1 : (cp.2.2.pushOverflow cp.1 .downward).round .downward = 1 :=
                      (round_downward_eq_one_iff _).mpr
                        ⟨by rw [hsb]; exact h_sbit_t, Or.inl hdig_pos⟩
                    rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hbool
                    exact Bool.noConfusion hbool
                  · subst hv
                    rcases hcoup with ⟨hzm_eq, hdead⟩ | ⟨hzm_lt_up, _⟩
                    · -- dead decision at maxRepUp: φ₃ = 0, exact.
                      have hφ0 : φ₃ = 0 := by
                        rcases hdead with hb1 | hb2
                        · obtain ⟨h_dig0, h_xbit0⟩ :=
                            roundUp_bool_downward_false_content cp.2.2 cp.1 h_sbit_t hb1
                          exact hφ_zero_of_empty h_dig0 h_xbit0
                        · have h_push_sbit : (cp.2.2.push (maxRepUp % 10)).sbit_
                              = cp.2.2.sbit_ := rfl
                          obtain ⟨h_dig0', h_xbit0'⟩ :=
                            roundUp_bool_downward_false_content (cp.2.2.push (maxRepUp % 10))
                              (maxRepUp / 10) (h_push_sbit.trans h_sbit_t) hb2
                          obtain ⟨h_dig0, h_xbit0⟩ :=
                            push_content_empty cp.2.2 (maxRepUp % 10) h_dig0' h_xbit0'
                          exact hφ_zero_of_empty h_dig0 h_xbit0
                      have hzm_q_eq : (cp.1.toNat : ℚ) = maxRepNat + 3 := by
                        rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]
                        norm_num
                      rw [hφ0, add_zero, hzm_q_eq]
                    · -- fired clamp at the interior: maxRepNat + 3 ≥ cp.1 + φ₃.
                      have hzm_q_lt : (cp.1.toNat : ℚ) ≤ maxRepNat + 2 := by
                        have h2 : cp.1.toNat ≤ 9223372036854775809 := by
                          rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hzm_lt_up
                          omega
                        calc (cp.1.toNat : ℚ)
                            ≤ ((9223372036854775809 : ℕ) : ℚ) := by exact_mod_cast h2
                          _ = maxRepNat + 2 := by norm_num
                      exact mul_le_mul_of_nonneg_right (by linarith [h3lt]) h10cp_nn
                  · subst hv
                    have hzm_q_eq : (cp.1.toNat : ℚ) = maxRepNat + 3 := by
                      rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
                    rw [hzm_q_eq]
                    exact mul_le_mul_of_nonneg_right (by linarith [h3lt]) h10cp_nn
              change result.toRat ≤ -V
              rw [h_res_eq_neg hzn]
              have h2 : V ≤ (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ := by
                rw [← hVW]; exact hRV_ge
              linarith
          · -- ===== .upward: truth ≤ result =====
            intro hmode
            subst hmode
            have h_bool_false : ∀ g' : Guard, g'.sbit_ = true →
                (((g'.round .upward == 1)
                  || ((g'.round .upward == 0) && (cp.1 % 2 == 1))) = false) := by
              intro g' hsb
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
            cases hzn : zn
            · -- zn = false: the magnitude rounds up (or is exact); V = W ≤ RV.
              have h_sbit_f : cp.2.2.sbit_ = false := hsbit₃.trans hzn
              have hRV_ge : ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1
                  ≤ (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
                by_cases h_zm_le_rep : cp.1.toNat ≤ maxRep.toNat
                · by_cases h_sru : cp.2.2.shouldRoundUp_upward
                  · by_cases h_cusp : cp.1 = maxRep
                    · have h_val := doRoundUp_value_upward_roundUp_cusp cp.2.2 false cp.1
                        cp.2.1 h_cusp h_sru "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
                      rw [h_val]
                      have hzm_q : (cp.1.toNat : ℚ) = maxRepNat := by
                        rw [show cp.1.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]
                        norm_num
                      rw [hzm_q]
                      apply mul_le_mul_of_nonneg_right _ h10cp_nn
                      rw [show (maxRepCuspTarget : ℚ) = maxRepNat + 3 from by norm_num]
                      linarith [h3lt]
                    · have hzm_lt : cp.1.toNat < maxRep.toNat := by
                        have : cp.1.toNat ≠ maxRep.toNat :=
                          fun heq => h_cusp (UInt64.toNat_inj.mp heq)
                        omega
                      have h_val := doRoundUp_value_upward_roundUp_noCusp cp.2.2 false cp.1
                        cp.2.1 h_sru (by omega) "Number::normalize 2" res_pos
                        h_rup_pos hres_pos_mant
                      rw [h_val]
                      exact mul_le_mul_of_nonneg_right (by linarith [h3lt]) h10cp_nn
                  · obtain ⟨h_dig0, h_xbit0⟩ :=
                      content_empty_of_not_shouldRoundUp_upward cp.2.2 h_sbit_f h_sru
                    have hφ0 : φ₃ = 0 := hφ_zero_of_empty h_dig0 h_xbit0
                    have h_tr := doRoundUp_value_upward_truncate cp.2.2 false cp.1 cp.2.1
                      h_sru h_zm_le_rep "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
                    rw [h_tr, hφ0, add_zero]
                · push_neg at h_zm_le_rep
                  obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases cp.2.2 cp.1
                    cp.2.1 .upward h_zm_le_rep h3le "Number::normalize 2" res_pos
                    h_rup_pos hres_pos_mant
                  rw [hv_val]
                  rcases hv_cases with ⟨hv, hzm_lt_up, hbool⟩ | ⟨hv, hcoup⟩ | ⟨hv, hzm_eq, _⟩
                  · exfalso
                    obtain ⟨hdig_pos, hsb⟩ := pushOverflow_cusp_interior_facts cp.2.2 cp.1
                      .upward h_zm_le_rep hzm_lt_up
                    have h1 : (cp.2.2.pushOverflow cp.1 .upward).round .upward = 1 :=
                      (round_upward_eq_one_iff _).mpr
                        ⟨by rw [hsb]; exact h_sbit_f, Or.inl hdig_pos⟩
                    rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hbool
                    exact Bool.noConfusion hbool
                  · subst hv
                    rcases hcoup with ⟨hzm_eq, hdead⟩ | ⟨hzm_lt_up, _⟩
                    · have hφ0 : φ₃ = 0 := by
                        rcases hdead with hb1 | hb2
                        · obtain ⟨h_dig0, h_xbit0⟩ :=
                            roundUp_bool_upward_false_content cp.2.2 cp.1 h_sbit_f hb1
                          exact hφ_zero_of_empty h_dig0 h_xbit0
                        · have h_push_sbit : (cp.2.2.push (maxRepUp % 10)).sbit_
                              = cp.2.2.sbit_ := rfl
                          obtain ⟨h_dig0', h_xbit0'⟩ :=
                            roundUp_bool_upward_false_content (cp.2.2.push (maxRepUp % 10))
                              (maxRepUp / 10) (h_push_sbit.trans h_sbit_f) hb2
                          obtain ⟨h_dig0, h_xbit0⟩ :=
                            push_content_empty cp.2.2 (maxRepUp % 10) h_dig0' h_xbit0'
                          exact hφ_zero_of_empty h_dig0 h_xbit0
                      have hzm_q_eq : (cp.1.toNat : ℚ) = maxRepNat + 3 := by
                        rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]
                        norm_num
                      rw [hφ0, add_zero, hzm_q_eq]
                    · have hzm_q_lt : (cp.1.toNat : ℚ) ≤ maxRepNat + 2 := by
                        have h2 : cp.1.toNat ≤ 9223372036854775809 := by
                          rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hzm_lt_up
                          omega
                        calc (cp.1.toNat : ℚ)
                            ≤ ((9223372036854775809 : ℕ) : ℚ) := by exact_mod_cast h2
                          _ = maxRepNat + 2 := by norm_num
                      exact mul_le_mul_of_nonneg_right (by linarith [h3lt]) h10cp_nn
                  · subst hv
                    have hzm_q_eq : (cp.1.toNat : ℚ) = maxRepNat + 3 := by
                      rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
                    rw [hzm_q_eq]
                    exact mul_le_mul_of_nonneg_right (by linarith [h3lt]) h10cp_nn
              change V ≤ result.toRat
              rw [h_res_eq_pos hzn]
              calc V = ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 := hVW.symm
                _ ≤ (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := hRV_ge
                _ = (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ := rfl
            · -- zn = true: the magnitude truncates; -V = -W ≤ -RV.
              have h_sbit_t : cp.2.2.sbit_ = true := hsbit₃.trans hzn
              have h_no_sru : ¬ cp.2.2.shouldRoundUp_upward := by
                intro h
                have := h.1
                rw [h_sbit_t] at this; exact Bool.noConfusion this
              have hRV_le : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
                  ≤ ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 := by
                by_cases h_zm_le_rep : cp.1.toNat ≤ maxRep.toNat
                · have h_tr := doRoundUp_value_upward_truncate cp.2.2 false cp.1 cp.2.1
                    h_no_sru h_zm_le_rep "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
                  rw [h_tr]
                  exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                · push_neg at h_zm_le_rep
                  obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases cp.2.2 cp.1
                    cp.2.1 .upward h_zm_le_rep h3le "Number::normalize 2" res_pos
                    h_rup_pos hres_pos_mant
                  rw [hv_val]
                  have hzm_q_gt : (maxRepNat : ℚ) < (cp.1.toNat : ℚ) := by
                    have : (maxRepNat : ℕ) < cp.1.toNat := by
                      rw [← maxRep_val]; exact h_zm_le_rep
                    exact_mod_cast this
                  rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
                  · subst hv
                    exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                  · subst hv
                    have hzm_eq : cp.1.toNat = maxRepUp.toNat := by
                      rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
                      · exact h
                      · rw [h_bool_false (cp.2.2.pushOverflow cp.1 .upward)
                            (by rw [hgP_sbit]; exact h_sbit_t)] at h
                        exact absurd h Bool.noConfusion
                    have hzm_q_eq : (cp.1.toNat : ℚ) = maxRepNat + 3 := by
                      rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
                    rw [hzm_q_eq]
                    exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                  · rw [h_bool_false cp.2.2 h_sbit_t] at hfire
                    exact absurd hfire Bool.noConfusion
              change -V ≤ result.toRat
              rw [h_res_eq_neg hzn]
              have h2 : (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ ≤ V := by
                rw [← hVW]; exact hRV_le
              linarith
          · -- ===== .towards_zero: |result| ≤ |truth| =====
            intro hmode
            subst hmode
            have hRV_le : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
                ≤ ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 := by
              by_cases h_zm_le_rep : cp.1.toNat ≤ maxRep.toNat
              · have h_tr := doRoundUp_value_towards_zero_truncate cp.2.2 false cp.1 cp.2.1
                  h_zm_le_rep "Number::normalize 2" res_pos h_rup_pos hres_pos_mant
                rw [h_tr]
                exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
              · push_neg at h_zm_le_rep
                obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases cp.2.2 cp.1
                  cp.2.1 .towards_zero h_zm_le_rep h3le "Number::normalize 2" res_pos
                  h_rup_pos hres_pos_mant
                rw [hv_val]
                have hzm_q_gt : (maxRepNat : ℚ) < (cp.1.toNat : ℚ) := by
                  have : (maxRepNat : ℕ) < cp.1.toNat := by
                    rw [← maxRep_val]; exact h_zm_le_rep
                  exact_mod_cast this
                rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
                · subst hv
                  exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                · subst hv
                  have hzm_eq : cp.1.toNat = maxRepUp.toNat := by
                    rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
                    · exact h
                    · rw [roundUp_bool_towards_zero_false] at h
                      exact absurd h Bool.noConfusion
                  have hzm_q_eq : (cp.1.toNat : ℚ) = maxRepNat + 3 := by
                    rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
                  rw [hzm_q_eq]
                  exact mul_le_mul_of_nonneg_right (by linarith [h3nn]) h10cp_nn
                · rw [roundUp_bool_towards_zero_false] at hfire
                  exact absurd hfire Bool.noConfusion
            change |result.toRat| ≤ V
            rw [h_abs]
            calc (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_
                = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := rfl
              _ ≤ ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 := hRV_le
              _ = V := hVW

end XRPL.Model.Protocol

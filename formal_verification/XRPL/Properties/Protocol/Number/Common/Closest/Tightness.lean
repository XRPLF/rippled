import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Closest.Bounds


namespace XRPL.Model.Protocol

/-! ## Helper: exponent lower bound for any representable Number ≥ q -/

lemma exponent_ge_of_toRat_ge (q : ℚ) (hq : 0 < q) (m : Number)
    (h_norm : m.isNormalized) (h_ge : q ≤ m.toRat) :
    Int.log 10 q - mantissaLog ≤ m.exponent_ := by
  have h_m_neg : m.negative_ = false := by
    rcases h : m.negative_ with _ | _
    · rfl
    · exfalso
      have h_mtr_le : m.toRat ≤ 0 := Number.toRat_nonpos_of_negative m h
      linarith
  have h_m_zero_ne : m ≠ Number.zero := by
    intro h_eq
    have h_eq2 : m.toRat = 0 := by rw [h_eq, Number.toRat_zero]
    linarith
  rcases h_norm with h_zero | ⟨h_min, h_max, _h_valid, h_emin, h_emax⟩
  · exact absurd h_zero h_m_zero_ne
  have h_min_nat : (10^18 : ℕ) ≤ m.mantissa_.toNat := by
    have h := h_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10^18 := by decide
    omega
  have h_max_nat : m.mantissa_.toNat ≤ (10^19 - 1 : ℕ) := by
    have h := h_max
    rw [UInt64.le_iff_toNat_le] at h
    have hmax_eq : largeRange.max.toNat = 10^19 - 1 := by decide
    omega
  have h_max_lt : m.mantissa_.toNat < 10^19 := by omega
  rw [Number.toRat_of_nonneg m h_m_neg] at h_ge
  have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ m.exponent_ := zpow_pos (by norm_num) _
  have h_mtr_lt : m.toRat < (10 : ℚ) ^ (m.exponent_ + 19) := by
    rw [Number.toRat_of_nonneg m h_m_neg]
    have h_cast_lt : (m.mantissa_.toNat : ℚ) < (10 : ℚ) ^ (19 : ℕ) := by
      have : (m.mantissa_.toNat : ℚ) < ((10^19 : ℕ) : ℚ) := by exact_mod_cast h_max_lt
      push_cast at this; exact this
    have h_lhs_lt : (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_
        < (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ m.exponent_ :=
      mul_lt_mul_of_pos_right h_cast_lt h_pow_pos
    have h_pow_eq : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ m.exponent_
        = (10 : ℚ) ^ (m.exponent_ + 19) := by
      rw [← zpow_natCast (10 : ℚ) 19, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      congr 1; push_cast; ring
    rw [← h_pow_eq]; exact h_lhs_lt
  have h_mtr_lt' : (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_
      < (10 : ℚ) ^ (m.exponent_ + 19) := by
    rw [← Number.toRat_of_nonneg m h_m_neg]; exact h_mtr_lt
  have h_q_lt : q < (10 : ℚ) ^ (m.exponent_ + 19) := lt_of_le_of_lt h_ge h_mtr_lt'
  have h_log_le : (10 : ℚ) ^ Int.log 10 q ≤ q := Int.zpow_log_le_self (by norm_num) hq
  have h_log_lt : (10 : ℚ) ^ Int.log 10 q < (10 : ℚ) ^ (m.exponent_ + 19) :=
    lt_of_le_of_lt h_log_le h_q_lt
  have h_one_lt : (1 : ℚ) < 10 := by norm_num
  have h_exp_lt : Int.log 10 q < m.exponent_ + 19 := by
    exact (zpow_lt_zpow_iff_right₀ h_one_lt).mp h_log_lt
  linarith

/-! ## Helper: tightness of `upperPosAux` for positive q -/

lemma upperPosAux_tight (q : ℚ) (hq : 0 < q) (n : Number)
    (h : upperPosAux q = some n) (m : Number) (h_norm : m.isNormalized)
    (h_ge : q ≤ m.toRat) : n.toRat ≤ m.toRat := by
  have h_m_neg : m.negative_ = false := by
    rcases hmn : m.negative_ with _ | _
    · rfl
    · exfalso
      have h_mtr_le : m.toRat ≤ 0 := Number.toRat_nonpos_of_negative m hmn
      linarith
  have h_m_zero_ne : m ≠ Number.zero := by
    intro h_eq
    have h_eq2 : m.toRat = 0 := by rw [h_eq, Number.toRat_zero]
    linarith
  have h_m_bounds : largeRange.min.toNat ≤ m.mantissa_.toNat ∧ m.mantissa_.toNat ≤ largeRange.max.toNat := by
    rcases h_norm with h_zero | ⟨h_min, h_max, _, _, _⟩
    · exact absurd h_zero h_m_zero_ne
    refine ⟨?_, ?_⟩
    · rw [← UInt64.le_iff_toNat_le]; exact h_min
    · rw [← UInt64.le_iff_toNat_le]; exact h_max
  have h_m_valid : m.mantissa_.toNat ≤ maxRep.toNat ∨ m.mantissa_.toNat % 10 = 0 := by
    rcases h_norm with h_zero | ⟨_, _, h_v, _, _⟩
    · exact absurd h_zero h_m_zero_ne
    rcases h_v with hv | hv
    · left; rw [← UInt64.le_iff_toNat_le]; exact hv
    · right; exact hv
  have h_m_min : (10^18 : ℕ) ≤ m.mantissa_.toNat := by
    have hmin_eq : largeRange.min.toNat = 10^18 := by decide
    omega
  have h_m_max : m.mantissa_.toNat ≤ (10^19 - 1 : ℕ) := by
    have hmax_eq : largeRange.max.toNat = 10^19 - 1 := by decide
    omega
  have h_m_lt : m.mantissa_.toNat < 10^19 := by omega
  have h_e_ge : Int.log 10 q - mantissaLog ≤ m.exponent_ :=
    exponent_ge_of_toRat_ge q hq m h_norm h_ge
  have h_ceil_bounds := m_ceil_nat_bounds q hq
  have h_real_bounds := m_real_bounds q hq
  unfold upperPosAux at h
  simp only at h
  set e_q : ℤ := Int.log 10 q - mantissaLog with he_q_def
  set m_real : ℚ := q * (10 : ℚ)^(-e_q) with hm_real_def
  set m_ceil_nat : ℕ := ⌈m_real⌉₊ with hm_ceil_def
  have h_pow_pos_eq : (0 : ℚ) < (10 : ℚ) ^ e_q := zpow_pos (by norm_num) _
  have h_q_eq : q = m_real * (10 : ℚ) ^ e_q := by
    rw [hm_real_def, mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    rw [show (-e_q + e_q : ℤ) = 0 from by ring]; simp
  have h_ge_mant : (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ ≥ q := by
    rw [← Number.toRat_of_nonneg m h_m_neg]; exact h_ge
  rw [Number.toRat_of_nonneg m h_m_neg]
  have h_corner_le_m : (⟨false, largeRange.min, minExponent⟩ : Number).toRat
      ≤ (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ := by
    have h_nn : (⟨false, largeRange.min, minExponent⟩ : Number).negative_ = false := rfl
    rw [Number.toRat_of_nonneg _ h_nn]
    change (largeRange.min.toNat : ℚ) * (10 : ℚ) ^ (minExponent : ℤ)
        ≤ (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_
    have hmin : (largeRange.min.toNat : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by
      have : largeRange.min.toNat = 10 ^ 18 := by decide
      rw [this]; push_cast; ring
    rw [hmin]
    have h_emin : minExponent ≤ m.exponent_ := by
      rcases h_norm with hz | ⟨_, _, _, h_emin, _⟩
      · exact absurd hz h_m_zero_ne
      · exact h_emin
    have h_pow_mono : (10 : ℚ) ^ (minExponent : ℤ) ≤ (10 : ℚ) ^ m.exponent_ :=
      zpow_le_zpow_right₀ (by norm_num) h_emin
    have h_pow18_pos : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
    have h_step1 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ)
        ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ m.exponent_ :=
      mul_le_mul_of_nonneg_left h_pow_mono (le_of_lt h_pow18_pos)
    have h_pow_em_pos : (0 : ℚ) < (10 : ℚ) ^ m.exponent_ := zpow_pos (by norm_num) _
    have h_mant_ge_cast : ((10^18 : ℕ) : ℚ) ≤ (m.mantissa_.toNat : ℚ) := by exact_mod_cast h_m_min
    have h_mant_ge_cast' : (10 : ℚ) ^ (18 : ℕ) ≤ (m.mantissa_.toNat : ℚ) := by
      have : ((10^18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by push_cast; ring
      rw [← this]; exact h_mant_ge_cast
    have h_step2 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ m.exponent_
        ≤ (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ :=
      mul_le_mul_of_nonneg_right h_mant_ge_cast' (le_of_lt h_pow_em_pos)
    linarith
  rcases h_bump_eq : bumpToValidMantissa m_ceil_nat with _ | m_out
  · rw [h_bump_eq] at h
    simp only at h
    split_ifs at h with hexp hcorner
    · have hn_eq : n = ⟨false, largeRange.min, e_q + 1⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      have h_nn : (⟨false, largeRange.min, e_q + 1⟩ : Number).negative_ = false := rfl
      rw [Number.toRat_of_nonneg _ h_nn]
      change (largeRange.min.toNat : ℚ) * (10 : ℚ) ^ (e_q + 1)
          ≤ (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_
      have hmin : (largeRange.min.toNat : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by
        have : largeRange.min.toNat = 10 ^ 18 := by decide
        rw [this]; push_cast; ring
      rw [hmin]
      have h_em_strict : m.exponent_ ≥ e_q + 1 := by
        by_contra h_not
        push_neg at h_not
        have h_em_eq : m.exponent_ = e_q := by omega
        have h_m_real_le_mant : m_real ≤ (m.mantissa_.toNat : ℚ) := by
          have h_toRat : (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ ≥ q := h_ge_mant
          rw [h_em_eq, h_q_eq] at h_toRat
          exact le_of_mul_le_mul_right h_toRat h_pow_pos_eq
        have h_ceil_le : m_ceil_nat ≤ m.mantissa_.toNat := Nat.ceil_le.mpr h_m_real_le_mant
        have hmax_val : maxRep.toNat = maxRepNat := maxRep_val
        have hcusp_val : cuspMin = maxRepCuspTarget := rfl
        have h_ceil_lower : m_ceil_nat ≥ 9999999999999999991 := by
          unfold bumpToValidMantissa at h_bump_eq
          by_cases hb1 : m_ceil_nat ≤ maxRep.toNat
          · rw [if_pos hb1] at h_bump_eq; exact absurd h_bump_eq (by simp)
          rw [if_neg hb1] at h_bump_eq
          by_cases hb2 : m_ceil_nat < cuspMin
          · rw [if_pos hb2] at h_bump_eq; exact absurd h_bump_eq (by simp)
          rw [if_neg hb2] at h_bump_eq
          simp only at h_bump_eq
          by_cases hb3 : ((m_ceil_nat + 9) / 10) * 10 < 10^19
          · rw [if_pos hb3] at h_bump_eq; exact absurd h_bump_eq (by simp)
          push_neg at hb3
          have h_div_eq : 10 * ((m_ceil_nat + 9) / 10) + (m_ceil_nat + 9) % 10 = m_ceil_nat + 9 :=
            Nat.div_add_mod (m_ceil_nat + 9) 10
          have h_mod_lt : (m_ceil_nat + 9) % 10 < 10 := Nat.mod_lt _ (by omega)
          omega
        have h_mant_gt : m.mantissa_.toNat > maxRep.toNat := by omega
        have h_mant_mod : m.mantissa_.toNat % 10 = 0 := by
          rcases h_m_valid with hv | hv
          · omega
          · exact hv
        omega
      have h_pow_mono : (10 : ℚ) ^ (e_q + 1) ≤ (10 : ℚ) ^ m.exponent_ :=
        zpow_le_zpow_right₀ (by norm_num) h_em_strict
      have h_pow18_pos : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
      have h_step1 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (e_q + 1)
          ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ m.exponent_ :=
        mul_le_mul_of_nonneg_left h_pow_mono (le_of_lt h_pow18_pos)
      have h_pow_em_pos : (0 : ℚ) < (10 : ℚ) ^ m.exponent_ := zpow_pos (by norm_num) _
      have h_mant_ge_cast : ((10^18 : ℕ) : ℚ) ≤ (m.mantissa_.toNat : ℚ) := by
        exact_mod_cast h_m_min
      have h_mant_ge_cast' : (10 : ℚ) ^ (18 : ℕ) ≤ (m.mantissa_.toNat : ℚ) := by
        have : ((10^18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by push_cast; ring
        rw [← this]; exact h_mant_ge_cast
      have h_step2 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ m.exponent_
          ≤ (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ :=
        mul_le_mul_of_nonneg_right h_mant_ge_cast' (le_of_lt h_pow_em_pos)
      linarith
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      exact h_corner_le_m
  · rw [h_bump_eq] at h
    simp only at h
    have h_m_out_valid : m_out ≤ maxRep.toNat ∨ m_out % 10 = 0 :=
      bumpToValidMantissa_valid m_ceil_nat m_out h_bump_eq
    have h_m_out_lt : m_out < 10 ^ 19 := bumpToValidMantissa_lt_pow19 m_ceil_nat m_out h_bump_eq
    have h_m_out_ge : m_ceil_nat ≤ m_out := bumpToValidMantissa_ge m_ceil_nat m_out h_bump_eq
    split_ifs at h with hexp hcorner
    pick_goal 2
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      exact h_corner_le_m
    have hn_eq : n = ⟨false, ⟨⟨⟨m_out, hexp.2.2⟩⟩⟩, e_q⟩ := (Option.some.inj h).symm
    rw [hn_eq]
    have h_nn : (⟨false, ⟨⟨⟨m_out, hexp.2.2⟩⟩⟩, e_q⟩ : Number).negative_ = false := rfl
    rw [Number.toRat_of_nonneg _ h_nn]
    change ((⟨⟨⟨m_out, hexp.2.2⟩⟩⟩ : UInt64).toNat : ℚ) * (10 : ℚ) ^ e_q
        ≤ (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_
    have h_toNat : ((⟨⟨⟨m_out, hexp.2.2⟩⟩⟩ : UInt64)).toNat = m_out := rfl
    rw [h_toNat]
    rcases lt_or_eq_of_le h_e_ge with h_em_gt | h_em_eq
    · have h_em_succ : e_q + 1 ≤ m.exponent_ := by linarith
      have h_m_out_lt_q : (m_out : ℚ) < (10 : ℚ) ^ (19 : ℕ) := by
        have : (m_out : ℚ) < ((10^19 : ℕ) : ℚ) := by exact_mod_cast h_m_out_lt
        push_cast at this; exact this
      have h_pow_em_pos : (0 : ℚ) < (10 : ℚ) ^ m.exponent_ := zpow_pos (by norm_num) _
      have h_mant_ge_cast : ((10^18 : ℕ) : ℚ) ≤ (m.mantissa_.toNat : ℚ) := by exact_mod_cast h_m_min
      have h_mant_ge_cast' : (10 : ℚ) ^ (18 : ℕ) ≤ (m.mantissa_.toNat : ℚ) := by
        have : ((10^18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by push_cast; ring
        rw [← this]; exact h_mant_ge_cast
      have h_pow_mono : (10 : ℚ) ^ (e_q + 1) ≤ (10 : ℚ) ^ m.exponent_ :=
        zpow_le_zpow_right₀ (by norm_num) h_em_succ
      have h_pow18_pos : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
      have h_pow_eq19 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (e_q + 1) = (10 : ℚ) ^ (e_q + 19) := by
        rw [← zpow_natCast (10 : ℚ) 18, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        congr 1; push_cast; ring
      have h_pow_eq19' : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ e_q = (10 : ℚ) ^ (e_q + 19) := by
        rw [← zpow_natCast (10 : ℚ) 19, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        congr 1; push_cast; ring
      have h_step1 : (m_out : ℚ) * (10 : ℚ) ^ e_q < (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ e_q :=
        mul_lt_mul_of_pos_right h_m_out_lt_q h_pow_pos_eq
      have h_step2 : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ e_q = (10 : ℚ) ^ (e_q + 19) := h_pow_eq19'
      have h_step3 : (10 : ℚ) ^ (e_q + 19) = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (e_q + 1) := h_pow_eq19.symm
      have h_step4 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (e_q + 1)
          ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ m.exponent_ :=
        mul_le_mul_of_nonneg_left h_pow_mono (le_of_lt h_pow18_pos)
      have h_step5 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ m.exponent_
          ≤ (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ :=
        mul_le_mul_of_nonneg_right h_mant_ge_cast' (le_of_lt h_pow_em_pos)
      linarith
    · have h_em_eq : m.exponent_ = e_q := h_em_eq.symm
      rw [h_em_eq]
      apply mul_le_mul_of_nonneg_right _ (le_of_lt h_pow_pos_eq)
      have h_m_real_le_mant : m_real ≤ (m.mantissa_.toNat : ℚ) := by
        have h_toRat : (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ ≥ q := h_ge_mant
        rw [h_em_eq, h_q_eq] at h_toRat
        exact le_of_mul_le_mul_right h_toRat h_pow_pos_eq
      have h_ceil_le : m_ceil_nat ≤ m.mantissa_.toNat := Nat.ceil_le.mpr h_m_real_le_mant
      have h_k_valid : m.mantissa_.toNat ≤ maxRep.toNat ∨
          (maxRep.toNat < m.mantissa_.toNat ∧ m.mantissa_.toNat % 10 = 0 ∧ m.mantissa_.toNat < 10^19) := by
        rcases h_m_valid with hv | hv
        · left; exact hv
        · by_cases h_le : m.mantissa_.toNat ≤ maxRep.toNat
          · left; exact h_le
          · push_neg at h_le
            right; exact ⟨h_le, hv, h_m_lt⟩
      have h_tight : m_out ≤ m.mantissa_.toNat :=
        bumpToValidMantissa_tight m_ceil_nat m_out h_bump_eq m.mantissa_.toNat h_k_valid h_ceil_le
      exact_mod_cast h_tight

/-! ## Helper: tightness of `lowerPosAux` for positive q -/

/-- Upper bound on exponent for a non-negative normalized Number below `q`. -/
lemma exponent_le_of_toRat_le_pos (q : ℚ) (_hq : 0 < q) (m : Number)
    (h_norm : m.isNormalized) (h_m_neg : m.negative_ = false)
    (h_m_zero_ne : m ≠ Number.zero) (h_le : m.toRat ≤ q) :
    m.exponent_ ≤ Int.log 10 q - mantissaLog := by
  rcases h_norm with h_zero | ⟨h_min, h_max, _, h_emin, h_emax⟩
  · exact absurd h_zero h_m_zero_ne
  have h_min_nat : (10^18 : ℕ) ≤ m.mantissa_.toNat := by
    have h := h_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10^18 := by decide
    omega
  rw [Number.toRat_of_nonneg m h_m_neg] at h_le
  have h_pow_em_pos : (0 : ℚ) < (10 : ℚ) ^ m.exponent_ := zpow_pos (by norm_num) _
  have h_mant_ge_cast : ((10^18 : ℕ) : ℚ) ≤ (m.mantissa_.toNat : ℚ) := by exact_mod_cast h_min_nat
  have h_mant_ge_cast' : (10 : ℚ) ^ (18 : ℕ) ≤ (m.mantissa_.toNat : ℚ) := by
    have : ((10^18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by push_cast; ring
    rw [← this]; exact h_mant_ge_cast
  have h_lower : (10 : ℚ) ^ (m.exponent_ + 18) ≤ m.mantissa_.toNat * (10 : ℚ) ^ m.exponent_ := by
    have h_pow_eq : (10 : ℚ) ^ (m.exponent_ + 18) = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ m.exponent_ := by
      rw [← zpow_natCast (10 : ℚ) 18, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      congr 1; push_cast; ring
    rw [h_pow_eq]
    exact mul_le_mul_of_nonneg_right h_mant_ge_cast' (le_of_lt h_pow_em_pos)
  have h_pow_le_q : (10 : ℚ) ^ (m.exponent_ + 18) ≤ q := le_trans h_lower h_le
  by_contra h_not
  push_neg at h_not
  have h_exp_lower : m.exponent_ + 18 ≥ Int.log 10 q + 1 := by linarith
  have h_pow_mono : (10 : ℚ) ^ (Int.log 10 q + 1) ≤ (10 : ℚ) ^ (m.exponent_ + 18) :=
    zpow_le_zpow_right₀ (by norm_num) h_exp_lower
  have h_q_lt : q < (10 : ℚ) ^ (Int.log 10 q + 1) := Int.lt_zpow_succ_log_self (by norm_num) q
  linarith

lemma lowerPosAux_tight (q : ℚ) (hq : 0 < q) (n : Number)
    (h : lowerPosAux q = some n) (m : Number) (h_norm : m.isNormalized)
    (h_le : m.toRat ≤ q) : m.toRat ≤ n.toRat := by
  by_cases h_m_neg : m.negative_ = false
  · by_cases h_m_zero : m = Number.zero
    · rw [h_m_zero, Number.toRat_zero]
      have h_n_neg : n.negative_ = false := by
        unfold lowerPosAux at h
        simp only at h
        split at h
        · split at h
          · exact congrArg (·.negative_) (Option.some.inj h).symm
          · -- else branch: (if e_q < minExp then some zero else none) = some n
            split at h
            · exact congrArg (·.negative_) (Option.some.inj h).symm  -- corner: n = zero
            · exact absurd h (by simp)
        · split at h
          · exact congrArg (·.negative_) (Option.some.inj h).symm
          · -- else branch: (if e_q - 1 < minExp then some zero else none) = some n
            split at h
            · exact congrArg (·.negative_) (Option.some.inj h).symm  -- corner: n = zero
            · exact absurd h (by simp)
      exact Number.toRat_nonneg_of_nonnegative n h_n_neg
    · -- m ≠ zero. Use exponent_le and case analysis.
      have h_e_le : m.exponent_ ≤ Int.log 10 q - mantissaLog :=
        exponent_le_of_toRat_le_pos q hq m h_norm h_m_neg h_m_zero h_le
      -- Extract mantissa bounds.
      rcases h_norm with h_zero | ⟨h_min, h_max, h_v, _h_emin, _h_emax⟩
      · exact absurd h_zero h_m_zero
      have h_min_nat : (10^18 : ℕ) ≤ m.mantissa_.toNat := by
        have h := h_min
        rw [UInt64.le_iff_toNat_le] at h
        have hmin_eq : largeRange.min.toNat = 10^18 := by decide
        omega
      have h_max_nat : m.mantissa_.toNat ≤ (10^19 - 1 : ℕ) := by
        have h := h_max
        rw [UInt64.le_iff_toNat_le] at h
        have hmax_eq : largeRange.max.toNat = 10^19 - 1 := by decide
        omega
      have h_m_lt : m.mantissa_.toNat < 10^19 := by omega
      have h_m_valid : m.mantissa_.toNat ≤ maxRep.toNat ∨ m.mantissa_.toNat % 10 = 0 := by
        rcases h_v with hv | hv
        · left; rw [← UInt64.le_iff_toNat_le]; exact hv
        · right; exact hv
      have h_real_bounds := m_real_bounds q hq
      unfold lowerPosAux at h
      simp only at h
      set e_q : ℤ := Int.log 10 q - mantissaLog with he_q_def
      set m_real : ℚ := q * (10 : ℚ)^(-e_q) with hm_real_def
      set m_floor_nat : ℕ := ⌊m_real⌋₊ with hm_floor_def
      have h_pow_pos_eq : (0 : ℚ) < (10 : ℚ) ^ e_q := zpow_pos (by norm_num) _
      have h_real_nn : (0 : ℚ) ≤ m_real := by
        have : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
        linarith [h_real_bounds.1]
      have h_floor_ge : (10 ^ 18 : ℕ) ≤ m_floor_nat := by
        have : ((10^18 : ℕ) : ℚ) ≤ m_real := by push_cast; exact h_real_bounds.1
        exact Nat.le_floor this
      have h_floor_lt : m_floor_nat < 10 ^ 19 := by
        have h_floor_le : (m_floor_nat : ℚ) ≤ m_real := Nat.floor_le h_real_nn
        have : (m_floor_nat : ℚ) < ((10^19 : ℕ) : ℚ) := by
          push_cast; linarith [h_real_bounds.2]
        exact_mod_cast this
      have h_q_eq : q = m_real * (10 : ℚ) ^ e_q := by
        rw [hm_real_def, mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        rw [show (-e_q + e_q : ℤ) = 0 from by ring]; simp
      rw [Number.toRat_of_nonneg m h_m_neg]
      have h_le_mant : (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_ ≤ q := by
        rw [← Number.toRat_of_nonneg m h_m_neg]; exact h_le
      set m_tr := truncToValidMantissa m_floor_nat with hm_tr_def
      split at h
      · rename_i hpos
        split at h
        · rename_i hexp
          have hn_eq : n = ⟨false, ⟨⟨⟨m_tr, hpos.2⟩⟩⟩, e_q⟩ := (Option.some.inj h).symm
          rw [hn_eq]
          have h_nn : (⟨false, ⟨⟨⟨m_tr, hpos.2⟩⟩⟩, e_q⟩ : Number).negative_ = false := rfl
          rw [Number.toRat_of_nonneg _ h_nn]
          change (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_
              ≤ ((⟨⟨⟨m_tr, hpos.2⟩⟩⟩ : UInt64).toNat : ℚ) * (10 : ℚ) ^ e_q
          have h_toNat : ((⟨⟨⟨m_tr, hpos.2⟩⟩⟩ : UInt64)).toNat = m_tr := rfl
          rw [h_toNat]
          rcases lt_or_eq_of_le h_e_le with h_em_lt | h_em_eq
          · have h_em_succ : m.exponent_ + 1 ≤ e_q := by linarith
            have h_pow_em_pos : (0 : ℚ) < (10 : ℚ) ^ m.exponent_ := zpow_pos (by norm_num) _
            have h_cast_lt : (m.mantissa_.toNat : ℚ) < (10 : ℚ) ^ (19 : ℕ) := by
              have : (m.mantissa_.toNat : ℚ) < ((10^19 : ℕ) : ℚ) := by exact_mod_cast h_m_lt
              push_cast at this; exact this
            have h_step1 : (m.mantissa_.toNat : ℚ) * (10 : ℚ) ^ m.exponent_
                < (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ m.exponent_ :=
              mul_lt_mul_of_pos_right h_cast_lt h_pow_em_pos
            have h_pow_eq19 : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ m.exponent_
                = (10 : ℚ) ^ (m.exponent_ + 19) := by
              rw [← zpow_natCast (10 : ℚ) 19, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
              congr 1; push_cast; ring
            have h_pow_mono : (10 : ℚ) ^ (m.exponent_ + 19) ≤ (10 : ℚ) ^ (e_q + 18) :=
              zpow_le_zpow_right₀ (by norm_num) (by linarith)
            have h_pow_eq18 : (10 : ℚ) ^ (e_q + 18) = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ e_q := by
              rw [← zpow_natCast (10 : ℚ) 18, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
              congr 1; push_cast; ring
            have h_tr_min : (10^18 : ℕ) ≤ m_tr := by
              have hmin_eq : largeRange.min.toNat = 10^18 := by decide
              have h1 : largeRange.min.toNat ≤ m_tr := hpos.1
              rw [hmin_eq] at h1
              exact h1
            have h_tr_min_cast : (10 : ℚ) ^ (18 : ℕ) ≤ (m_tr : ℚ) := by
              have : ((10^18 : ℕ) : ℚ) ≤ (m_tr : ℚ) := by exact_mod_cast h_tr_min
              have heq : ((10^18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by push_cast; ring
              linarith [heq]
            have h_step2 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ e_q
                ≤ (m_tr : ℚ) * (10 : ℚ) ^ e_q :=
              mul_le_mul_of_nonneg_right h_tr_min_cast (le_of_lt h_pow_pos_eq)
            linarith
          · rw [← h_em_eq]
            have h_pow_em_pos : (0 : ℚ) < (10 : ℚ) ^ m.exponent_ := zpow_pos (by norm_num) _
            apply mul_le_mul_of_nonneg_right _ (le_of_lt h_pow_em_pos)
            have h_mant_le_tr : m.mantissa_.toNat ≤ m_tr := by
              apply truncToValidMantissa_tight m_floor_nat m.mantissa_.toNat
              · by_cases h_le_mr : m.mantissa_.toNat ≤ maxRep.toNat
                · left; exact h_le_mr
                · push_neg at h_le_mr
                  right
                  refine ⟨h_le_mr, ?_, h_m_lt⟩
                  rcases h_m_valid with h_v | h_v
                  · exact absurd h_v (not_le.mpr h_le_mr)
                  · exact h_v
              · have h_le_real : (m.mantissa_.toNat : ℚ) ≤ m_real := by
                  rw [hm_real_def]
                  have h_le_mant' : (m.mantissa_.toNat : ℚ) * 10^e_q ≤ q := by
                    rw [← h_em_eq]; exact h_le_mant
                  have h_pow_ne : (10 : ℚ)^e_q ≠ 0 := ne_of_gt h_pow_pos_eq
                  have : (m.mantissa_.toNat : ℚ) ≤ q / (10 : ℚ)^e_q := by
                    rw [le_div_iff₀ h_pow_pos_eq]; exact h_le_mant'
                  calc (m.mantissa_.toNat : ℚ) ≤ q / (10 : ℚ)^e_q := this
                    _ = q * (10 : ℚ)^(-e_q) := by
                          rw [zpow_neg]; ring
                exact Nat.le_floor h_le_real
            exact_mod_cast h_mant_le_tr
        · split_ifs at h with hcorner
          exfalso; omega
      · rename_i _hpos_neg
        exfalso
        apply _hpos_neg
        have h_min : largeRange.min.toNat = 10^18 := by decide
        have h_maxR : maxRep.toNat = maxRepNat := maxRep_val
        have h_cusp_v : cuspMin = maxRepCuspTarget := by decide
        have h_pow18_v : (10 : ℕ)^18 = 1000000000000000000 := by decide
        have h_pow19_v : (10 : ℕ)^19 = tenPow19 := by decide
        have h_pow64 : (2 : ℕ)^64 = twoPow64 := by decide
        have h_tr_ge_pow18 : truncToValidMantissa m_floor_nat ≥ 10^18 := by
          by_cases h1 : m_floor_nat ≤ maxRep.toNat
          · have h_eq : truncToValidMantissa m_floor_nat = m_floor_nat := by
              unfold truncToValidMantissa; rw [if_pos h1]
            rw [h_eq]; exact h_floor_ge
          · push_neg at h1
            by_cases h2 : m_floor_nat < cuspMin
            · have h_eq : truncToValidMantissa m_floor_nat = maxRep.toNat := by
                unfold truncToValidMantissa
                rw [if_neg (by omega), if_pos h2]
              rw [h_eq]; omega
            · push_neg at h2
              have h_eq : truncToValidMantissa m_floor_nat = (m_floor_nat / 10) * 10 := by
                unfold truncToValidMantissa
                rw [if_neg (by omega), if_neg (by omega)]
              rw [h_eq]
              have h_div_ge : cuspMin / 10 ≤ m_floor_nat / 10 := Nat.div_le_div_right h2
              have h_cusp_div_v : cuspMin / 10 = mantissaFloorSucc := cuspMin_div_ten
              have h_lower : mantissaFloorSucc ≤ m_floor_nat / 10 := by omega
              have h_mul : maxRepCuspTarget ≤ m_floor_nat / 10 * 10 := by
                have h_eq2 : mantissaFloorSucc * 10 = maxRepCuspTarget := by decide
                calc maxRepCuspTarget = mantissaFloorSucc * 10 := h_eq2.symm
                  _ ≤ m_floor_nat / 10 * 10 := Nat.mul_le_mul_right 10 h_lower
              omega
        -- Establish m_tr < 2^64
        have h_tr_lt_64 : truncToValidMantissa m_floor_nat < 2^64 := by
          have h_le : truncToValidMantissa m_floor_nat ≤ m_floor_nat := truncToValidMantissa_le _
          omega
        refine ⟨?_, ?_⟩
        · rw [h_min]; exact h_tr_ge_pow18
        · exact h_tr_lt_64
  · push_neg at h_m_neg
    have h_m_neg' : m.negative_ = true := by
      cases hmm : m.negative_
      · exact absurd hmm h_m_neg
      · rfl
    have h_m_le_zero : m.toRat ≤ 0 := Number.toRat_nonpos_of_negative m h_m_neg'
    have h_n_neg : n.negative_ = false := by
      unfold lowerPosAux at h
      simp only at h
      split at h
      · split at h
        · exact congrArg (·.negative_) (Option.some.inj h).symm
        · split_ifs at h with hcorner
          exact congrArg (·.negative_) (Option.some.inj h).symm
      · split at h
        · exact congrArg (·.negative_) (Option.some.inj h).symm
        · split_ifs at h with hcorner
          exact congrArg (·.negative_) (Option.some.inj h).symm
    have h_n_nn : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n h_n_neg
    linarith

/-- `Number.lower q` returns a normalized Number when defined. -/
lemma Number.lower_isNormalized (q : ℚ) (n : Number) (h : Number.lower q = some n) :
    n.isNormalized := by
  unfold Number.lower at h
  split_ifs at h with hq0 hqneg
  · have : n = Number.zero := (Option.some.inj h).symm
    left; exact this
  · rw [Option.map_eq_some_iff] at h
    obtain ⟨n', hn', heq⟩ := h
    have hnq : 0 < -q := by linarith
    have h_n'_right := upperPosAux_isNormalized_right (-q) hnq n' hn'
    rw [← heq]
    right
    exact h_n'_right
  · have hq_pos : 0 < q := by
      rcases lt_trichotomy q 0 with h1 | h1 | h1
      · exact absurd h1 hqneg
      · exact absurd h1 hq0
      · exact h1
    exact lowerPosAux_isNormalized q hq_pos n h

/-- `Number.lower q` is at most `q`. -/
lemma Number.lower_le (q : ℚ) (n : Number) (h : Number.lower q = some n) :
    n.toRat ≤ q := by
  unfold Number.lower at h
  split_ifs at h with hq0 hqneg
  · have : n = Number.zero := (Option.some.inj h).symm
    rw [this, hq0, Number.toRat_zero]
  · rw [Option.map_eq_some_iff] at h
    obtain ⟨n', hn', heq⟩ := h
    have hnq : 0 < -q := by linarith
    have h_n'_ge : -q ≤ n'.toRat := upperPosAux_ge (-q) hnq n' hn'
    rw [← heq]
    have h_n_neg : ({ n' with negative_ := true } : Number).negative_ = true := rfl
    rw [Number.toRat_of_neg _ h_n_neg]
    have h_n'_neg : n'.negative_ = false := by
      unfold upperPosAux at hn'
      simp only at hn'
      rcases h_bump : bumpToValidMantissa
          ⌈(-q) * (10 : ℚ)^(-(Int.log 10 (-q) - mantissaLog))⌉₊ with _ | m
      · rw [h_bump] at hn'; simp only at hn'
        split_ifs at hn' <;>
          exact congrArg (·.negative_) (Option.some.inj hn').symm
      · rw [h_bump] at hn'; simp only at hn'
        split_ifs at hn' <;>
          exact congrArg (·.negative_) (Option.some.inj hn').symm
    rw [Number.toRat_of_nonneg _ h_n'_neg] at h_n'_ge
    change -((n'.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n'.exponent_) ≤ q
    linarith
  · have hq_pos : 0 < q := by
      rcases lt_trichotomy q 0 with h1 | h1 | h1
      · exact absurd h1 hqneg
      · exact absurd h1 hq0
      · exact h1
    exact lowerPosAux_le q hq_pos n h

/-- `Number.lower q` is the largest representable Number ≤ q. -/
lemma Number.lower_tight (q : ℚ) (n : Number) (h_lower : Number.lower q = some n)
    (m : Number) (h_norm : m.isNormalized) (h_le : m.toRat ≤ q) :
    m.toRat ≤ n.toRat := by
  unfold Number.lower at h_lower
  split_ifs at h_lower with hq0 hqneg
  · have h_eq : n = Number.zero := (Option.some.inj h_lower).symm
    rw [h_eq, Number.toRat_zero]; rw [hq0] at h_le; exact h_le
  · rw [Option.map_eq_some_iff] at h_lower
    obtain ⟨n', hn', heq⟩ := h_lower
    have hnq : 0 < -q := by linarith
    have h_m_neg : m.negative_ = true := by
      by_contra h_not
      have h_nn : m.negative_ = false := by
        cases hh : m.negative_
        · rfl
        · exact absurd hh h_not
      have h_m_nn : 0 ≤ m.toRat := Number.toRat_nonneg_of_nonnegative m h_nn
      linarith
    set m' := ({m with negative_ := false} : Number) with hm'_def
    have h_m'_eq : m'.toRat = -m.toRat := by
      have h := Number.toRat_set_neg_false_of_neg m h_m_neg
      rw [← hm'_def] at h; exact h
    have h_m'_norm : m'.isNormalized := by
      rcases h_norm with hz | ⟨hmin, hmax, hv, hemin, hemax⟩
      · exfalso
        have h_zero : m.toRat = 0 := by rw [hz, Number.toRat_zero]
        linarith
      · right; exact ⟨hmin, hmax, hv, hemin, hemax⟩
    have h_m'_ge : -q ≤ m'.toRat := by rw [h_m'_eq]; linarith
    have h_n'_le : n'.toRat ≤ m'.toRat :=
      upperPosAux_tight (-q) hnq n' hn' m' h_m'_norm h_m'_ge
    have h_n'_pos : n'.negative_ = false := by
      unfold upperPosAux at hn'
      simp only at hn'
      rcases h_bump : bumpToValidMantissa
          ⌈(-q) * (10 : ℚ)^(-(Int.log 10 (-q) - mantissaLog))⌉₊ with _ | mm
      · rw [h_bump] at hn'; simp only at hn'
        split_ifs at hn' with hexp hcorner
        · exact congrArg (·.negative_) (Option.some.inj hn').symm
        · exact congrArg (·.negative_) (Option.some.inj hn').symm
      · rw [h_bump] at hn'; simp only at hn'
        split_ifs at hn' with hexp hcorner
        · exact congrArg (·.negative_) (Option.some.inj hn').symm
        · exact congrArg (·.negative_) (Option.some.inj hn').symm
    rw [← heq]
    have h_n_eq : ({n' with negative_ := true} : Number).toRat = -n'.toRat := by
      have h := Number.toRat_set_neg_true_of_nn n' h_n'_pos
      exact h
    rw [h_n_eq]
    rw [h_m'_eq] at h_n'_le
    linarith
  · have hq_pos : 0 < q := by
      rcases lt_trichotomy q 0 with h1 | h1 | h1
      · exact absurd h1 hqneg
      · exact absurd h1 hq0
      · exact h1
    exact lowerPosAux_tight q hq_pos n h_lower m h_norm h_le

/-- `Number.upper q` is the smallest representable Number ≥ q. -/
lemma Number.upper_tight (q : ℚ) (n : Number) (h_upper : Number.upper q = some n)
    (m : Number) (h_norm : m.isNormalized) (h_ge : q ≤ m.toRat) :
    n.toRat ≤ m.toRat := by
  unfold Number.upper at h_upper
  split_ifs at h_upper with hq0 hqneg
  · have h_eq : n = Number.zero := (Option.some.inj h_upper).symm
    rw [h_eq, Number.toRat_zero]; rw [hq0] at h_ge; exact h_ge
  · rw [Option.map_eq_some_iff] at h_upper
    obtain ⟨n', hn', heq⟩ := h_upper
    have hnq : 0 < -q := by linarith
    have h_n'_pos : n'.negative_ = false := by
      unfold lowerPosAux at hn'
      simp only at hn'
      split at hn'
      · split at hn'
        · exact congrArg (·.negative_) (Option.some.inj hn').symm
        · split at hn'
          · exact congrArg (·.negative_) (Option.some.inj hn').symm
          · exact absurd hn' (by simp)
      · split at hn'
        · exact congrArg (·.negative_) (Option.some.inj hn').symm
        · split at hn'
          · exact congrArg (·.negative_) (Option.some.inj hn').symm
          · exact absurd hn' (by simp)
    have h_n_toRat : ({n' with negative_ := true} : Number).toRat = -n'.toRat := by
      exact Number.toRat_set_neg_true_of_nn n' h_n'_pos
    by_cases h_m_pos : m.negative_ = false
    · have h_m_nn : 0 ≤ m.toRat := Number.toRat_nonneg_of_nonnegative m h_m_pos
      have h_n'_nn : 0 ≤ n'.toRat := Number.toRat_nonneg_of_nonnegative n' h_n'_pos
      rw [← heq]
      split_ifs with h_nz
      · rw [Number.toRat_zero]; exact h_m_nn
      · rw [h_n_toRat]; linarith
    · have h_m_neg : m.negative_ = true := by
        cases hh : m.negative_
        · exact absurd hh h_m_pos
        · rfl
      set m' := ({m with negative_ := false} : Number) with hm'_def
      have h_m'_eq : m'.toRat = -m.toRat := by
        have h := Number.toRat_set_neg_false_of_neg m h_m_neg
        rw [← hm'_def] at h; exact h
      have h_m'_norm : m'.isNormalized := by
        rcases h_norm with hz | ⟨hmin, hmax, hv, hemin, hemax⟩
        · exfalso
          have h_neg_z : m.negative_ = false := by rw [hz]; rfl
          rw [h_neg_z] at h_m_neg; exact absurd h_m_neg (by simp)
        · right; exact ⟨hmin, hmax, hv, hemin, hemax⟩
      have h_m'_le : m'.toRat ≤ -q := by rw [h_m'_eq]; linarith
      have h_m'_le_n' : m'.toRat ≤ n'.toRat :=
        lowerPosAux_tight (-q) hnq n' hn' m' h_m'_norm h_m'_le
      rw [h_m'_eq] at h_m'_le_n'
      rw [← heq]
      split_ifs with h_nz
      · subst h_nz
        simp only [Number.toRat_zero] at h_m'_le_n'
        rw [Number.toRat_zero]
        linarith
      · rw [h_n_toRat]; linarith
  · have hq_pos : 0 < q := by
      rcases lt_trichotomy q 0 with h1 | h1 | h1
      · exact absurd h1 hqneg
      · exact absurd h1 hq0
      · exact h1
    exact upperPosAux_tight q hq_pos n h_upper m h_norm h_ge


end XRPL.Model.Protocol

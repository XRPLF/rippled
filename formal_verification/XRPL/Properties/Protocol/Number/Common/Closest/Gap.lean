import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Closest.Tightness


namespace XRPL.Model.Protocol

/-! ## Gap-bound lemma

The gap between `Number.upper q` and `Number.lower q` is bounded by `10 × 10^e`
where `e` is the normalized exponent of `q`. This loose bound covers all cases:
single mantissa unit (gap = 1), cusp jump (gap = 3), and above-cusp grid (gap ≤ 10). -/

/-! ### Helper: mantissa-level gap bound -/

/-! ## No normalized Number in an open ULP-grid gap -/

/-- No positive normalized Number's `toRat` lies strictly between consecutive
grid points `k · 10^e` and `(k+1) · 10^e` for `k ∈ [10^18, 10^19)`. -/
theorem no_normalized_in_open_ulp_gap_pos
    (e : Int) (k : ℕ)
    (_h_e_min : minExponent ≤ e) (_h_e_max : e ≤ maxExponent)
    (h_k_min : (10 : ℕ) ^ 18 ≤ k) (_h_k_max : k < (10 : ℕ) ^ 19)
    (n : Number) (h_norm : n.isNormalized)
    (h_n_pos : 0 < n.toRat)
    (h_lt_lo : (k : ℚ) * 10 ^ e < n.toRat)
    (h_lt_hi : n.toRat < ((k : ℚ) + 1) * 10 ^ e) :
    False := by
  have h_neg_false : n.negative_ = false := by
    rcases hn : n.negative_ with _ | _
    · rfl
    · exfalso
      have : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n hn
      linarith
  have h_m_ne : n.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_toRat_ne_zero h_n_pos.ne'
  rcases h_norm with h_zero | ⟨h_min, h_max, _h_valid, h_emin, _h_emax⟩
  · exfalso; apply h_m_ne; rw [h_zero]; rfl
  have h_m_min : (10 : ℕ) ^ 18 ≤ n.mantissa_.toNat := by
    have h := h_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10 ^ 18 := by decide
    omega
  have h_m_max : n.mantissa_.toNat ≤ (10 : ℕ) ^ 19 - 1 := by
    have h := h_max
    rw [UInt64.le_iff_toNat_le] at h
    have hmax_eq : largeRange.max.toNat = 10 ^ 19 - 1 := by decide
    omega
  have h_m_lt : n.mantissa_.toNat < (10 : ℕ) ^ 19 := by omega
  rw [Number.toRat_of_nonneg n h_neg_false] at h_lt_lo h_lt_hi
  have h_pow_e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  by_cases h_exp_ge : e ≤ n.exponent_
  · have h_diff_nn : (0 : ℤ) ≤ n.exponent_ - e := by linarith
    set d : ℕ := (n.exponent_ - e).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = n.exponent_ - e := Int.toNat_of_nonneg h_diff_nn
    have h_pow_split : (10 : ℚ) ^ n.exponent_ = (10 : ℚ) ^ d * (10 : ℚ) ^ e := by
      have h_exp_eq : n.exponent_ = (d : ℤ) + e := by
        rw [h_d_cast]; ring
      rw [h_exp_eq]
      rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    set N : ℕ := n.mantissa_.toNat * 10 ^ d with hN_def
    have h_n_toRat_eq : (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        = (N : ℚ) * (10 : ℚ) ^ e := by
      rw [hN_def, h_pow_split]
      push_cast
      ring
    rw [h_n_toRat_eq] at h_lt_lo h_lt_hi
    have h_k_lt_N : (k : ℚ) < (N : ℚ) := lt_of_mul_lt_mul_right h_lt_lo (le_of_lt h_pow_e_pos)
    have h_N_lt : (N : ℚ) < (k : ℚ) + 1 := lt_of_mul_lt_mul_right h_lt_hi (le_of_lt h_pow_e_pos)
    have h_k_lt_N_nat : k < N := by exact_mod_cast h_k_lt_N
    have h_N_lt_nat : N < k + 1 := by
      have : (N : ℚ) < ((k + 1 : ℕ) : ℚ) := by push_cast; exact h_N_lt
      exact_mod_cast this
    omega
  · push_neg at h_exp_ge
    have h_exp_le : n.exponent_ ≤ e - 1 := by linarith
    have h_pow_mono : (10 : ℚ) ^ n.exponent_ ≤ (10 : ℚ) ^ (e - 1) := by
      apply zpow_le_zpow_right₀ (by norm_num : (1 : ℚ) ≤ 10) h_exp_le
    have h_m_lt_q : (n.mantissa_.toNat : ℚ) < (10 : ℚ) ^ (19 : ℕ) := by
      have : (n.mantissa_.toNat : ℚ) < ((10 ^ 19 : ℕ) : ℚ) := by exact_mod_cast h_m_lt
      push_cast at this; exact this
    have h_pow18_pos : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
    have h_pow_e_pred_pos : (0 : ℚ) < (10 : ℚ) ^ (e - 1) := zpow_pos (by norm_num) _
    have h_pow_n_exp_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    have h_step1 : (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        ≤ (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ (e - 1) :=
      mul_le_mul_of_nonneg_left h_pow_mono (Nat.cast_nonneg _)
    have h_step2 : (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ (e - 1)
        < (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ (e - 1) :=
      mul_lt_mul_of_pos_right h_m_lt_q h_pow_e_pred_pos
    have h_pow_eq : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ (e - 1) = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ e := by
      rw [← zpow_natCast (10 : ℚ) 19, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0),
          ← zpow_natCast (10 : ℚ) 18, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      congr 1; push_cast; ring
    have h_k_ge_q : ((10 : ℚ) ^ (18 : ℕ)) ≤ (k : ℚ) := by
      have : ((10 ^ 18 : ℕ) : ℚ) ≤ (k : ℚ) := by exact_mod_cast h_k_min
      push_cast at this; exact this
    have h_step3 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ e ≤ (k : ℚ) * (10 : ℚ) ^ e :=
      mul_le_mul_of_nonneg_right h_k_ge_q (le_of_lt h_pow_e_pos)
    linarith

/-! ### Stronger gap lemmas for the post-scaleDown range

After `scaleDown128`, `zm.toNat ∈ [mantissaFloor, maxRep]`, which extends
below `10^18`. These lemmas use the cusp invariant of `isNormalized` to exclude
in-between candidates at exponent `e - 1`. -/

/-- No mantissa `m ∈ (10k, 10k+10)` satisfies the cusp invariant when `k ≥ cuspMin/10`. -/
private lemma no_valid_mantissa_at_eminus1 (k m : ℕ)
    (h_k_min : mantissaFloorSucc ≤ k)
    (h_lo : 10 * k < m) (h_hi : m < 10 * k + 10)
    (h_cusp : m ≤ maxRep.toNat ∨ m % 10 = 0) :
    False := by
  have h_maxR : maxRep.toNat = maxRepNat := maxRep_val
  have h_10k_ge : 10 * k ≥ maxRepCuspTarget := by omega
  have h_m_gt_maxRep : maxRep.toNat < m := by omega
  rcases h_cusp with h_le | h_div
  · omega
  · have h_10k_mod : (10 * k) % 10 = 0 := Nat.mul_mod_right 10 k
    omega

/-- No positive normalized Number's `toRat` lies strictly between `k · 10^e` and
`(k+1) · 10^e` for `k ∈ [mantissaFloorSucc, 10^19)`. Uses the cusp invariant. -/
theorem no_normalized_in_open_ulp_gap_pos_zm
    (e : Int) (k : ℕ)
    (h_k_min : mantissaFloorSucc ≤ k) (_h_k_max : k < (10 : ℕ) ^ 19)
    (n : Number) (h_norm : n.isNormalized)
    (h_n_pos : 0 < n.toRat)
    (h_lt_lo : (k : ℚ) * 10 ^ e < n.toRat)
    (h_lt_hi : n.toRat < ((k : ℚ) + 1) * 10 ^ e) :
    False := by
  have h_neg_false : n.negative_ = false := by
    rcases hn : n.negative_ with _ | _
    · rfl
    · exfalso
      have : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n hn
      linarith
  have h_m_ne : n.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_toRat_ne_zero h_n_pos.ne'
  rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, _h_emax⟩
  · exfalso; apply h_m_ne; rw [h_zero]; rfl
  have h_m_min : (10 : ℕ) ^ 18 ≤ n.mantissa_.toNat := by
    have h := h_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10 ^ 18 := by decide
    omega
  have h_m_max : n.mantissa_.toNat ≤ (10 : ℕ) ^ 19 - 1 := by
    have h := h_max
    rw [UInt64.le_iff_toNat_le] at h
    have hmax_eq : largeRange.max.toNat = 10 ^ 19 - 1 := by decide
    omega
  have h_m_lt : n.mantissa_.toNat < (10 : ℕ) ^ 19 := by omega
  have h_cusp : n.mantissa_.toNat ≤ maxRep.toNat ∨ n.mantissa_.toNat % 10 = 0 := by
    rcases h_valid with hle | hmod
    · left; exact UInt64.le_iff_toNat_le.mp hle
    · right; exact hmod
  rw [Number.toRat_of_nonneg n h_neg_false] at h_lt_lo h_lt_hi
  have h_pow_e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  by_cases h_exp_ge : e ≤ n.exponent_
  · have h_diff_nn : (0 : ℤ) ≤ n.exponent_ - e := by linarith
    set d : ℕ := (n.exponent_ - e).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = n.exponent_ - e := Int.toNat_of_nonneg h_diff_nn
    have h_pow_split : (10 : ℚ) ^ n.exponent_ = (10 : ℚ) ^ d * (10 : ℚ) ^ e := by
      have h_exp_eq : n.exponent_ = (d : ℤ) + e := by
        rw [h_d_cast]; ring
      rw [h_exp_eq]
      rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    set N : ℕ := n.mantissa_.toNat * 10 ^ d with hN_def
    have h_n_toRat_eq : (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        = (N : ℚ) * (10 : ℚ) ^ e := by
      rw [hN_def, h_pow_split]
      push_cast
      ring
    rw [h_n_toRat_eq] at h_lt_lo h_lt_hi
    have h_k_lt_N : (k : ℚ) < (N : ℚ) := lt_of_mul_lt_mul_right h_lt_lo (le_of_lt h_pow_e_pos)
    have h_N_lt : (N : ℚ) < (k : ℚ) + 1 := lt_of_mul_lt_mul_right h_lt_hi (le_of_lt h_pow_e_pos)
    have h_k_lt_N_nat : k < N := by exact_mod_cast h_k_lt_N
    have h_N_lt_nat : N < k + 1 := by
      have : (N : ℚ) < ((k + 1 : ℕ) : ℚ) := by push_cast; exact h_N_lt
      exact_mod_cast this
    omega
  · push_neg at h_exp_ge
    have h_exp_le : n.exponent_ ≤ e - 1 := by linarith
    have h_diff_pos : (0 : ℤ) < e - n.exponent_ := by linarith
    set d : ℕ := (e - n.exponent_).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = e - n.exponent_ := Int.toNat_of_nonneg (by linarith)
    have h_d_pos : 0 < d := by
      have : (0 : ℤ) < (d : ℤ) := by rw [h_d_cast]; exact h_diff_pos
      exact_mod_cast this
    have h_pow_split : (10 : ℚ) ^ e = (10 : ℚ) ^ d * (10 : ℚ) ^ n.exponent_ := by
      have h_exp_eq : e = (d : ℤ) + n.exponent_ := by rw [h_d_cast]; ring
      rw [h_exp_eq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    have h_pow_d_pos : (0 : ℚ) < (10 : ℚ) ^ d := by positivity
    have h_pow_n_exp_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    have h_k_10d_lt : (k : ℚ) * (10 : ℚ) ^ d < (n.mantissa_.toNat : ℚ) := by
      have h_step : (k : ℚ) * 10 ^ e = (k : ℚ) * 10 ^ d * (10 : ℚ) ^ n.exponent_ := by
        rw [h_pow_split]; ring
      rw [h_step] at h_lt_lo
      exact lt_of_mul_lt_mul_right h_lt_lo (le_of_lt h_pow_n_exp_pos)
    have h_m_lt_k1_10d : (n.mantissa_.toNat : ℚ) < ((k : ℚ) + 1) * (10 : ℚ) ^ d := by
      have h_step : ((k : ℚ) + 1) * 10 ^ e = ((k : ℚ) + 1) * 10 ^ d * (10 : ℚ) ^ n.exponent_ := by
        rw [h_pow_split]; ring
      rw [h_step] at h_lt_hi
      exact lt_of_mul_lt_mul_right h_lt_hi (le_of_lt h_pow_n_exp_pos)
    have h_k_10d_lt_nat : k * 10 ^ d < n.mantissa_.toNat := by
      have hq : ((k * 10 ^ d : ℕ) : ℚ) < ((n.mantissa_.toNat : ℕ) : ℚ) := by
        push_cast; exact h_k_10d_lt
      exact_mod_cast hq
    have h_m_lt_k1_10d_nat : n.mantissa_.toNat < (k + 1) * 10 ^ d := by
      have hq : ((n.mantissa_.toNat : ℕ) : ℚ) < (((k + 1) * 10 ^ d : ℕ) : ℚ) := by
        push_cast; linarith
      exact_mod_cast hq
    by_cases h_d_eq_1 : d = 1
    · rw [h_d_eq_1] at h_k_10d_lt_nat h_m_lt_k1_10d_nat
      have h_10k_lt : 10 * k < n.mantissa_.toNat := by
        have : k * 10 ^ 1 = 10 * k := by ring
        rw [this] at h_k_10d_lt_nat; exact h_k_10d_lt_nat
      have h_lt_10k10 : n.mantissa_.toNat < 10 * k + 10 := by
        have : (k + 1) * 10 ^ 1 = 10 * k + 10 := by ring
        rw [this] at h_m_lt_k1_10d_nat; exact h_m_lt_k1_10d_nat
      exact no_valid_mantissa_at_eminus1 k n.mantissa_.toNat h_k_min h_10k_lt h_lt_10k10 h_cusp
    · have h_d_ge_2 : 2 ≤ d := by omega
      have h_pow_ge : (100 : ℕ) ≤ 10 ^ d := by
        calc (100 : ℕ) = 10 ^ 2 := by norm_num
          _ ≤ 10 ^ d := Nat.pow_le_pow_right (by norm_num) h_d_ge_2
      have h_k_10d_big : (10 : ℕ) ^ 19 ≤ k * 10 ^ d := by
        have h1 : mantissaFloorSucc * 100 ≤ k * 10 ^ d :=
          Nat.mul_le_mul h_k_min h_pow_ge
        have h2 : (10 : ℕ) ^ 19 ≤ mantissaFloorSucc * 100 := by decide
        omega
      omega

/-- Negative analog of `no_normalized_in_open_ulp_gap_pos_zm`. -/
theorem no_normalized_in_open_ulp_gap_neg_zm
    (e : Int) (k : ℕ)
    (h_k_min : mantissaFloorSucc ≤ k) (h_k_max : k < (10 : ℕ) ^ 19)
    (n : Number) (h_norm : n.isNormalized)
    (h_n_neg : n.toRat < 0)
    (h_lt_lo : -(((k : ℚ) + 1) * 10 ^ e) < n.toRat)
    (h_lt_hi : n.toRat < -((k : ℚ) * 10 ^ e)) :
    False := by
  have h_n_neg_flag : n.negative_ = true := by
    rcases hn : n.negative_ with _ | _
    · exfalso
      have : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n hn
      linarith
    · rfl
  set n' : Number := { n with negative_ := false } with hn'_def
  have h_n'_neg_false : n'.negative_ = false := rfl
  have h_n'_mant : n'.mantissa_ = n.mantissa_ := rfl
  have h_n'_exp : n'.exponent_ = n.exponent_ := rfl
  have h_n'_toRat_eq : n'.toRat = -n.toRat := by
    rw [Number.toRat_of_nonneg n' h_n'_neg_false]
    rw [Number.toRat_of_neg n h_n_neg_flag]
    rw [h_n'_mant, h_n'_exp]
    ring
  have h_n'_norm : n'.isNormalized := by
    rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, h_emax⟩
    · exfalso
      have : n.negative_ = false := by rw [h_zero]; rfl
      rw [this] at h_n_neg_flag
      exact Bool.false_ne_true h_n_neg_flag
    · right
      refine ⟨?_, ?_, ?_, ?_, ?_⟩
      · rw [h_n'_mant]; exact h_min
      · rw [h_n'_mant]; exact h_max
      · rw [h_n'_mant]; exact h_valid
      · rw [h_n'_exp]; exact h_emin
      · rw [h_n'_exp]; exact h_emax
  have h_n'_pos : 0 < n'.toRat := by rw [h_n'_toRat_eq]; linarith
  have h_lt_lo' : (k : ℚ) * 10 ^ e < n'.toRat := by rw [h_n'_toRat_eq]; linarith
  have h_lt_hi' : n'.toRat < ((k : ℚ) + 1) * 10 ^ e := by rw [h_n'_toRat_eq]; linarith
  exact no_normalized_in_open_ulp_gap_pos_zm e k h_k_min h_k_max n' h_n'_norm
    h_n'_pos h_lt_lo' h_lt_hi'

/-- Gap lemma for `k = mantissaFloor` (post-scaleDown floor). Requires the extra
lower bound `(k + 8/10) * 10^e ≤ n.toRat` to exclude mantissa candidates
`9223372036854775801..maxRepNat` at exponent `e - 1`. -/
theorem no_normalized_in_ulp_gap_at_floor_pos
    (e : Int) (k : ℕ)
    (h_k_eq : k = mantissaFloor) (_h_k_max : k < (10 : ℕ) ^ 19)
    (n : Number) (h_norm : n.isNormalized)
    (h_n_pos : 0 < n.toRat)
    (h_lt_lo : ((k : ℚ) + (8 : ℚ) / 10) * 10 ^ e ≤ n.toRat)
    (h_lt_hi : n.toRat < ((k : ℚ) + 1) * 10 ^ e) :
    False := by
  have h_neg_false : n.negative_ = false := by
    rcases hn : n.negative_ with _ | _
    · rfl
    · exfalso
      have : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n hn
      linarith
  have h_m_ne : n.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_toRat_ne_zero h_n_pos.ne'
  rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, _h_emax⟩
  · exfalso; apply h_m_ne; rw [h_zero]; rfl
  have h_m_min : (10 : ℕ) ^ 18 ≤ n.mantissa_.toNat := by
    have h := h_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10 ^ 18 := by decide
    omega
  have h_m_max : n.mantissa_.toNat ≤ (10 : ℕ) ^ 19 - 1 := by
    have h := h_max
    rw [UInt64.le_iff_toNat_le] at h
    have hmax_eq : largeRange.max.toNat = 10 ^ 19 - 1 := by decide
    omega
  have h_m_lt : n.mantissa_.toNat < (10 : ℕ) ^ 19 := by omega
  have h_cusp : n.mantissa_.toNat ≤ maxRep.toNat ∨ n.mantissa_.toNat % 10 = 0 := by
    rcases h_valid with hle | hmod
    · left; exact UInt64.le_iff_toNat_le.mp hle
    · right; exact hmod
  rw [Number.toRat_of_nonneg n h_neg_false] at h_lt_lo h_lt_hi
  have h_pow_e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  by_cases h_exp_ge : e ≤ n.exponent_
  · have h_diff_nn : (0 : ℤ) ≤ n.exponent_ - e := by linarith
    set d : ℕ := (n.exponent_ - e).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = n.exponent_ - e := Int.toNat_of_nonneg h_diff_nn
    have h_pow_split : (10 : ℚ) ^ n.exponent_ = (10 : ℚ) ^ d * (10 : ℚ) ^ e := by
      have h_exp_eq : n.exponent_ = (d : ℤ) + e := by rw [h_d_cast]; ring
      rw [h_exp_eq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    set N : ℕ := n.mantissa_.toNat * 10 ^ d with hN_def
    have h_n_toRat_eq : (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        = (N : ℚ) * (10 : ℚ) ^ e := by
      rw [hN_def, h_pow_split]; push_cast; ring
    rw [h_n_toRat_eq] at h_lt_lo h_lt_hi
    have h_k_lt_N : ((k : ℚ) + 8/10) ≤ (N : ℚ) := by
      have h_le' : ((k : ℚ) + 8 / 10) * 10 ^ e ≤ (N : ℚ) * 10 ^ e := h_lt_lo
      exact le_of_mul_le_mul_right h_le' h_pow_e_pos
    have h_N_lt : (N : ℚ) < (k : ℚ) + 1 :=
      lt_of_mul_lt_mul_right h_lt_hi (le_of_lt h_pow_e_pos)
    have h_k_lt_N_strict : (k : ℚ) < (N : ℚ) := by linarith
    have h_k_lt_N_nat : k < N := by exact_mod_cast h_k_lt_N_strict
    have h_N_lt_nat : N < k + 1 := by
      have : (N : ℚ) < ((k + 1 : ℕ) : ℚ) := by push_cast; exact h_N_lt
      exact_mod_cast this
    omega
  · push_neg at h_exp_ge
    have h_diff_pos : (0 : ℤ) < e - n.exponent_ := by linarith
    set d : ℕ := (e - n.exponent_).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = e - n.exponent_ := Int.toNat_of_nonneg (by linarith)
    have h_d_pos : 0 < d := by
      have : (0 : ℤ) < (d : ℤ) := by rw [h_d_cast]; exact h_diff_pos
      exact_mod_cast this
    have h_pow_split : (10 : ℚ) ^ e = (10 : ℚ) ^ d * (10 : ℚ) ^ n.exponent_ := by
      have h_exp_eq : e = (d : ℤ) + n.exponent_ := by rw [h_d_cast]; ring
      rw [h_exp_eq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    have h_pow_n_exp_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    have h_k08_10d_le : ((k : ℚ) + 8 / 10) * (10 : ℚ) ^ d ≤ (n.mantissa_.toNat : ℚ) := by
      have h_step : ((k : ℚ) + 8 / 10) * 10 ^ e
          = ((k : ℚ) + 8 / 10) * 10 ^ d * (10 : ℚ) ^ n.exponent_ := by
        rw [h_pow_split]; ring
      rw [h_step] at h_lt_lo
      exact le_of_mul_le_mul_right h_lt_lo h_pow_n_exp_pos
    have h_m_lt_k1_10d : (n.mantissa_.toNat : ℚ) < ((k : ℚ) + 1) * (10 : ℚ) ^ d := by
      have h_step : ((k : ℚ) + 1) * 10 ^ e = ((k : ℚ) + 1) * 10 ^ d * (10 : ℚ) ^ n.exponent_ := by
        rw [h_pow_split]; ring
      rw [h_step] at h_lt_hi
      exact lt_of_mul_lt_mul_right h_lt_hi (le_of_lt h_pow_n_exp_pos)
    by_cases h_d_eq_1 : d = 1
    · -- d = 1: mantissa in {10k+8, 10k+9}, both > maxRep, neither % 10 = 0
      rw [h_d_eq_1] at h_k08_10d_le h_m_lt_k1_10d
      have h_lhs : ((k : ℚ) + 8 / 10) * (10 : ℚ) ^ (1 : ℕ) = 10 * k + 8 := by
        ring
      have h_rhs : ((k : ℚ) + 1) * (10 : ℚ) ^ (1 : ℕ) = 10 * k + 10 := by ring
      rw [h_lhs] at h_k08_10d_le
      rw [h_rhs] at h_m_lt_k1_10d
      have h_le_nat : 10 * k + 8 ≤ n.mantissa_.toNat := by
        have hq : ((10 * k + 8 : ℕ) : ℚ) ≤ ((n.mantissa_.toNat : ℕ) : ℚ) := by
          push_cast; exact h_k08_10d_le
        exact_mod_cast hq
      have h_lt_nat : n.mantissa_.toNat < 10 * k + 10 := by
        have hq : ((n.mantissa_.toNat : ℕ) : ℚ) < ((10 * k + 10 : ℕ) : ℚ) := by
          push_cast; exact h_m_lt_k1_10d
        exact_mod_cast hq
      have h_maxR : maxRep.toNat = maxRepNat := maxRep_val
      rw [h_k_eq] at h_le_nat h_lt_nat
      have h_m_gt_maxRep : maxRep.toNat < n.mantissa_.toNat := by omega
      rcases h_cusp with h_le | h_div
      · omega
      · omega
    · have h_d_ge_2 : 2 ≤ d := by omega
      have h_pow_ge : (100 : ℕ) ≤ 10 ^ d := by
        calc (100 : ℕ) = 10 ^ 2 := by norm_num
          _ ≤ 10 ^ d := Nat.pow_le_pow_right (by norm_num) h_d_ge_2
      have h_k_10d_ge : (10 : ℕ) ^ 19 ≤ k * 10 ^ d := by
        have h1 : mantissaFloor * 100 ≤ k * 10 ^ d := by
          rw [h_k_eq]; exact Nat.mul_le_mul_left _ h_pow_ge
        have h2 : (10 : ℕ) ^ 19 ≤ mantissaFloor * 100 := by decide
        omega
      have h_k_10d_le_q : ((k * 10 ^ d : ℕ) : ℚ) ≤ ((k : ℚ) + 8 / 10) * (10 : ℚ) ^ d := by
        push_cast
        have : (0 : ℚ) ≤ 8 / 10 * (10 : ℚ) ^ d := by positivity
        nlinarith
      have h_m_q : (n.mantissa_.toNat : ℚ) < ((10 ^ 19 : ℕ) : ℚ) := by
        push_cast; exact_mod_cast h_m_lt
      have h_chain : ((10 ^ 19 : ℕ) : ℚ) ≤ ((k * 10 ^ d : ℕ) : ℚ) := by exact_mod_cast h_k_10d_ge
      linarith

/-- Negative analog of `no_normalized_in_ulp_gap_at_floor_pos`. -/
theorem no_normalized_in_ulp_gap_at_floor_neg
    (e : Int) (k : ℕ)
    (h_k_eq : k = mantissaFloor) (h_k_max : k < (10 : ℕ) ^ 19)
    (n : Number) (h_norm : n.isNormalized)
    (h_n_neg : n.toRat < 0)
    (h_lt_lo : -(((k : ℚ) + 1) * 10 ^ e) < n.toRat)
    (h_lt_hi : n.toRat ≤ -(((k : ℚ) + 8 / 10) * 10 ^ e)) :
    False := by
  have h_n_neg_flag : n.negative_ = true := by
    rcases hn : n.negative_ with _ | _
    · exfalso
      have : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n hn
      linarith
    · rfl
  set n' : Number := { n with negative_ := false } with hn'_def
  have h_n'_neg_false : n'.negative_ = false := rfl
  have h_n'_mant : n'.mantissa_ = n.mantissa_ := rfl
  have h_n'_exp : n'.exponent_ = n.exponent_ := rfl
  have h_n'_toRat_eq : n'.toRat = -n.toRat := by
    rw [Number.toRat_of_nonneg n' h_n'_neg_false]
    rw [Number.toRat_of_neg n h_n_neg_flag]
    rw [h_n'_mant, h_n'_exp]
    ring
  have h_n'_norm : n'.isNormalized := by
    rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, h_emax⟩
    · exfalso
      have : n.negative_ = false := by rw [h_zero]; rfl
      rw [this] at h_n_neg_flag
      exact Bool.false_ne_true h_n_neg_flag
    · right
      refine ⟨?_, ?_, ?_, ?_, ?_⟩
      · rw [h_n'_mant]; exact h_min
      · rw [h_n'_mant]; exact h_max
      · rw [h_n'_mant]; exact h_valid
      · rw [h_n'_exp]; exact h_emin
      · rw [h_n'_exp]; exact h_emax
  have h_n'_pos : 0 < n'.toRat := by rw [h_n'_toRat_eq]; linarith
  have h_lt_lo' : ((k : ℚ) + 8 / 10) * 10 ^ e ≤ n'.toRat := by rw [h_n'_toRat_eq]; linarith
  have h_lt_hi' : n'.toRat < ((k : ℚ) + 1) * 10 ^ e := by rw [h_n'_toRat_eq]; linarith
  exact no_normalized_in_ulp_gap_at_floor_pos e k h_k_eq h_k_max n' h_n'_norm
    h_n'_pos h_lt_lo' h_lt_hi'

/-- Cusp-gap lemma: no positive normalized Number's toRat lies in
`(maxRep.toNat * 10^e, maxRepCuspTarget * 10^e)` (the 3-unit gap at the cusp). -/
theorem no_normalized_in_cusp_gap_pos
    (e : Int) (n : Number) (h_norm : n.isNormalized)
    (h_n_pos : 0 < n.toRat)
    (h_lt_lo : (maxRep.toNat : ℚ) * 10 ^ e < n.toRat)
    (h_lt_hi : n.toRat < (maxRepCuspTarget : ℚ) * 10 ^ e) :
    False := by
  have h_neg_false : n.negative_ = false := by
    rcases hn : n.negative_ with _ | _
    · rfl
    · exfalso
      have : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n hn
      linarith
  have h_m_ne : n.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_toRat_ne_zero h_n_pos.ne'
  rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, _h_emax⟩
  · exfalso; apply h_m_ne; rw [h_zero]; rfl
  have h_m_min : (10 : ℕ) ^ 18 ≤ n.mantissa_.toNat := by
    have h := h_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10 ^ 18 := by decide
    omega
  have h_m_max : n.mantissa_.toNat ≤ (10 : ℕ) ^ 19 - 1 := by
    have h := h_max
    rw [UInt64.le_iff_toNat_le] at h
    have hmax_eq : largeRange.max.toNat = 10 ^ 19 - 1 := by decide
    omega
  have h_m_lt : n.mantissa_.toNat < (10 : ℕ) ^ 19 := by omega
  have h_cusp : n.mantissa_.toNat ≤ maxRep.toNat ∨ n.mantissa_.toNat % 10 = 0 := by
    rcases h_valid with hle | hmod
    · left; exact UInt64.le_iff_toNat_le.mp hle
    · right; exact hmod
  rw [Number.toRat_of_nonneg n h_neg_false] at h_lt_lo h_lt_hi
  have h_pow_e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  have h_maxR : maxRep.toNat = maxRepNat := maxRep_val
  have h_cuspMin_v : (maxRepCuspTarget : ℕ) = maxRepCuspTarget := rfl
  by_cases h_exp_ge : e ≤ n.exponent_
  · have h_diff_nn : (0 : ℤ) ≤ n.exponent_ - e := by linarith
    set d : ℕ := (n.exponent_ - e).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = n.exponent_ - e := Int.toNat_of_nonneg h_diff_nn
    have h_pow_split : (10 : ℚ) ^ n.exponent_ = (10 : ℚ) ^ d * (10 : ℚ) ^ e := by
      have h_exp_eq : n.exponent_ = (d : ℤ) + e := by rw [h_d_cast]; ring
      rw [h_exp_eq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    set N : ℕ := n.mantissa_.toNat * 10 ^ d with hN_def
    have h_n_toRat_eq : (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        = (N : ℚ) * (10 : ℚ) ^ e := by
      rw [hN_def, h_pow_split]; push_cast; ring
    rw [h_n_toRat_eq] at h_lt_lo h_lt_hi
    have h_maxR_lt_N : (maxRep.toNat : ℚ) < (N : ℚ) :=
      lt_of_mul_lt_mul_right h_lt_lo (le_of_lt h_pow_e_pos)
    have h_N_lt_cusp : (N : ℚ) < (maxRepCuspTarget : ℚ) :=
      lt_of_mul_lt_mul_right h_lt_hi (le_of_lt h_pow_e_pos)
    have h_maxR_lt_N_nat : maxRep.toNat < N := by exact_mod_cast h_maxR_lt_N
    have h_N_lt_cusp_nat : N < maxRepCuspTarget := by
      have : (N : ℚ) < ((maxRepCuspTarget : ℕ) : ℚ) := by push_cast; exact h_N_lt_cusp
      exact_mod_cast this
    by_cases h_d_zero : d = 0
    · have h_N_eq : N = n.mantissa_.toNat := by rw [hN_def, h_d_zero]; ring
      rw [← h_N_eq] at h_cusp
      have h_N_gt_maxR : maxRep.toNat < N := h_maxR_lt_N_nat
      rcases h_cusp with h_le | h_div
      · omega
      · omega
    · have h_d_pos : 1 ≤ d := by omega
      have h_pow_ge : (10 : ℕ) ≤ 10 ^ d := by
        calc (10 : ℕ) = 10 ^ 1 := by norm_num
          _ ≤ 10 ^ d := Nat.pow_le_pow_right (by norm_num) h_d_pos
      have h_N_ge_pow19 : (10 : ℕ) ^ 19 ≤ N := by
        rw [hN_def]
        calc (10 : ℕ) ^ 19 = 10 ^ 18 * 10 := by ring
          _ ≤ n.mantissa_.toNat * 10 ^ d := Nat.mul_le_mul h_m_min h_pow_ge
      omega
  · push_neg at h_exp_ge
    have h_diff_pos : (0 : ℤ) < e - n.exponent_ := by linarith
    set d : ℕ := (e - n.exponent_).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = e - n.exponent_ := Int.toNat_of_nonneg (by linarith)
    have h_d_pos : 0 < d := by
      have : (0 : ℤ) < (d : ℤ) := by rw [h_d_cast]; exact h_diff_pos
      exact_mod_cast this
    have h_pow_split : (10 : ℚ) ^ e = (10 : ℚ) ^ d * (10 : ℚ) ^ n.exponent_ := by
      have h_exp_eq : e = (d : ℤ) + n.exponent_ := by rw [h_d_cast]; ring
      rw [h_exp_eq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    have h_pow_n_exp_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    have h_maxR_lt : (maxRep.toNat : ℚ) * (10 : ℚ) ^ d < (n.mantissa_.toNat : ℚ) := by
      have h_step : (maxRep.toNat : ℚ) * 10 ^ e
          = (maxRep.toNat : ℚ) * 10 ^ d * (10 : ℚ) ^ n.exponent_ := by
        rw [h_pow_split]; ring
      rw [h_step] at h_lt_lo
      exact lt_of_mul_lt_mul_right h_lt_lo (le_of_lt h_pow_n_exp_pos)
    have h_d_ge_1 : 1 ≤ d := h_d_pos
    have h_pow_ge : (10 : ℕ) ≤ 10 ^ d := by
      calc (10 : ℕ) = 10 ^ 1 := by norm_num
        _ ≤ 10 ^ d := Nat.pow_le_pow_right (by norm_num) h_d_ge_1
    have h_maxR_10d_big : (10 : ℕ) ^ 19 ≤ maxRep.toNat * 10 ^ d := by
      have h1 : maxRepNat * 10 ≤ maxRep.toNat * 10 ^ d := by
        rw [h_maxR]
        exact Nat.mul_le_mul_left _ h_pow_ge
      have h2 : (10 : ℕ) ^ 19 ≤ maxRepNat * 10 := by decide
      omega
    have h_maxR_10d_q : ((10 ^ 19 : ℕ) : ℚ) ≤ (maxRep.toNat : ℚ) * (10 : ℚ) ^ d := by
      have hq : ((10 ^ 19 : ℕ) : ℚ) ≤ ((maxRep.toNat * 10 ^ d : ℕ) : ℚ) := by exact_mod_cast h_maxR_10d_big
      push_cast at hq; exact hq
    have h_m_q : (n.mantissa_.toNat : ℚ) < ((10 ^ 19 : ℕ) : ℚ) := by
      exact_mod_cast h_m_lt
    linarith

/-- Upper-cusp-gap lemma: no positive normalized Number's toRat lies in
`(maxRepCuspTarget * 10^e, (maxRepNat + 13) * 10^e)` — mantissas `…811`–`…819`
exceed `maxRep` with a nonzero last digit, lower exponents overflow the mantissa
range, higher exponents skip the interval. Consumed by the `to_nearest`
drop-digit leaf at `zm = maxRepUp`. -/
theorem no_normalized_in_upper_cusp_gap_pos
    (e : Int) (n : Number) (h_norm : n.isNormalized)
    (h_n_pos : 0 < n.toRat)
    (h_lt_lo : (maxRepCuspTarget : ℚ) * 10 ^ e < n.toRat)
    (h_lt_hi : n.toRat < (twoPow63Add12 : ℚ) * 10 ^ e) :
    False := by
  have h_neg_false : n.negative_ = false := by
    rcases hn : n.negative_ with _ | _
    · rfl
    · exfalso
      have : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n hn
      linarith
  have h_m_ne : n.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_toRat_ne_zero h_n_pos.ne'
  rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, _h_emax⟩
  · exfalso; apply h_m_ne; rw [h_zero]; rfl
  have h_m_min : (10 : ℕ) ^ 18 ≤ n.mantissa_.toNat := by
    have h := h_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10 ^ 18 := by decide
    omega
  have h_m_max : n.mantissa_.toNat ≤ (10 : ℕ) ^ 19 - 1 := by
    have h := h_max
    rw [UInt64.le_iff_toNat_le] at h
    have hmax_eq : largeRange.max.toNat = 10 ^ 19 - 1 := by decide
    omega
  have h_m_lt : n.mantissa_.toNat < (10 : ℕ) ^ 19 := by omega
  have h_cusp : n.mantissa_.toNat ≤ maxRep.toNat ∨ n.mantissa_.toNat % 10 = 0 := by
    rcases h_valid with hle | hmod
    · left; exact UInt64.le_iff_toNat_le.mp hle
    · right; exact hmod
  rw [Number.toRat_of_nonneg n h_neg_false] at h_lt_lo h_lt_hi
  have h_pow_e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  have h_maxR : maxRep.toNat = maxRepNat := maxRep_val
  by_cases h_exp_ge : e ≤ n.exponent_
  · have h_diff_nn : (0 : ℤ) ≤ n.exponent_ - e := by linarith
    set d : ℕ := (n.exponent_ - e).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = n.exponent_ - e := Int.toNat_of_nonneg h_diff_nn
    have h_pow_split : (10 : ℚ) ^ n.exponent_ = (10 : ℚ) ^ d * (10 : ℚ) ^ e := by
      have h_exp_eq : n.exponent_ = (d : ℤ) + e := by rw [h_d_cast]; ring
      rw [h_exp_eq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    set N : ℕ := n.mantissa_.toNat * 10 ^ d with hN_def
    have h_n_toRat_eq : (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        = (N : ℚ) * (10 : ℚ) ^ e := by
      rw [hN_def, h_pow_split]; push_cast; ring
    rw [h_n_toRat_eq] at h_lt_lo h_lt_hi
    have h_cusp_lt_N : (maxRepCuspTarget : ℚ) < (N : ℚ) :=
      lt_of_mul_lt_mul_right h_lt_lo (le_of_lt h_pow_e_pos)
    have h_N_lt_top : (N : ℚ) < (twoPow63Add12 : ℚ) :=
      lt_of_mul_lt_mul_right h_lt_hi (le_of_lt h_pow_e_pos)
    have h_cusp_lt_N_nat : maxRepCuspTarget < N := by exact_mod_cast h_cusp_lt_N
    have h_N_lt_top_nat : N < twoPow63Add12 := by exact_mod_cast h_N_lt_top
    by_cases h_d_zero : d = 0
    · have h_N_eq : N = n.mantissa_.toNat := by rw [hN_def, h_d_zero]; ring
      rw [← h_N_eq] at h_cusp
      rcases h_cusp with h_le | h_div
      · omega
      · omega
    · have h_d_pos : 1 ≤ d := by omega
      have h_pow_ge : (10 : ℕ) ≤ 10 ^ d := by
        calc (10 : ℕ) = 10 ^ 1 := by norm_num
          _ ≤ 10 ^ d := Nat.pow_le_pow_right (by norm_num) h_d_pos
      have h_N_ge_pow19 : (10 : ℕ) ^ 19 ≤ N := by
        rw [hN_def]
        calc (10 : ℕ) ^ 19 = 10 ^ 18 * 10 := by ring
          _ ≤ n.mantissa_.toNat * 10 ^ d := Nat.mul_le_mul h_m_min h_pow_ge
      omega
  · push_neg at h_exp_ge
    have h_diff_pos : (0 : ℤ) < e - n.exponent_ := by linarith
    set d : ℕ := (e - n.exponent_).toNat with hd_def
    have h_d_cast : ((d : ℤ) : ℤ) = e - n.exponent_ := Int.toNat_of_nonneg (by linarith)
    have h_d_pos : 0 < d := by
      have : (0 : ℤ) < (d : ℤ) := by rw [h_d_cast]; exact h_diff_pos
      exact_mod_cast this
    have h_pow_split : (10 : ℚ) ^ e = (10 : ℚ) ^ d * (10 : ℚ) ^ n.exponent_ := by
      have h_exp_eq : e = (d : ℤ) + n.exponent_ := by rw [h_d_cast]; ring
      rw [h_exp_eq, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    have h_pow_n_exp_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    have h_cusp_lt : (maxRepCuspTarget : ℚ) * (10 : ℚ) ^ d < (n.mantissa_.toNat : ℚ) := by
      have h_step : (maxRepCuspTarget : ℚ) * 10 ^ e
          = (maxRepCuspTarget : ℚ) * 10 ^ d * (10 : ℚ) ^ n.exponent_ := by
        rw [h_pow_split]; ring
      rw [h_step] at h_lt_lo
      exact lt_of_mul_lt_mul_right h_lt_lo (le_of_lt h_pow_n_exp_pos)
    have h_d_ge_1 : 1 ≤ d := h_d_pos
    have h_pow_ge : (10 : ℕ) ≤ 10 ^ d := by
      calc (10 : ℕ) = 10 ^ 1 := by norm_num
        _ ≤ 10 ^ d := Nat.pow_le_pow_right (by norm_num) h_d_ge_1
    have h_cusp_10d_big : (10 : ℕ) ^ 19 ≤ maxRepCuspTarget * 10 ^ d := by
      have h1 : maxRepCuspTarget * 10 ≤ maxRepCuspTarget * 10 ^ d :=
        Nat.mul_le_mul_left _ h_pow_ge
      have h2 : (10 : ℕ) ^ 19 ≤ maxRepCuspTarget * 10 := by decide
      omega
    have h_cusp_10d_q : ((10 ^ 19 : ℕ) : ℚ) ≤ (maxRepCuspTarget : ℚ) * (10 : ℚ) ^ d := by
      have hq : ((10 ^ 19 : ℕ) : ℚ) ≤ ((maxRepCuspTarget * 10 ^ d : ℕ) : ℚ) := by
        exact_mod_cast h_cusp_10d_big
      push_cast at hq; exact hq
    have h_m_q : (n.mantissa_.toNat : ℚ) < ((10 ^ 19 : ℕ) : ℚ) := by
      exact_mod_cast h_m_lt
    linarith

/-- Negative analog of `no_normalized_in_upper_cusp_gap_pos`. -/
theorem no_normalized_in_upper_cusp_gap_neg
    (e : Int) (n : Number) (h_norm : n.isNormalized)
    (_h_n_neg : n.toRat < 0)
    (h_lt_lo : -((twoPow63Add12 : ℚ) * 10 ^ e) < n.toRat)
    (h_lt_hi : n.toRat < -((maxRepCuspTarget : ℚ) * 10 ^ e)) :
    False := by
  have h_n_neg_flag : n.negative_ = true := by
    rcases hn : n.negative_ with _ | _
    · exfalso
      have : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n hn
      linarith
    · rfl
  set n' : Number := { n with negative_ := false } with hn'_def
  have h_n'_neg_false : n'.negative_ = false := rfl
  have h_n'_mant : n'.mantissa_ = n.mantissa_ := rfl
  have h_n'_exp : n'.exponent_ = n.exponent_ := rfl
  have h_n'_toRat_eq : n'.toRat = -n.toRat := by
    rw [Number.toRat_of_nonneg n' h_n'_neg_false]
    rw [Number.toRat_of_neg n h_n_neg_flag]
    rw [h_n'_mant, h_n'_exp]
    ring
  have h_n'_norm : n'.isNormalized := by
    rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, h_emax⟩
    · exfalso
      have : n.negative_ = false := by rw [h_zero]; rfl
      rw [this] at h_n_neg_flag
      exact Bool.false_ne_true h_n_neg_flag
    · right
      refine ⟨?_, ?_, ?_, ?_, ?_⟩
      · rw [h_n'_mant]; exact h_min
      · rw [h_n'_mant]; exact h_max
      · rw [h_n'_mant]; exact h_valid
      · rw [h_n'_exp]; exact h_emin
      · rw [h_n'_exp]; exact h_emax
  have h_n'_pos : 0 < n'.toRat := by rw [h_n'_toRat_eq]; linarith
  have h_lt_lo' : (maxRepCuspTarget : ℚ) * 10 ^ e < n'.toRat := by
    rw [h_n'_toRat_eq]; linarith
  have h_lt_hi' : n'.toRat < (twoPow63Add12 : ℚ) * 10 ^ e := by
    rw [h_n'_toRat_eq]; linarith
  exact no_normalized_in_upper_cusp_gap_pos e n' h_n'_norm h_n'_pos h_lt_lo' h_lt_hi'

/-- Negative analog of `no_normalized_in_cusp_gap_pos`. -/
theorem no_normalized_in_cusp_gap_neg
    (e : Int) (n : Number) (h_norm : n.isNormalized)
    (h_n_neg : n.toRat < 0)
    (h_lt_lo : -((maxRepCuspTarget : ℚ) * 10 ^ e) < n.toRat)
    (h_lt_hi : n.toRat < -((maxRep.toNat : ℚ) * 10 ^ e)) :
    False := by
  have h_n_neg_flag : n.negative_ = true := by
    rcases hn : n.negative_ with _ | _
    · exfalso
      have : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n hn
      linarith
    · rfl
  set n' : Number := { n with negative_ := false } with hn'_def
  have h_n'_neg_false : n'.negative_ = false := rfl
  have h_n'_mant : n'.mantissa_ = n.mantissa_ := rfl
  have h_n'_exp : n'.exponent_ = n.exponent_ := rfl
  have h_n'_toRat_eq : n'.toRat = -n.toRat := by
    rw [Number.toRat_of_nonneg n' h_n'_neg_false]
    rw [Number.toRat_of_neg n h_n_neg_flag]
    rw [h_n'_mant, h_n'_exp]
    ring
  have h_n'_norm : n'.isNormalized := by
    rcases h_norm with h_zero | ⟨h_min, h_max, h_valid, h_emin, h_emax⟩
    · exfalso
      have : n.negative_ = false := by rw [h_zero]; rfl
      rw [this] at h_n_neg_flag
      exact Bool.false_ne_true h_n_neg_flag
    · right
      refine ⟨?_, ?_, ?_, ?_, ?_⟩
      · rw [h_n'_mant]; exact h_min
      · rw [h_n'_mant]; exact h_max
      · rw [h_n'_mant]; exact h_valid
      · rw [h_n'_exp]; exact h_emin
      · rw [h_n'_exp]; exact h_emax
  have h_n'_pos : 0 < n'.toRat := by rw [h_n'_toRat_eq]; linarith
  have h_lt_lo' : (maxRep.toNat : ℚ) * 10 ^ e < n'.toRat := by rw [h_n'_toRat_eq]; linarith
  have h_lt_hi' : n'.toRat < (maxRepCuspTarget : ℚ) * 10 ^ e := by rw [h_n'_toRat_eq]; linarith
  exact no_normalized_in_cusp_gap_pos e n' h_n'_norm h_n'_pos h_lt_lo' h_lt_hi'

end XRPL.Model.Protocol

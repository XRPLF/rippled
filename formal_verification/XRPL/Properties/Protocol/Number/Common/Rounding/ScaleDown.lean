import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Rounding.Guard

namespace XRPL.Model.Protocol

/-! # scaleDown128 invariant

`scaleDown128` divides a UInt128 product `M` by 10 repeatedly until
`m ≤ maxRepUp` (the Enabled330 `repLimit`), pushing each dropped digit into
the guard. -/

/-- Euclidean step: splitting `a % 10^(k+1)` through `a % 10`. -/
lemma nat_mod_pow_step (a k : ℕ) :
    a % 10 ^ (k + 1) = 10 * (a / 10 % 10 ^ k) + a % 10 := by
  have hk_pos : (0 : ℕ) < 10 ^ k := by positivity
  have hk1 : (10 : ℕ) ^ (k + 1) = 10 ^ k * 10 := by ring
  have h_mod_lt_k : a / 10 % 10 ^ k < 10 ^ k := Nat.mod_lt _ hk_pos
  have h_mod_lt_10 : a % 10 < 10 := Nat.mod_lt _ (by decide)
  have hbound : 10 * (a / 10 % 10 ^ k) + a % 10 < 10 ^ (k + 1) := by
    rw [hk1]; nlinarith [h_mod_lt_k, h_mod_lt_10]
  have hdecomp : a = 10 ^ (k + 1) * (a / 10 / 10 ^ k)
                    + (10 * (a / 10 % 10 ^ k) + a % 10) := by
    have h1 : a = 10 * (a / 10) + a % 10 := (Nat.div_add_mod a 10).symm
    have h2 : a / 10 = 10 ^ k * (a / 10 / 10 ^ k) + a / 10 % 10 ^ k :=
      (Nat.div_add_mod (a / 10) (10 ^ k)).symm
    calc a = 10 * (a / 10) + a % 10 := h1
      _ = 10 * (10 ^ k * (a / 10 / 10 ^ k) + a / 10 % 10 ^ k) + a % 10 := by
            conv_lhs => rw [h2]
      _ = 10 ^ (k + 1) * (a / 10 / 10 ^ k) + (10 * (a / 10 % 10 ^ k) + a % 10) := by
            rw [hk1]; ring
  conv_lhs => rw [hdecomp]
  rw [Nat.mul_add_mod]
  exact Nat.mod_eq_of_lt hbound

/-- Main invariant of scaleDown128. The 5th conjunct constrains the residue
when the output mantissa is `(maxRep + 1) / 10` — under the `maxRepUp`
threshold that case is unreachable when digits were dropped (the output is
`≥ (maxRepUp + 1) / 10 = mantissaFloor + 1`), so the conjunct is kept only
for the consumers' destructuring shape. -/
theorem scaleDown128_correct :
    ∀ (M : UInt128) (e : Int) (g0 : Guard) (f0 : ℚ) (_hrep0 : represents g0 f0),
      let (m, e', g) := scaleDown128 M e g0
      ∃ k : ℕ,
        e' = e + k ∧
        m.toNat ≤ maxRepUp.toNat ∧
        M.toNat = m.toNat * 10 ^ k + M.toNat % 10 ^ k ∧
        represents g ((f0 + ((M.toNat % 10 ^ k : ℕ) : ℚ)) / 10 ^ k) ∧
        (k > 0 ∧ m.toNat = (maxRep.toNat + 1) / 10 → 8 * 10 ^ (k - 1) ≤ M.toNat % 10 ^ k) := by
  intro M e g0
  induction M, e, g0 using scaleDown128.induct with
  | case1 M e g0 hcond d IH =>
    intro f0 hrep0
    have hd_def : d = toUInt64 (M % 10) := rfl
    have h10_128 : (10 : UInt128).toNat = 10 := by decide
    have hM_mod_lt : M.toNat % 10 < 10 := Nat.mod_lt _ (by decide)
    have hM10_nat : (M / 10 : UInt128).toNat = M.toNat / 10 := by
      rw [BitVec.toNat_udiv, h10_128]
    have hMmod_nat : (M % 10 : UInt128).toNat = M.toNat % 10 := by
      rw [BitVec.toNat_umod, h10_128]
    have hd_fit : (M % 10 : UInt128).toNat < 2 ^ 64 := by
      rw [hMmod_nat]; omega
    have hd_eq : d.toNat = M.toNat % 10 := by
      simp only [hd_def]; rw [toNat_toUInt64 hd_fit, hMmod_nat]
    have hd_lt : d.toNat < 10 := by rw [hd_eq]; exact hM_mod_lt
    have hEuc : M.toNat = 10 * (M / 10).toNat + M.toNat % 10 := by
      rw [hM10_nat]; omega
    have hM_gt_maxRep : M.toNat > maxRepUp.toNat := by
      have := BitVec.lt_def.mp hcond
      rwa [toNat_toUInt128] at this
    have hpush : represents (g0.push d) ((f0 + d.toNat) / 10) := represents_push hrep0 hd_lt
    have hunfold : scaleDown128 M e g0
        = scaleDown128 (M / 10) (e + 1) (g0.push d) := by
      conv_lhs => rw [scaleDown128]
      simp [hcond, hd_def]
    rw [hunfold]
    obtain ⟨k, hek, hrep, hM_decomp, hrep_g, hfloor_IH⟩ := IH _ hpush
    set result := scaleDown128 (M / 10) (e + 1) (g0.push d)
    refine ⟨k + 1, ?_, hrep, ?_, ?_, ?_⟩
    · push_cast; linarith
    · have hstep := nat_mod_pow_step M.toNat k
      have hM10' : (M / 10).toNat = M.toNat / 10 := hM10_nat
      calc M.toNat
          = 10 * (M / 10).toNat + M.toNat % 10 := hEuc
        _ = 10 * (result.1.toNat * 10 ^ k + (M / 10).toNat % 10 ^ k) + M.toNat % 10 := by
              linarith [hM_decomp]
        _ = result.1.toNat * 10 ^ (k + 1) + (10 * ((M / 10).toNat % 10 ^ k) + M.toNat % 10) := by
              ring
        _ = result.1.toNat * 10 ^ (k + 1) + M.toNat % 10 ^ (k + 1) := by
              rw [hM10', ← hstep]
    · have hstep := nat_mod_pow_step M.toNat k
      have hM10' : (M / 10).toNat = M.toNat / 10 := hM10_nat
      have hstep_q : ((M.toNat % 10 ^ (k + 1) : ℕ) : ℚ)
          = 10 * (((M / 10).toNat % 10 ^ k : ℕ) : ℚ) + ((M.toNat % 10 : ℕ) : ℚ) := by
        have : M.toNat % 10 ^ (k + 1) = 10 * ((M / 10).toNat % 10 ^ k) + M.toNat % 10 := by
          rw [hM10']; exact hstep
        exact_mod_cast this
      have target_eq :
          (f0 + ((M.toNat % 10 ^ (k + 1) : ℕ) : ℚ)) / 10 ^ (k + 1)
            = ((f0 + (d.toNat : ℚ)) / 10
                + (((M / 10).toNat % 10 ^ k : ℕ) : ℚ)) / 10 ^ k := by
        rw [hstep_q]
        have hd_eq_q : (d.toNat : ℚ) = ((M.toNat % 10 : ℕ) : ℚ) := by
          exact_mod_cast hd_eq
        rw [hd_eq_q]
        have hk1_q : ((10 : ℚ) ^ (k + 1)) = 10 * 10 ^ k := by ring
        rw [hk1_q]
        field_simp
        ring
      rw [target_eq]
      exact hrep_g
    · rintro ⟨_hk_pos, hm_floor⟩
      by_cases hk0 : k = 0
      · subst hk0
        have hM_div_eq : (M / 10).toNat = result.1.toNat := by
          have := hM_decomp
          simp [Nat.mod_one] at this
          omega
        have h_div_val : (M / 10).toNat = mantissaFloor := by
          rw [hM_div_eq]; exact hm_floor
        have hup_eq : maxRepUp.toNat = maxRepUpNat := rfl
        have : 9223372036854775800 + M.toNat % 10 > 9223372036854775810 := by
          have := hEuc
          have : M.toNat = 10 * mantissaFloor + M.toNat % 10 := by
            rw [← h_div_val]; linarith
          rw [hup_eq] at hM_gt_maxRep; omega
        have h_mod_ge : M.toNat % 10 ≥ 8 := by omega
        change 8 * 10 ^ (0 + 1 - 1) ≤ M.toNat % 10 ^ (0 + 1)
        simp only [zero_add, pow_one]
        exact h_mod_ge
      · have hk_pos' : k > 0 := Nat.pos_of_ne_zero hk0
        have h_inner_floor : result.1.toNat = (maxRep.toNat + 1) / 10 := hm_floor
        have h_inner_ge := hfloor_IH ⟨hk_pos', h_inner_floor⟩
        have hstep := nat_mod_pow_step M.toNat k
        have hM10' : (M / 10).toNat = M.toNat / 10 := hM10_nat
        have hstep' : M.toNat % 10 ^ (k + 1) = 10 * ((M / 10).toNat % 10 ^ k) + M.toNat % 10 := by
          rw [hM10']; exact hstep
        change 8 * 10 ^ (k + 1 - 1) ≤ M.toNat % 10 ^ (k + 1)
        have hk_simp : k + 1 - 1 = k := by omega
        rw [hk_simp, hstep']
        have h1 : 8 * 10 ^ k = 10 * (8 * 10 ^ (k - 1)) := by
          have hkk : k = (k - 1) + 1 := by omega
          conv_lhs => rw [hkk]
          ring
        rw [h1]
        have h_le : 10 * (8 * 10 ^ (k - 1)) ≤ 10 * ((M / 10).toNat % 10 ^ k) :=
          Nat.mul_le_mul_left 10 h_inner_ge
        omega
  | case2 M e g0 hcond =>
    intro f0 hrep0
    have hrep_le : M ≤ toUInt128 maxRepUp := BitVec.not_lt.mp hcond
    have hexit : scaleDown128 M e g0 = (toUInt64 M, e, g0) := by
      unfold scaleDown128
      simp [hcond]
    rw [hexit]
    have hM_le_rep : M.toNat ≤ maxRepUp.toNat := by
      have := BitVec.le_def.mp hrep_le; rwa [toNat_toUInt128] at this
    have hfit : M.toNat < 2 ^ 64 := by
      have : maxRepUp.toNat < 2 ^ 64 := maxRepUp.toNat_lt; omega
    refine ⟨0, by simp, ?_, ?_, ?_, ?_⟩
    · rw [toNat_toUInt64 hfit]; exact hM_le_rep
    · rw [toNat_toUInt64 hfit]; simp [Nat.mod_one]
    · have h10 : (10 : ℕ) ^ 0 = 1 := pow_zero _
      have h10q : (10 : ℚ) ^ 0 = 1 := pow_zero _
      have hmod : M.toNat % 10 ^ 0 = 0 := by rw [h10]; exact Nat.mod_one _
      rw [hmod, h10q]
      simpa using hrep0
    · rintro ⟨h, _⟩
      exact absurd h (by omega)

/-- Lower bound on scaleDown128 output: `m.toNat ≥ (maxRepUp.toNat + 1) / 10`
(`= mantissaFloor + 1` — strictly above the floor, which is what makes the
floor residue case vacuous downstream). -/
theorem scaleDown128_lower_bound :
    ∀ (M : UInt128) (e : Int) (g0 : Guard),
      M.toNat > maxRepUp.toNat →
      let (m, _, _) := scaleDown128 M e g0
      m.toNat ≥ (maxRepUp.toNat + 1) / 10 := by
  intro M e g0
  induction M, e, g0 using scaleDown128.induct with
  | case1 M e g0 hcond d IH =>
    intro _hM_gt
    have h10_128 : (10 : UInt128).toNat = 10 := by decide
    have hM10_nat : (M / 10 : UInt128).toNat = M.toNat / 10 := by
      rw [BitVec.toNat_udiv, h10_128]
    by_cases h_next_gt : (M / 10 : UInt128).toNat > maxRepUp.toNat
    · have h_rec := IH h_next_gt
      simp only at h_rec ⊢
      unfold scaleDown128
      simp only [hcond]
      exact h_rec
    · push_neg at h_next_gt
      have h_div_ge : M.toNat / 10 ≥ (maxRepUp.toNat + 1) / 10 :=
        Nat.div_le_div_right _hM_gt
      unfold scaleDown128
      simp only [hcond]
      unfold scaleDown128
      have h_next_cond :
          ¬ ((M / 10 : UInt128) > toUInt128 maxRepUp) := by
        intro h_gt_rep
        have := BitVec.lt_def.mp h_gt_rep
        rw [toNat_toUInt128] at this
        omega
      simp only [h_next_cond]
      have h_fit : (M / 10 : UInt128).toNat < 2 ^ 64 := by
        have : maxRepUp.toNat < 2 ^ 64 := maxRepUp.toNat_lt
        omega
      simp only [↓reduceDIte]
      change (toUInt64 (M / 10)).toNat ≥ (maxRepUp.toNat + 1) / 10
      rw [toNat_toUInt64 h_fit, hM10_nat]
      exact h_div_ge
  | case2 M e g0 hcond =>
    intro hM_gt
    have hM_le_rep : M.toNat ≤ maxRepUp.toNat := by
      have := BitVec.le_def.mp (BitVec.not_lt.mp hcond)
      rwa [toNat_toUInt128] at this
    omega

end XRPL.Model.Protocol

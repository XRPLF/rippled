import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Closest.Defs
import XRPL.Properties.Protocol.Number.Common.Closest.Bounds
import XRPL.Properties.Protocol.Number.Common.Closest.Existence
import XRPL.Properties.Protocol.Number.Common.Closest.Normalize
import XRPL.Properties.Protocol.Number.Common.Closest.Tightness


namespace XRPL.Model.Protocol

/-! # Grid-point idempotence

`Number.lower`/`Number.upper` of a value that is itself a normalized grid
point return (a representative of) that very value. The addition operator's
zero-operand guards return the other operand exactly, so its discrete-rounding
theorems consume these. -/

/-- `Number.upper` never undershoots its argument. -/
lemma Number.upper_ge (q : ℚ) (n : Number) (h : Number.upper q = some n) :
    q ≤ n.toRat := by
  unfold Number.upper at h
  split_ifs at h with hq0 hqneg
  · have h_eq : n = Number.zero := (Option.some.inj h).symm
    rw [h_eq, Number.toRat_zero, hq0]
  · rw [Option.map_eq_some_iff] at h
    obtain ⟨n', hn', heq⟩ := h
    have hnq : 0 < -q := by linarith
    have h_le := lowerPosAux_le (-q) hnq n' hn'
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
    rw [← heq]
    split_ifs with h_nz
    · rw [Number.toRat_zero]
      linarith
    · rw [Number.toRat_set_neg_true_of_nn n' h_n'_pos]
      linarith
  · have hq_pos : 0 < q := by
      rcases lt_trichotomy q 0 with h1 | h1 | h1
      · exact absurd h1 hqneg
      · exact absurd h1 hq0
      · exact h1
    exact upperPosAux_ge q hq_pos n h

/-- The positive twin of a nonzero normalized Number is normalized. -/
private lemma pos_twin_isNormalized (x : Number) (h_norm : x.isNormalized)
    (h_x_neg : x.negative_ = true) :
    ({x with negative_ := false} : Number).isNormalized := by
  rcases h_norm with hz | ⟨hmin, hmax, hv, hemin, hemax⟩
  · exfalso
    have h_neg_z : x.negative_ = false := by rw [hz]; rfl
    rw [h_neg_z] at h_x_neg
    exact absurd h_x_neg (by simp)
  · right
    exact ⟨hmin, hmax, hv, hemin, hemax⟩

/-- `Number.lower` of a normalized nonzero grid point recovers its value. -/
theorem Number.lower_value_self (x : Number) (h_norm : x.isNormalized)
    (hx_ne : x.toRat ≠ 0) :
    ∃ n, Number.lower x.toRat = some n ∧ x.toRat = n.toRat := by
  have h_exists : ∃ n, Number.lower x.toRat = some n := by
    rcases lt_or_gt_of_ne hx_ne with h_neg | h_pos
    · -- negative grid point: route through `upperPosAux` of the positive twin.
      have h_x_neg : x.negative_ = true := by
        rcases hh : x.negative_ with _ | _
        · exact absurd (Number.toRat_nonneg_of_nonnegative x hh) (not_le.mpr h_neg)
        · rfl
      set m : Number := {x with negative_ := false} with hm_def
      have h_m_eq : m.toRat = -x.toRat := Number.toRat_set_neg_false_of_neg x h_x_neg
      have h_m_norm : m.isNormalized := pos_twin_isNormalized x h_norm h_x_neg
      have h_m_pos : 0 < m.toRat := by rw [h_m_eq]; linarith
      have hnq : 0 < -x.toRat := by linarith
      obtain ⟨n, hn⟩ := Number.upper_some_of_pos_witnesses (-x.toRat) hnq
        m h_m_norm h_m_pos (le_of_eq h_m_eq) m h_m_norm (ge_of_eq h_m_eq)
      unfold Number.upper at hn
      rw [if_neg (ne_of_gt hnq), if_neg (not_lt.mpr (le_of_lt hnq))] at hn
      rw [Number.lower_neg_eq x.toRat h_neg, hn]
      exact ⟨_, rfl⟩
    · obtain ⟨n, hn⟩ := Number.lower_some_of_pos_witnesses x.toRat h_pos
        x h_norm h_pos (le_refl _) x h_norm (le_refl _)
      exact ⟨n, hn⟩
  obtain ⟨n, hn⟩ := h_exists
  exact ⟨n, hn, le_antisymm
    (Number.lower_tight x.toRat n hn x h_norm (le_refl _))
    (Number.lower_le x.toRat n hn)⟩

/-- `Number.upper` of a normalized nonzero grid point recovers its value. -/
theorem Number.upper_value_self (x : Number) (h_norm : x.isNormalized)
    (hx_ne : x.toRat ≠ 0) :
    ∃ n, Number.upper x.toRat = some n ∧ x.toRat = n.toRat := by
  have h_exists : ∃ n, Number.upper x.toRat = some n := by
    rcases lt_or_gt_of_ne hx_ne with h_neg | h_pos
    · -- negative grid point: route through `lowerPosAux` of the positive twin.
      have h_x_neg : x.negative_ = true := by
        rcases hh : x.negative_ with _ | _
        · exact absurd (Number.toRat_nonneg_of_nonnegative x hh) (not_le.mpr h_neg)
        · rfl
      set m : Number := {x with negative_ := false} with hm_def
      have h_m_eq : m.toRat = -x.toRat := Number.toRat_set_neg_false_of_neg x h_x_neg
      have h_m_norm : m.isNormalized := pos_twin_isNormalized x h_norm h_x_neg
      have h_m_pos : 0 < m.toRat := by rw [h_m_eq]; linarith
      have hnq : 0 < -x.toRat := by linarith
      obtain ⟨n, hn⟩ := Number.lower_some_of_pos_witnesses (-x.toRat) hnq
        m h_m_norm h_m_pos (le_of_eq h_m_eq) m h_m_norm (ge_of_eq h_m_eq)
      unfold Number.lower at hn
      rw [if_neg (ne_of_gt hnq), if_neg (not_lt.mpr (le_of_lt hnq))] at hn
      rw [Number.upper_neg_eq x.toRat h_neg, hn]
      exact ⟨_, rfl⟩
    · obtain ⟨n, hn⟩ := Number.upper_some_of_pos_witnesses x.toRat h_pos
        x h_norm h_pos (le_refl _) x h_norm (le_refl _)
      exact ⟨n, hn⟩
  obtain ⟨n, hn⟩ := h_exists
  exact ⟨n, hn, le_antisymm
    (Number.upper_ge x.toRat n hn)
    (Number.upper_tight x.toRat n hn x h_norm (le_refl _))⟩

end XRPL.Model.Protocol

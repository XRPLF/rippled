import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Closest.Defs
import XRPL.Properties.Protocol.Number.Common.Closest.Bounds
import XRPL.Properties.Protocol.Number.Common.Closest.Existence
import XRPL.Properties.Protocol.Number.Common.Closest.Normalize
import XRPL.Properties.Protocol.Number.Common.Closest.Tightness
import XRPL.Properties.Protocol.Number.Common.Closest.Gap
import XRPL.Properties.Protocol.Number.Common.Closest.GridPoint


namespace XRPL.Model.Protocol

/-! # Decade bridge: `lower`/`upper` as floor/ceil inside a decade

For a value in the lower half of the decade `[10^18·10^E, 10^19·10^E)` —
where the 19-digit grid spacing is exactly `10^E` and the `pushOverflow`
cusp is out of reach — `Number.lower` and `Number.upper` are literally
`⌊q/10^E⌋·10^E` and `⌈q/10^E⌉·10^E`. -/

/-- The canonical positive grid point at mantissa `k`, exponent `E`. -/
private def gridPt (k : ℕ) (E : ℤ) (hk : k < 2 ^ 64) : Number :=
  ⟨false, ⟨⟨⟨k, hk⟩⟩⟩, E⟩

private lemma gridPt_toRat (k : ℕ) (E : ℤ) (hk : k < 2 ^ 64) :
    (gridPt k E hk).toRat = (k : ℚ) * 10 ^ E := by
  rw [gridPt, Number.toRat_of_nonneg _ rfl]
  rfl

private lemma gridPt_isNormalized (k : ℕ) (E : ℤ) (hk : k < 2 ^ 64)
    (hk_lo : 10 ^ 18 ≤ k) (hk_hi : k ≤ 2 * 10 ^ 18)
    (hE_lo : minExponent ≤ E) (hE_hi : E ≤ maxExponent) :
    (gridPt k E hk).isNormalized := by
  right
  refine ⟨?_, ?_, ?_, hE_lo, hE_hi⟩
  · show largeRange.min ≤ _
    rw [UInt64.le_iff_toNat_le]
    show largeRange.min.toNat ≤ k
    rw [(by decide : largeRange.min.toNat = 1000000000000000000)]
    omega
  · show _ ≤ largeRange.max
    rw [UInt64.le_iff_toNat_le]
    show k ≤ largeRange.max.toNat
    rw [(by decide : largeRange.max.toNat = 9999999999999999999)]
    omega
  · left
    show (⟨⟨⟨k, hk⟩⟩⟩ : UInt64) ≤ maxRep
    rw [UInt64.le_iff_toNat_le]
    show k ≤ maxRep.toNat
    rw [(by decide : maxRep.toNat = 9223372036854775807)]
    omega

/-- In the lower half-decade, `Number.lower` of a positive value is its
floor on the `10^E` grid. -/
theorem Number.lower_eq_floor_in_decade (q : ℚ) (E : ℤ)
    (hE_lo : minExponent ≤ E) (hE_hi : E ≤ maxExponent)
    (h_lo : (10 : ℚ) ^ (18 : ℕ) * 10 ^ E ≤ q)
    (h_hi : q < 2 * 10 ^ (18 : ℕ) * 10 ^ E) :
    ∃ n, Number.lower q = some n ∧ n.toRat = (⌊q / 10 ^ E⌋ : ℚ) * 10 ^ E := by
  have hpow_pos : (0 : ℚ) < (10 : ℚ) ^ E := zpow_pos (by norm_num) _
  have hq_pos : 0 < q := lt_of_lt_of_le (by positivity) h_lo
  -- The floor mantissa, as a natural number in [10^18, 2·10^18].
  have h_div_lo : (10 : ℚ) ^ (18 : ℕ) ≤ q / 10 ^ E := by
    rw [le_div_iff₀ hpow_pos]
    exact h_lo
  have h_div_hi : q / 10 ^ E < 2 * 10 ^ (18 : ℕ) := by
    rw [div_lt_iff₀ hpow_pos]
    exact h_hi
  set K : ℤ := ⌊q / 10 ^ E⌋ with hK_def
  have hK_lo : (10 ^ 18 : ℤ) ≤ K := by
    apply Int.le_floor.mpr
    calc ((10 ^ 18 : ℤ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by norm_num
      _ ≤ q / 10 ^ E := h_div_lo
  have hK_hi : K < 2 * 10 ^ 18 := by
    have h1 : (K : ℚ) ≤ q / 10 ^ E := Int.floor_le _
    by_contra h_not
    push_neg at h_not
    have h2 : ((2 * 10 ^ 18 : ℤ) : ℚ) ≤ (K : ℚ) := by exact_mod_cast h_not
    have h3 : ((2 * 10 ^ 18 : ℤ) : ℚ) = 2 * 10 ^ (18 : ℕ) := by norm_num
    linarith
  set k : ℕ := K.toNat with hk_def
  have hk_K : (k : ℤ) = K := Int.toNat_of_nonneg (by omega)
  have hk_lo : 10 ^ 18 ≤ k := by omega
  have hk_hi : k ≤ 2 * 10 ^ 18 := by omega
  have hk_fit : k < 2 ^ 64 := by omega
  have hk1_fit : k + 1 < 2 ^ 64 := by omega
  have hk_q : ((k : ℕ) : ℚ) = (K : ℚ) := by exact_mod_cast hk_K
  -- The two bracketing grid points.
  set x : Number := gridPt k E hk_fit with hx_def
  have hx_norm : x.isNormalized := gridPt_isNormalized k E hk_fit hk_lo hk_hi hE_lo hE_hi
  have hx_val : x.toRat = (K : ℚ) * 10 ^ E := by
    rw [hx_def, gridPt_toRat, hk_q]
  have hx_le : x.toRat ≤ q := by
    rw [hx_val]
    have h1 : (K : ℚ) ≤ q / 10 ^ E := Int.floor_le _
    calc (K : ℚ) * 10 ^ E ≤ (q / 10 ^ E) * 10 ^ E :=
          mul_le_mul_of_nonneg_right h1 (le_of_lt hpow_pos)
      _ = q := div_mul_cancel₀ q (ne_of_gt hpow_pos)
  have hx_pos : 0 < x.toRat := by
    rw [hx_val]
    have : (0 : ℚ) < (K : ℚ) := by
      have : (0 : ℤ) < K := by omega
      exact_mod_cast this
    positivity
  set y : Number := gridPt (k + 1) E hk1_fit with hy_def
  have hy_norm : y.isNormalized :=
    gridPt_isNormalized (k + 1) E hk1_fit (by omega) (by omega) hE_lo hE_hi
  have hy_val : y.toRat = ((k : ℚ) + 1) * 10 ^ E := by
    rw [hy_def, gridPt_toRat]
    push_cast
    ring
  have hy_ge : q ≤ y.toRat := by
    rw [hy_val]
    have h1 : q / 10 ^ E < (K : ℚ) + 1 := Int.lt_floor_add_one _
    have h2 : q = (q / 10 ^ E) * 10 ^ E := (div_mul_cancel₀ q (ne_of_gt hpow_pos)).symm
    rw [h2, hk_q]
    exact le_of_lt (mul_lt_mul_of_pos_right h1 hpow_pos)
  -- Existence + pinning.
  obtain ⟨n, hn_eq⟩ := Number.lower_some_of_pos_witnesses q hq_pos x hx_norm hx_pos hx_le
    y hy_norm hy_ge
  refine ⟨n, hn_eq, ?_⟩
  have h_n_le : n.toRat ≤ q := Number.lower_le q n hn_eq
  have h_x_le_n : x.toRat ≤ n.toRat := Number.lower_tight q n hn_eq x hx_norm hx_le
  have h_n_norm : n.isNormalized := Number.lower_isNormalized q n hn_eq
  by_cases h_eq : n.toRat = x.toRat
  · rw [h_eq, hx_val]
  · exfalso
    have h_n_gt : x.toRat < n.toRat := lt_of_le_of_ne h_x_le_n (Ne.symm h_eq)
    have h_n_pos : 0 < n.toRat := lt_trans hx_pos h_n_gt
    apply no_normalized_in_open_ulp_gap_pos E k hE_lo hE_hi hk_lo (by omega)
      n h_n_norm h_n_pos
    · rw [← hk_q] at hx_val
      rw [← hx_val]
      exact h_n_gt
    · have h_q_lt : q < ((k : ℚ) + 1) * 10 ^ E := by
        have h1 : q / 10 ^ E < (K : ℚ) + 1 := Int.lt_floor_add_one _
        have h2 : q = (q / 10 ^ E) * 10 ^ E := (div_mul_cancel₀ q (ne_of_gt hpow_pos)).symm
        rw [h2, hk_q]
        exact mul_lt_mul_of_pos_right h1 hpow_pos
      linarith
/-- In the lower half-decade, `Number.upper` of a positive value is its
ceiling on the `10^E` grid. -/
theorem Number.upper_eq_ceil_in_decade (q : ℚ) (E : ℤ)
    (hE_lo : minExponent ≤ E) (hE_hi : E ≤ maxExponent)
    (h_lo : (10 : ℚ) ^ (18 : ℕ) * 10 ^ E ≤ q)
    (h_hi : q < 2 * 10 ^ (18 : ℕ) * 10 ^ E) :
    ∃ n, Number.upper q = some n ∧ n.toRat = (⌈q / 10 ^ E⌉ : ℚ) * 10 ^ E := by
  have hpow_pos : (0 : ℚ) < (10 : ℚ) ^ E := zpow_pos (by norm_num) _
  have hq_pos : 0 < q := lt_of_lt_of_le (by positivity) h_lo
  have h_div_lo : (10 : ℚ) ^ (18 : ℕ) ≤ q / 10 ^ E := by
    rw [le_div_iff₀ hpow_pos]
    exact h_lo
  have h_div_hi : q / 10 ^ E < 2 * 10 ^ (18 : ℕ) := by
    rw [div_lt_iff₀ hpow_pos]
    exact h_hi
  set C : ℤ := ⌈q / 10 ^ E⌉ with hC_def
  have hC_lo : (10 ^ 18 : ℤ) ≤ C := by
    have h1 : q / 10 ^ E ≤ (C : ℚ) := Int.le_ceil _
    by_contra h_not
    push_neg at h_not
    have h2 : (C : ℚ) < ((10 ^ 18 : ℤ) : ℚ) := by exact_mod_cast h_not
    have h3 : ((10 ^ 18 : ℤ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by norm_num
    linarith
  have hC_hi : C ≤ 2 * 10 ^ 18 := by
    apply Int.ceil_le.mpr
    have : ((2 * 10 ^ 18 : ℤ) : ℚ) = 2 * 10 ^ (18 : ℕ) := by norm_num
    rw [this]
    exact le_of_lt h_div_hi
  set c : ℕ := C.toNat with hc_def
  have hc_C : (c : ℤ) = C := Int.toNat_of_nonneg (by omega)
  have hc_lo : 10 ^ 18 ≤ c := by omega
  have hc_hi : c ≤ 2 * 10 ^ 18 := by omega
  have hc_fit : c < 2 ^ 64 := by omega
  have hc_q : ((c : ℕ) : ℚ) = (C : ℚ) := by exact_mod_cast hc_C
  -- Bracketing grid points: x below (the floor), y = ceil above.
  set y : Number := gridPt c E hc_fit with hy_def
  have hy_norm : y.isNormalized := gridPt_isNormalized c E hc_fit hc_lo hc_hi hE_lo hE_hi
  have hy_val : y.toRat = (C : ℚ) * 10 ^ E := by rw [hy_def, gridPt_toRat, hc_q]
  have hy_ge : q ≤ y.toRat := by
    rw [hy_val]
    have h1 : q / 10 ^ E ≤ (C : ℚ) := Int.le_ceil _
    calc q = (q / 10 ^ E) * 10 ^ E := (div_mul_cancel₀ q (ne_of_gt hpow_pos)).symm
      _ ≤ (C : ℚ) * 10 ^ E := mul_le_mul_of_nonneg_right h1 (le_of_lt hpow_pos)
  -- Lower witness: the floor grid point.
  have h18_fit : (10 ^ 18 : ℕ) < 2 ^ 64 := by norm_num
  set x : Number := gridPt (10 ^ 18) E h18_fit with hx_def
  have hx_norm : x.isNormalized :=
    gridPt_isNormalized _ E h18_fit (le_refl _) (by norm_num) hE_lo hE_hi
  have hx_val : x.toRat = (10 : ℚ) ^ (18 : ℕ) * 10 ^ E := by
    rw [hx_def, gridPt_toRat]
    norm_num
  have hx_le : x.toRat ≤ q := by rw [hx_val]; exact h_lo
  have hx_pos : 0 < x.toRat := by rw [hx_val]; positivity
  obtain ⟨n, hn_eq⟩ := Number.upper_some_of_pos_witnesses q hq_pos x hx_norm hx_pos hx_le
    y hy_norm hy_ge
  refine ⟨n, hn_eq, ?_⟩
  have h_n_ge : q ≤ n.toRat := Number.upper_ge q n hn_eq
  have h_n_le_y : n.toRat ≤ y.toRat := Number.upper_tight q n hn_eq y hy_norm hy_ge
  have h_n_norm : n.isNormalized := Number.upper_isNormalized q n hn_eq
  by_cases h_eq : n.toRat = y.toRat
  · rw [h_eq, hy_val]
  · exfalso
    have h_n_lt : n.toRat < y.toRat := lt_of_le_of_ne h_n_le_y h_eq
    -- n ∈ [q, C·10^E): then C−1 < q/10^E ≤ n/10^E < C puts n in the open gap (C−1, C)·10^E.
    have h_n_pos : 0 < n.toRat := lt_of_lt_of_le hq_pos h_n_ge
    have hCm1_lt : ((C : ℚ) - 1) * 10 ^ E < q := by
      have h1 : (C : ℚ) - 1 < q / 10 ^ E := by
        have := Int.ceil_lt_add_one (q / 10 ^ E)
        have h2 : ((C - 1 : ℤ) : ℚ) < q / 10 ^ E := by
          by_contra h_not
          push_neg at h_not
          have h3 : ⌈q / 10 ^ E⌉ ≤ C - 1 := Int.ceil_le.mpr h_not
          omega
        push_cast at h2
        linarith
      calc ((C : ℚ) - 1) * 10 ^ E < (q / 10 ^ E) * 10 ^ E :=
            mul_lt_mul_of_pos_right h1 hpow_pos
        _ = q := div_mul_cancel₀ q (ne_of_gt hpow_pos)
    have hc1_lo : (10 : ℕ) ^ 18 ≤ c - 1 := by
      -- C ≥ 10^18 + 1 here: if C = 10^18 then n < y = 10^18·10^E ≤ q ≤ n, contradiction.
      rcases Nat.lt_or_ge (10 ^ 18) c with h | h
      · omega
      · exfalso
        have hc_eq : c = 10 ^ 18 := by omega
        have : y.toRat = (10 : ℚ) ^ (18 : ℕ) * 10 ^ E := by
          rw [hy_val, ← hc_q, hc_eq]
          norm_num
        rw [this] at h_n_lt
        linarith
    apply no_normalized_in_open_ulp_gap_pos E (c - 1) hE_lo hE_hi hc1_lo (by omega)
      n h_n_norm h_n_pos
    · have h_eq2 : ((c - 1 : ℕ) : ℚ) = (C : ℚ) - 1 := by
        have : ((c - 1 : ℕ) : ℤ) = C - 1 := by omega
        exact_mod_cast this
      rw [h_eq2]
      linarith
    · have h_eq2 : ((c - 1 : ℕ) : ℚ) + 1 = (C : ℚ) := by
        have : ((c - 1 : ℕ) : ℤ) = C - 1 := by omega
        have h3 : ((c - 1 : ℕ) : ℚ) = (C : ℚ) - 1 := by exact_mod_cast this
        linarith
      rw [h_eq2, ← hy_val]
      exact h_n_lt

/-- Negative-side mirror: `lower` of a negative value in the half-decade is
`−(ceil of the magnitude)`, i.e. the floor on the `10^E` grid. -/
theorem Number.lower_eq_floor_in_decade_neg (q : ℚ) (E : ℤ)
    (hE_lo : minExponent ≤ E) (hE_hi : E ≤ maxExponent)
    (h_lo : (10 : ℚ) ^ (18 : ℕ) * 10 ^ E ≤ -q)
    (h_hi : -q < 2 * 10 ^ (18 : ℕ) * 10 ^ E) :
    ∃ n, Number.lower q = some n ∧ n.toRat = (⌊q / 10 ^ E⌋ : ℚ) * 10 ^ E := by
  have hpow_pos : (0 : ℚ) < (10 : ℚ) ^ E := zpow_pos (by norm_num) _
  have hq_neg : q < 0 := by nlinarith [pow_pos (show (0:ℚ) < 10 by norm_num) 18]
  obtain ⟨m, hm_eq, hm_val⟩ := Number.upper_eq_ceil_in_decade (-q) E hE_lo hE_hi h_lo h_hi
  -- lower q = (upperPosAux (−q)).map (negate); upper (−q) routes through upperPosAux.
  have h_up_unfold : Number.upper (-q) = upperPosAux (-q) := by
    unfold Number.upper
    rw [if_neg (by intro h; linarith : ¬ (-q : ℚ) = 0),
        if_neg (by push_neg; linarith : ¬ (-q : ℚ) < 0)]
  rw [h_up_unfold] at hm_eq
  rw [Number.lower_neg_eq q hq_neg, hm_eq]
  refine ⟨{ m with negative_ := true }, rfl, ?_⟩
  have hm_pos : 0 < m.toRat := by
    rw [hm_val]
    have h1 : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) * 10 ^ E := by positivity
    have h2 : (10 : ℚ) ^ (18 : ℕ) ≤ ((⌈-q / 10 ^ E⌉ : ℤ) : ℚ) := by
      have h3 : (10 : ℚ) ^ (18 : ℕ) ≤ -q / 10 ^ E := by
        rw [le_div_iff₀ hpow_pos]
        exact h_lo
      calc (10 : ℚ) ^ (18 : ℕ) ≤ -q / 10 ^ E := h3
        _ ≤ _ := Int.le_ceil _
    nlinarith
  have hm_neg_false : m.negative_ = false := by
    rcases hmn : m.negative_ with _ | _
    · rfl
    · exfalso
      have := Number.toRat_nonpos_of_negative m hmn
      linarith
  have h_flip : ({ m with negative_ := true } : Number).toRat = -m.toRat :=
    Number.toRat_set_neg_true_of_nn m hm_neg_false
  rw [h_flip, hm_val]
  have h_ceil_neg : ⌈-q / 10 ^ E⌉ = -⌊q / 10 ^ E⌋ := by
    rw [show -q / 10 ^ E = -(q / 10 ^ E) from by ring, Int.ceil_neg]
  rw [h_ceil_neg]
  push_cast
  ring

/-- Negative-side mirror: `upper` of a negative value in the half-decade is
`−(floor of the magnitude)`, i.e. the ceiling on the `10^E` grid. -/
theorem Number.upper_eq_ceil_in_decade_neg (q : ℚ) (E : ℤ)
    (hE_lo : minExponent ≤ E) (hE_hi : E ≤ maxExponent)
    (h_lo : (10 : ℚ) ^ (18 : ℕ) * 10 ^ E ≤ -q)
    (h_hi : -q < 2 * 10 ^ (18 : ℕ) * 10 ^ E) :
    ∃ n, Number.upper q = some n ∧ n.toRat = (⌈q / 10 ^ E⌉ : ℚ) * 10 ^ E := by
  have hpow_pos : (0 : ℚ) < (10 : ℚ) ^ E := zpow_pos (by norm_num) _
  have hq_neg : q < 0 := by nlinarith [pow_pos (show (0:ℚ) < 10 by norm_num) 18]
  obtain ⟨m, hm_eq, hm_val⟩ := Number.lower_eq_floor_in_decade (-q) E hE_lo hE_hi h_lo h_hi
  have h_lo_unfold : Number.lower (-q) = lowerPosAux (-q) := by
    unfold Number.lower
    rw [if_neg (by intro h; linarith : ¬ (-q : ℚ) = 0),
        if_neg (by push_neg; linarith : ¬ (-q : ℚ) < 0)]
  rw [h_lo_unfold] at hm_eq
  rw [Number.upper_neg_eq q hq_neg, hm_eq]
  simp only [Option.map_some]
  have hm_pos : 0 < m.toRat := by
    rw [hm_val]
    have h2 : (10 : ℚ) ^ (18 : ℕ) ≤ ((⌊-q / 10 ^ E⌋ : ℤ) : ℚ) := by
      have h3 : ((10 ^ 18 : ℤ) : ℚ) ≤ ((⌊-q / 10 ^ E⌋ : ℤ) : ℚ) := by
        have : (10 ^ 18 : ℤ) ≤ ⌊-q / 10 ^ E⌋ := by
          apply Int.le_floor.mpr
          have h4 : (10 : ℚ) ^ (18 : ℕ) ≤ -q / 10 ^ E := by
            rw [le_div_iff₀ hpow_pos]
            exact h_lo
          calc ((10 ^ 18 : ℤ) : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by norm_num
            _ ≤ -q / 10 ^ E := h4
        exact_mod_cast this
      calc (10 : ℚ) ^ (18 : ℕ) = ((10 ^ 18 : ℤ) : ℚ) := by norm_num
        _ ≤ _ := h3
    nlinarith
  have hm_ne_zero : m ≠ Number.zero := by
    intro h_eq
    rw [h_eq, Number.toRat_zero] at hm_pos
    exact lt_irrefl 0 hm_pos
  rw [if_neg hm_ne_zero]
  have hm_neg_false : m.negative_ = false := by
    rcases hmn : m.negative_ with _ | _
    · rfl
    · exfalso
      have := Number.toRat_nonpos_of_negative m hmn
      linarith
  refine ⟨{ m with negative_ := true }, rfl, ?_⟩
  have h_flip : ({ m with negative_ := true } : Number).toRat = -m.toRat :=
    Number.toRat_set_neg_true_of_nn m hm_neg_false
  rw [h_flip, hm_val]
  have h_floor_neg : ⌊-q / 10 ^ E⌋ = -⌈q / 10 ^ E⌉ := by
    rw [show -q / 10 ^ E = -(q / 10 ^ E) from by ring, Int.floor_neg]
  rw [h_floor_neg]
  push_cast
  ring

end XRPL.Model.Protocol

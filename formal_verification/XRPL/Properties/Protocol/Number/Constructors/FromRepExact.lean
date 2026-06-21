import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize
import XRPL.Properties.Protocol.Number.Common.Rounding.SmallRange
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Normalize.Common.Underflow
import XRPL.Properties.Protocol.Number.Constructors.Common.Proofs

/-! # `from_rep` is value-exact on any in-range `Int64` mantissa

`from_rep` (hence `IOUAmount.toNumber`) only ever scales an `Int64` mantissa **up** into the
internal `largeRange = [10¹⁸, 10¹⁹)`, which is exact, with no rounding. The single
exception is `mantissa = Int64.minValue`, whose magnitude `2⁶³` lands in the cusp
`(maxRep, maxRepUp)` where `pushOverflow` rounds; we exclude it via `m ≤ maxRep`.

This file proves the exact-value characterization and feeds the hypothesis-minimal
`IOUAmount` comparison headlines. -/

namespace XRPL.Model.Protocol

/-- `doNormalize_scaleUp` never drops the exponent below `minExponent`. -/
lemma doNormalize_scaleUp_exp_ge (minMant m : UInt64) (e : Int) :
    minExponent ≤ e → minExponent ≤ (doNormalize_scaleUp minMant m e).2 := by
  induction m, e using doNormalize_scaleUp.induct (minMantissa := minMant) with
  | case1 m e hcond IH =>
    intro _
    rw [doNormalize_scaleUp, if_pos hcond]
    exact IH (by obtain ⟨_, h⟩ := hcond; omega)
  | case2 m e hcond =>
    intro he
    rw [doNormalize_scaleUp, if_neg hcond]; exact he

/-- `doNormalize_scaleUp` does not increase the exponent. -/
lemma doNormalize_scaleUp_exp_le (minMant m : UInt64) (e : Int) :
    (doNormalize_scaleUp minMant m e).2 ≤ e := by
  induction m, e using doNormalize_scaleUp.induct (minMantissa := minMant) with
  | case1 m e hcond IH =>
    rw [doNormalize_scaleUp, if_pos hcond]; omega
  | case2 m e hcond =>
    rw [doNormalize_scaleUp, if_neg hcond]

/-- With `minMant ≤ 10¹⁸`, `doNormalize_scaleUp` keeps the mantissa below `10¹⁹`. -/
lemma doNormalize_scaleUp_lt19 (minMant m : UInt64) (e : Int)
    (hminMant : minMant.toNat ≤ 10 ^ 18) :
    m.toNat < 10 ^ 19 → (doNormalize_scaleUp minMant m e).1.toNat < 10 ^ 19 := by
  induction m, e using doNormalize_scaleUp.induct (minMantissa := minMant) with
  | case1 m e hcond IH =>
    intro _
    rw [doNormalize_scaleUp, if_pos hcond]
    obtain ⟨hlt, _⟩ := hcond
    have hmlt : m.toNat < minMant.toNat := UInt64.lt_iff_toNat_lt.mp hlt
    have hm10 : (m * 10).toNat = m.toNat * 10 := by
      rw [UInt64.toNat_mul, uint64_ten_toNat]; exact Nat.mod_eq_of_lt (by omega)
    exact IH (by rw [hm10]; omega)
  | case2 m e hcond =>
    intro hm; rw [doNormalize_scaleUp, if_neg hcond]; exact hm

/-- `doNormalize_scaleUp` exits either at the input (no scaling) or at a multiple of 10
(at least one `×10` step). Requires `minMant ≤ 10¹⁸` so the scaling steps don't overflow. -/
lemma doNormalize_scaleUp_id_or_mul10 (minMant m : UInt64) (e : Int)
    (hminMant : minMant.toNat ≤ 10 ^ 18) (hm19 : m.toNat < 10 ^ 19) :
    (doNormalize_scaleUp minMant m e).1 = m ∨ (doNormalize_scaleUp minMant m e).1.toNat % 10 = 0 := by
  induction m, e using doNormalize_scaleUp.induct (minMantissa := minMant) with
  | case1 m e hcond IH =>
    rw [doNormalize_scaleUp, if_pos hcond]
    obtain ⟨hlt, _⟩ := hcond
    have hmlt : m.toNat < minMant.toNat := UInt64.lt_iff_toNat_lt.mp hlt
    have hm10 : (m * 10).toNat = m.toNat * 10 := by
      rw [UInt64.toNat_mul, uint64_ten_toNat]; exact Nat.mod_eq_of_lt (by omega)
    rcases IH (by rw [hm10]; omega) with h | h
    · right; rw [h, hm10]; omega
    · right; exact h
  | case2 m e hcond =>
    left; rw [doNormalize_scaleUp, if_neg hcond]

set_option maxHeartbeats 800000 in
-- unfolds the full normalize pipeline (scaleUp / scaleDown / capAtMaxRep / doRoundUp)
/-- **`doNormalize` is value-exact** on any `Int64`-range mantissa `m ∈ [1, maxRep]`
(i.e. `m ≠ 2⁶³`) with enough exponent room: it scales `m` up into `largeRange` with no
rounding, returning a normalized number of the same value `m · 10^e`, in every mode. -/
lemma doNormalize_largeRange_exact (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_pos : 1 ≤ m.toNat) (h_le : m.toNat ≤ maxRep.toNat)
    (he_lo : minExponent + 18 ≤ e) (he_hi : e ≤ maxExponent - 1) :
    ∃ M : UInt64, ∃ e1 : Int,
      doNormalize neg m e largeRange.min largeRange.max mode = .ok ⟨neg, M, e1⟩ ∧
      (M.toNat : ℚ) * 10 ^ e1 = (m.toNat : ℚ) * 10 ^ e ∧ Number.isnormal ⟨neg, M, e1⟩ := by
  have hminN : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxN : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hmaxRepN : maxRep.toNat = 9223372036854775807 := maxRep_val
  have hmaxRepUpN : maxRepUp.toNat = 9223372036854775810 := rfl
  obtain ⟨M, e1, hMe1⟩ : ∃ M e1, doNormalize_scaleUp largeRange.min m e = (M, e1) := ⟨_, _, rfl⟩
  -- value preservation
  have hval' : (M.toNat : ℚ) * 10 ^ e1 = (m.toNat : ℚ) * 10 ^ e := by
    have hval := doNormalize_scaleUp_value largeRange.min m e (by rw [hminN]; norm_num)
    simp only [hMe1] at hval; exact hval
  -- mantissa bounds after scaleUp
  have hM19 : M.toNat < 10 ^ 19 := by
    have h := doNormalize_scaleUp_lt19 largeRange.min m e (by rw [hminN]; norm_num)
      (by rw [hmaxRepN] at h_le; omega)
    simp only [hMe1] at h; exact h
  have hM_le_max : M ≤ largeRange.max := by rw [UInt64.le_iff_toNat_le, hmaxN]; omega
  have he1_le : e1 ≤ e := by
    have h := doNormalize_scaleUp_exp_le largeRange.min m e; simp only [hMe1] at h; exact h
  have he1_ge : minExponent ≤ e1 := by
    have h := doNormalize_scaleUp_exp_ge largeRange.min m e (by omega); simp only [hMe1] at h; exact h
  -- the keystone lower bound `M ≥ 10^18`, from exit-exponent + value + exponent room
  have hM_lo : 10 ^ 18 ≤ M.toNat := by
    by_contra hlt
    push_neg at hlt
    have hMlt : M < largeRange.min := by rw [UInt64.lt_iff_toNat_lt, hminN]; omega
    have hexit := doNormalize_scaleUp_exit_exp largeRange.min m e
    simp only [hMe1] at hexit
    have he1_min : e1 ≤ minExponent := hexit hMlt
    have h10e_pos : (0 : ℚ) < 10 ^ e := by positivity
    have hm_ge : (10 : ℚ) ^ e ≤ (m.toNat : ℚ) * 10 ^ e := by
      have h1 : (1 : ℚ) ≤ (m.toNat : ℚ) := by exact_mod_cast h_pos
      nlinarith [h10e_pos]
    have hMq : (M.toNat : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by exact_mod_cast hlt
    have he1e : e1 ≤ e - 18 := by omega
    have hzp : (10 : ℚ) ^ e1 ≤ (10 : ℚ) ^ (e - 18) := zpow_le_zpow_right₀ (by norm_num) he1e
    have hsplit : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (e - 18) = (10 : ℚ) ^ e := by
      rw [show ((10 : ℚ) ^ (18 : ℕ)) = (10 : ℚ) ^ (18 : ℤ) by norm_num,
          ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      ring_nf
    have hMpos : (0 : ℚ) ≤ (M.toNat : ℚ) := by positivity
    have he1pos : (0 : ℚ) < (10 : ℚ) ^ e1 := by positivity
    have hbound : (M.toNat : ℚ) * 10 ^ e1 < (10 : ℚ) ^ e := by
      calc (M.toNat : ℚ) * 10 ^ e1 < (10 : ℚ) ^ (18 : ℕ) * 10 ^ e1 := by
            apply mul_lt_mul_of_pos_right hMq he1pos
        _ ≤ (10 : ℚ) ^ (18 : ℕ) * 10 ^ (e - 18) := by
            apply mul_le_mul_of_nonneg_left hzp (by positivity)
        _ = (10 : ℚ) ^ e := hsplit
    rw [hval'] at hbound
    linarith [hm_ge, hbound]
  have hM_or : M.toNat ≤ maxRep.toNat ∨ M.toNat % 10 = 0 := by
    rcases doNormalize_scaleUp_id_or_mul10 largeRange.min m e (by rw [hminN]; norm_num)
      (by rw [hmaxRepN] at h_le; omega) with h | h
    · simp only [hMe1] at h; left; rw [h]; exact h_le
    · simp only [hMe1] at h; right; exact h
  -- result is normalized
  have hnormal : Number.isnormal ⟨neg, M, e1⟩ := by
    right
    refine ⟨?_, hM_le_max, ?_, he1_ge, ?_⟩
    · show largeRange.min ≤ M
      rw [UInt64.le_iff_toNat_le, hminN]; omega
    · show M ≤ maxRep ∨ M.toNat % 10 = 0
      rcases hM_or with h | h
      · left; rw [UInt64.le_iff_toNat_le]; exact h
      · right; exact h
    · show e1 ≤ maxExponent
      omega
  refine ⟨M, e1, ?_, hval', hnormal⟩
  -- run the doNormalize pipeline: scaleUp lands at (M, e1); scaleDown is identity; the
  -- empty guard makes capAtMaxRep / doRoundUp exact (both arms).
  have hm_ne : ¬ (m == 0) = true := by
    intro h; have : m = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at h_pos; simp at h_pos
  set g0 : Guard := if neg then Guard.new.set_negative else Guard.new with hg0_def
  have hg0_empty : g0.empty = true := by rw [hg0_def]; cases neg <;> decide
  unfold doNormalize
  rw [show (m == 0) = false from Bool.eq_false_iff.mpr hm_ne]
  simp only [Bool.false_eq_true, if_false]
  rw [hMe1]
  simp only []
  rw [← hg0_def, doNormalize_scaleDown_id largeRange.max M e1 g0 hM_le_max]
  simp only []
  have h_zero_check : (decide (e1 < minExponent) || decide (M < largeRange.min)) = false := by
    rw [Bool.or_eq_false_iff]
    refine ⟨decide_eq_false (by omega), decide_eq_false ?_⟩
    rw [UInt64.lt_iff_toNat_lt, hminN]; omega
  rw [h_zero_check]
  simp only [Bool.false_eq_true, if_false]
  by_cases h_cap : M > maxRepUp
  · -- divide arm: M is a multiple of 10 (since M > maxRepUp > maxRep ≥ m forces a scale step)
    have hM_gt_maxRep : maxRep < M := by
      rw [UInt64.lt_iff_toNat_lt, hmaxRepN]
      have := UInt64.lt_iff_toNat_lt.mp h_cap; rw [hmaxRepUpN] at this; omega
    have hM_mod : M.toNat % 10 = 0 := by
      rcases hM_or with h | h
      · exfalso; have := UInt64.lt_iff_toNat_lt.mp hM_gt_maxRep; omega
      · exact h
    have h_cap_eq : doNormalize_capAtMaxRep M e1 g0
        = .ok (M / 10, e1 + 1, g0.push (M % 10)) := by
      unfold doNormalize_capAtMaxRep
      rw [if_pos h_cap, if_neg (by omega : ¬ (e1 ≥ maxExponent))]; rfl
    rw [h_cap_eq]
    simp only []
    have hMmod10 : (M % 10) = 0 := by
      rw [← UInt64.toNat_inj, UInt64.toNat_mod, uint64_ten_toNat]; exact hM_mod
    have h_g_empty : (g0.push (M % 10)).empty = true := Guard.push_zero_empty hg0_empty hMmod10
    rw [empty_guard_doRoundUp_divu10 _ mode neg M e1 _ h_g_empty hM_gt_maxRep
        (by rw [hmaxN]; omega) hM_mod he1_ge (by omega)]
    rfl
  · -- keep arm: capAtMaxRep is identity, M avoids the (maxRep, maxRepUp) cusp
    have h_cap_eq : doNormalize_capAtMaxRep M e1 g0 = .ok (M, e1, g0) := by
      unfold doNormalize_capAtMaxRep; rw [if_neg h_cap]
    rw [h_cap_eq]
    simp only []
    have h_no_cusp : ¬ (maxRep < M ∧ M < maxRepUp) := by
      rintro ⟨h1, h2⟩
      have hMr := UInt64.lt_iff_toNat_lt.mp h1
      have hMu := UInt64.lt_iff_toNat_lt.mp h2
      rw [hmaxRepN] at hMr; rw [hmaxRepUpN] at hMu
      rcases hM_or with h | h <;> omega
    rw [empty_guard_doRoundUp_id g0 mode neg M e1 _ hg0_empty
        (by rw [UInt64.le_iff_toNat_le, hminN]; omega) h_no_cusp he1_ge (by omega)]
    rfl

/-- **`from_rep` is value-exact** on any `Int64` mantissa except `Int64.minValue`
(whose magnitude `2⁶³` falls in the `pushOverflow` cusp), given enough exponent room:
the result equals `mantissa · 10^e` and is normalized, in every mode. -/
theorem Number.from_rep_exact (mantissa : Int64) (e : Int) (mode : rounding_mode)
    (h_ne_min : mantissa ≠ Int64.minValue)
    (he_lo : minExponent + 18 ≤ e) (he_hi : e ≤ maxExponent - 1) :
    ∃ result : Number,
      Number.from_rep mantissa e largeRange.min largeRange.max mode = .ok result ∧
      result.toRat = (mantissa.toInt : ℚ) * 10 ^ e ∧ result.isNormalized := by
  set neg : Bool := decide (mantissa < 0) with hneg_def
  set m : UInt64 := mantissa.toInt.natAbs.toUInt64 with hm_def
  have hfr : Number.from_rep mantissa e largeRange.min largeRange.max mode
      = doNormalize neg m e largeRange.min largeRange.max mode := rfl
  have hb_lo : -9223372036854775808 ≤ mantissa.toInt := Int64.le_toInt mantissa
  have hb_hi : mantissa.toInt < 9223372036854775808 := Int64.toInt_lt mantissa
  have hmt : m.toNat = mantissa.toInt.natAbs :=
    UInt64.toNat_ofNat_of_lt (by show mantissa.toInt.natAbs < 2 ^ 64; omega)
  by_cases hz : mantissa.toInt = 0
  · -- zero mantissa → `Number.zero`, value `0`
    refine ⟨Number.zero, ?_, ?_, Or.inl rfl⟩
    · rw [hfr]; unfold doNormalize
      have : m == 0 := by
        rw [beq_iff_eq, ← UInt64.toNat_inj, hmt, hz]; rfl
      rw [if_pos this]
    · rw [Number.toRat_zero, hz]; push_cast; ring
  · -- nonzero mantissa: apply the general exactness lemma
    have hpos : 1 ≤ m.toNat := by rw [hmt]; omega
    have hmin_val : Int64.minValue.toInt = (-9223372036854775808 : ℤ) := by decide
    have h_ne : mantissa.toInt ≠ Int64.minValue.toInt :=
      fun h => h_ne_min (Int64.toInt_inj.mp h)
    have hle : m.toNat ≤ maxRep.toNat := by
      rw [hmt, maxRep_val]; rw [hmin_val] at h_ne; omega
    obtain ⟨M, e1, hok, hval, hnorm⟩ :=
      doNormalize_largeRange_exact neg m e mode hpos hle he_lo he_hi
    refine ⟨⟨neg, M, e1⟩, by rw [hfr]; exact hok, ?_, hnorm⟩
    have hin : (Number.unchecked neg m e).toRat = (mantissa.toInt : ℚ) * 10 ^ e :=
      Number.from_rep_input_toRat mantissa e
    rw [Number.unchecked_toRat] at hin
    show (Number.unchecked neg M e1).toRat = (mantissa.toInt : ℚ) * 10 ^ e
    rw [Number.unchecked_toRat, ← hin, hval]

end XRPL.Model.Protocol

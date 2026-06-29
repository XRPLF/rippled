import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.AlgorithmicFacts


namespace XRPL.Model.Protocol

/-! # The 16-digit (`cMinValue`/`cMaxValue`) `doNormalize` instantiation

`IOUAmount.fromNumber` runs `Number.normalizeToRange cMinValue cMaxValue`, i.e.
`doNormalize` at the 16-digit mantissa range. For the inputs the
`STAmount.roundToScale` proof produces — 19-digit normalized mantissas — the
pipeline is a 3-step `scaleDown` followed by a cusp-free `doRoundUp`: every
16-digit mantissa sits far below `maxRep`, so `pushOverflow`, the cusp
branches, and `capAtMaxRep` are all dead. -/

lemma cMinValue_val : cMinValue.toNat = 1000000000000000 := by decide

lemma cMaxValue_val : cMaxValue.toNat = 9999999999999999 := by decide

/-- `push` never touches the sign bit. -/
lemma Guard.push_sbit (g : Guard) (d : UInt64) : (g.push d).sbit_ = g.sbit_ := rfl

/-- Pushing a zero digit preserves guard emptiness. -/
lemma Guard.push_zero_empty {g : Guard} (h : g.empty = true) {d : UInt64} (hd : d = 0) :
    (g.push d).empty = true := by
  subst hd
  obtain ⟨dg, xb, sb⟩ := g
  unfold Guard.empty at h
  rw [Bool.and_eq_true, beq_iff_eq] at h
  obtain ⟨hdig, hxb⟩ := h
  simp only at hdig hxb
  have hxb' : xb = false := by
    cases hx : xb
    · rfl
    · rw [hx] at hxb; exact absurd hxb (by decide)
  subst hdig hxb'
  simp only [Guard.push, Guard.empty]
  decide

/-- One firing step of `doNormalize_scaleDown`. -/
lemma doNormalize_scaleDown_step {maxMant M : UInt64} {e : Int} {g : Guard}
    (hgt : maxMant < M) (he : e < maxExponent) :
    doNormalize_scaleDown maxMant M e g
      = doNormalize_scaleDown maxMant (M / 10) (e + 1) (g.push (M % 10)) := by
  conv_lhs => unfold doNormalize_scaleDown
  rw [dif_pos hgt, if_neg (not_le.mpr he)]

/-- For a 19-digit mantissa, `scaleDown` at `cMaxValue` fires exactly three
times, dropping the three low decimal digits into the guard. -/
lemma doNormalize_scaleDown_three (M : UInt64) (e : Int) (g : Guard)
    (hM_lo : 10 ^ 18 ≤ M.toNat) (hM_hi : M.toNat < 10 ^ 19)
    (he : e + 3 ≤ maxExponent) :
    doNormalize_scaleDown cMaxValue M e g
      = .ok (M / 10 / 10 / 10, e + 3,
             ((g.push (M % 10)).push (M / 10 % 10)).push (M / 10 / 10 % 10)) := by
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  have hm1 : (M / 10).toNat = M.toNat / 10 := by rw [UInt64.toNat_div, h10]
  have hm2 : (M / 10 / 10).toNat = M.toNat / 100 := by
    rw [UInt64.toNat_div, hm1, h10, Nat.div_div_eq_div_mul]
  have hm3 : (M / 10 / 10 / 10).toNat = M.toNat / 1000 := by
    rw [UInt64.toNat_div, hm2, h10, Nat.div_div_eq_div_mul]
  rw [doNormalize_scaleDown_step
        (by rw [UInt64.lt_iff_toNat_lt, cMaxValue_val]; omega)
        (by omega),
      doNormalize_scaleDown_step
        (by rw [UInt64.lt_iff_toNat_lt, cMaxValue_val, hm1]; omega)
        (by omega),
      doNormalize_scaleDown_step
        (by rw [UInt64.lt_iff_toNat_lt, cMaxValue_val, hm2]; omega)
        (by omega),
      doNormalize_scaleDown_id _ _ _ _
        (by rw [UInt64.le_iff_toNat_le, cMaxValue_val, hm3]; omega)]
  have he3 : e + 1 + 1 + 1 = e + 3 := by ring
  rw [he3]

/-- The fraction collected by the three dropped digits is `(M % 1000) / 1000`. -/
lemma represents_three_pushes {g : Guard} (hg : represents g 0) (M : UInt64) :
    represents (((g.push (M % 10)).push (M / 10 % 10)).push (M / 10 / 10 % 10))
      (((M.toNat % 1000 : ℕ) : ℚ) / 1000) := by
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  have hd0 : (M % 10).toNat = M.toNat % 10 := by rw [UInt64.toNat_mod, h10]
  have hd1 : (M / 10 % 10).toNat = M.toNat / 10 % 10 := by
    rw [UInt64.toNat_mod, UInt64.toNat_div, h10]
  have hd2 : (M / 10 / 10 % 10).toNat = M.toNat / 100 % 10 := by
    rw [UInt64.toNat_mod, UInt64.toNat_div, UInt64.toNat_div, h10, Nat.div_div_eq_div_mul]
  have h1 := represents_push hg (d := M % 10) (by rw [hd0]; omega)
  have h2 := represents_push h1 (d := M / 10 % 10) (by rw [hd1]; omega)
  have h3 := represents_push h2 (d := M / 10 / 10 % 10) (by rw [hd2]; omega)
  have h_eq : ((((0 : ℚ) + ((M % 10).toNat : ℚ)) / 10 + ((M / 10 % 10).toNat : ℚ)) / 10
        + ((M / 10 / 10 % 10).toNat : ℚ)) / 10
      = ((M.toNat % 1000 : ℕ) : ℚ) / 1000 := by
    rw [hd0, hd1, hd2]
    have hdecomp : M.toNat % 1000
        = M.toNat / 100 % 10 * 100 + M.toNat / 10 % 10 * 10 + M.toNat % 10 := by omega
    rw [hdecomp]
    push_cast
    ring
  rw [h_eq] at h3
  exact h3

/-- 16-digit `doRoundUp`, truncating branch: the round decision is off, the
mantissa is kept. Mode-generic (the decision enters through `hb`). -/
lemma doRoundUp_small_truncate (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (mode : rounding_mode) (loc : String)
    (hb : (g.round mode == 1 || (g.round mode == 0 && m % 2 == 1)) = false)
    (hmin : cMinValue.toNat ≤ m.toNat) (hmax : m.toNat ≤ cMaxValue.toNat)
    (hexp_lo : minExponent ≤ e) (hexp_hi : e ≤ maxExponent) :
    g.doRoundUp neg m e cMinValue cMaxValue mode loc
      = .ok { negative_ := neg, mantissa_ := m, exponent_ := e } := by
  have h_le_maxRep : m.toNat ≤ maxRep.toNat := by
    rw [maxRep_val]; rw [cMaxValue_val] at hmax; omega
  have h_no_cusp : ¬ (maxRep < m ∧ m < maxRepUp) := by
    intro ⟨h1, _⟩
    exact absurd (UInt64.lt_iff_toNat_lt.mp h1) (not_lt.mpr h_le_maxRep)
  have h_m_ne : m ≠ 0 := by
    intro h
    rw [h] at hmin
    rw [cMinValue_val] at hmin
    simp at hmin
  have h_lt_maxRep : m.toNat < maxRep.toNat := by
    rw [maxRep_val]; rw [cMaxValue_val] at hmax; omega
  unfold Guard.doRoundUp
  simp only []
  rw [pushOverflow_noop_of_lt_maxRep h_lt_maxRep g mode, hb]
  simp only [Bool.false_eq_true, if_false]
  rw [if_neg h_no_cusp]
  rw [bringIntoRange_noscale_result (fun h => absurd (UInt64.lt_iff_toNat_lt.mp h.1)
    (not_lt.mpr hmin))]
  rw [if_neg (not_or.mpr ⟨not_lt.mpr hexp_lo, h_m_ne⟩)]
  rw [if_neg (not_lt.mpr hexp_hi)]

/-- 16-digit `doRoundUp`, firing branch: the round decision is on and the
mantissa has headroom (`m < cMaxValue`), so it increments in place. -/
lemma doRoundUp_small_fire (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (mode : rounding_mode) (loc : String)
    (hb : (g.round mode == 1 || (g.round mode == 0 && m % 2 == 1)) = true)
    (hmin : cMinValue.toNat ≤ m.toNat) (hmax : m.toNat < cMaxValue.toNat)
    (hexp_lo : minExponent ≤ e) (hexp_hi : e ≤ maxExponent) :
    g.doRoundUp neg m e cMinValue cMaxValue mode loc
      = .ok { negative_ := neg, mantissa_ := m + 1, exponent_ := e } := by
  have h_le_maxRep : m.toNat ≤ maxRep.toNat := by
    rw [maxRep_val]; rw [cMaxValue_val] at hmax; omega
  have h_add_one : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow h_le_maxRep
  have h_branch : m < cMaxValue ∧ m < maxRep := by
    constructor
    · rw [UInt64.lt_iff_toNat_lt]; exact hmax
    · rw [UInt64.lt_iff_toNat_lt, maxRep_val]
      rw [cMaxValue_val] at hmax; omega
  have h_m1_ne : m + 1 ≠ 0 := by
    intro h
    have : (m + 1).toNat = 0 := by rw [h]; rfl
    omega
  have h_lt_maxRep2 : m.toNat < maxRep.toNat := UInt64.lt_iff_toNat_lt.mp h_branch.2
  unfold Guard.doRoundUp
  simp only []
  rw [pushOverflow_noop_of_lt_maxRep h_lt_maxRep2 g mode, hb]
  simp only [if_true]
  rw [if_pos h_branch]
  rw [bringIntoRange_noscale_result (fun h => absurd (UInt64.lt_iff_toNat_lt.mp h.1)
    (by rw [not_lt, h_add_one]; omega))]
  rw [if_neg (not_or.mpr ⟨not_lt.mpr hexp_lo, h_m1_ne⟩)]
  rw [if_neg (not_lt.mpr hexp_hi)]

/-- The 16-digit `doNormalize` reduction for 19-digit inputs: the pipeline
reduces to a single `doRoundUp` over the 3-digit-truncated mantissa, with the
guard representing the dropped fraction `(M % 1000)/1000`. The guard's sign
bit tracks `neg`, and it stays empty when the dropped digits are all zero. -/
theorem doNormalize_small_facts (neg : Bool) (M : UInt64) (e : Int) (mode : rounding_mode)
    (hM_lo : 10 ^ 18 ≤ M.toNat) (hM_hi : M.toNat < 10 ^ 19)
    (hexp_lo : minExponent ≤ e + 3) (hexp_hi : e + 3 ≤ maxExponent) :
    ∃ g : Guard,
      represents g (((M.toNat % 1000 : ℕ) : ℚ) / 1000) ∧
      g.sbit_ = neg ∧
      (M.toNat % 1000 = 0 → g.empty = true) ∧
      doNormalize neg M e cMinValue cMaxValue mode
        = match g.doRoundUp neg (M / 10 / 10 / 10) (e + 3) cMinValue cMaxValue mode
                "Number::normalize 2" with
          | .error err => .error err
          | .ok res => .ok res.toNumber := by
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  have hm3 : (M / 10 / 10 / 10).toNat = M.toNat / 1000 := m_div_thousand_toNat M
  set g0 : Guard := if neg then Guard.new.set_negative else Guard.new with hg0_def
  have hg0_rep : represents g0 0 := g0_represents_zero neg
  have hg0_sbit : g0.sbit_ = neg := by
    rw [hg0_def]; cases neg <;> rfl
  have hg0_empty : g0.empty = true := by
    rw [hg0_def]; cases neg <;> decide
  set g3 : Guard := ((g0.push (M % 10)).push (M / 10 % 10)).push (M / 10 / 10 % 10)
    with hg3_def
  refine ⟨g3, represents_three_pushes hg0_rep M, ?_, ?_, ?_⟩
  · rw [hg3_def, Guard.push_sbit, Guard.push_sbit, Guard.push_sbit]
    exact hg0_sbit
  · intro hzero
    have h0nat : (0 : UInt64).toNat = 0 := rfl
    have hd0 : M % 10 = 0 := by
      rw [← UInt64.toNat_inj, UInt64.toNat_mod, h10, h0nat]
      omega
    have hd1 : M / 10 % 10 = 0 := by
      rw [← UInt64.toNat_inj, UInt64.toNat_mod, UInt64.toNat_div, h10, h0nat]
      omega
    have hd2 : M / 10 / 10 % 10 = 0 := by
      rw [← UInt64.toNat_inj, UInt64.toNat_mod, UInt64.toNat_div, UInt64.toNat_div, h10,
          Nat.div_div_eq_div_mul, h0nat]
      omega
    rw [hg3_def]
    exact Guard.push_zero_empty
      (Guard.push_zero_empty (Guard.push_zero_empty hg0_empty hd0) hd1) hd2
  · have hM_ne : ¬ (M == 0) = true := by
      intro h
      have : M = 0 := by exact_mod_cast beq_iff_eq.mp h
      rw [this] at hM_lo
      simp at hM_lo
    unfold doNormalize
    rw [show (M == 0) = false from Bool.not_eq_true _ ▸ Bool.eq_false_iff.mpr
      (fun h => hM_ne h)]
    simp only [Bool.false_eq_true, if_false]
    rw [doNormalize_scaleUp_id cMinValue M e
      (by rw [UInt64.le_iff_toNat_le, cMinValue_val]; omega)]
    simp only []
    rw [← hg0_def, doNormalize_scaleDown_three M e g0 hM_lo hM_hi hexp_hi]
    simp only []
    rw [← hg3_def]
    have h_zero_check : (decide (e + 3 < minExponent) || decide (M / 10 / 10 / 10 < cMinValue))
        = false := by
      rw [Bool.or_eq_false_iff]
      constructor
      · exact decide_eq_false (not_lt.mpr hexp_lo)
      · apply decide_eq_false
        rw [UInt64.lt_iff_toNat_lt, hm3, cMinValue_val]
        omega
    rw [h_zero_check]
    simp only [Bool.false_eq_true, if_false]
    have h_cap : doNormalize_capAtMaxRep (M / 10 / 10 / 10) (e + 3) g3
        = .ok (M / 10 / 10 / 10, e + 3, g3) := by
      unfold doNormalize_capAtMaxRep
      rw [if_neg]
      intro h
      have h1 := UInt64.lt_iff_toNat_lt.mp h
      have h2 : maxRepUp.toNat = maxRepUpNat := rfl
      omega
    rw [h_cap]
    rfl

/-! ## `doNormalize` at `largeRange` on 16-digit mantissas (the `from_rep` leg)

`IOUAmount.normalize`/`STAmount.iou` first run the signed mantissa through the
**19-digit** constructor pipeline. A 16-digit mantissa scales up by exactly
`×1000`; the result is exact in every mode. -/

/-- One firing step of `doNormalize_scaleUp`. -/
lemma doNormalize_scaleUp_step {minMant m : UInt64} {e : Int}
    (hlt : m < minMant) (he : minExponent < e) :
    doNormalize_scaleUp minMant m e = doNormalize_scaleUp minMant (m * 10) (e - 1) := by
  conv_lhs => unfold doNormalize_scaleUp
  rw [if_pos ⟨hlt, he⟩]

/-- A 16-digit mantissa scales up exactly three times at `largeRange.min`. -/
lemma doNormalize_scaleUp_three (m₀ : UInt64) (e : Int)
    (h_lo : 10 ^ 15 ≤ m₀.toNat) (h_hi : m₀.toNat < 10 ^ 16)
    (he : minExponent + 3 ≤ e) :
    doNormalize_scaleUp largeRange.min m₀ e = (m₀ * 10 * 10 * 10, e - 3) := by
  have hminN : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hm1 : (m₀ * 10).toNat = m₀.toNat * 10 :=
    m_mul_ten_no_overflow (by rw [hminN]; omega)
  have hm2 : (m₀ * 10 * 10).toNat = m₀.toNat * 100 := by
    rw [m_mul_ten_no_overflow (by rw [hminN, hm1]; omega), hm1]
    ring
  have hm3 : (m₀ * 10 * 10 * 10).toNat = m₀.toNat * 1000 := by
    rw [m_mul_ten_no_overflow (by rw [hminN, hm2]; omega), hm2]
    ring
  rw [doNormalize_scaleUp_step
        (by rw [UInt64.lt_iff_toNat_lt, hminN]; omega) (by omega),
      doNormalize_scaleUp_step
        (by rw [UInt64.lt_iff_toNat_lt, hminN, hm1]; omega) (by omega),
      doNormalize_scaleUp_step
        (by rw [UInt64.lt_iff_toNat_lt, hminN, hm2]; omega) (by omega),
      doNormalize_scaleUp_id _ _ _
        (by rw [UInt64.le_iff_toNat_le, hminN, hm3]; omega)]
  have he3 : e - 1 - 1 - 1 = e - 3 := by ring
  rw [he3]

/-- `doNormalize` at `largeRange` of a 16-digit mantissa: exact `×1000` shift,
every mode. (Both `capAtMaxRep` branches land on the same record: the keep arm
directly, the divide arm via the `bringIntoRange` rescale.) -/
theorem doNormalize_large_16digit (neg : Bool) (m₀ : UInt64) (e : Int) (mode : rounding_mode)
    (h_lo : 10 ^ 15 ≤ m₀.toNat) (h_hi : m₀.toNat < 10 ^ 16)
    (he_lo : minExponent + 3 ≤ e) (he_hi : e - 3 < maxExponent) :
    doNormalize neg m₀ e largeRange.min largeRange.max mode
      = .ok ⟨neg, m₀ * 10 * 10 * 10, e - 3⟩ := by
  have hminN : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxN : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hmaxRepN : maxRep.toNat = 9223372036854775807 := maxRep_val
  have hmaxRepUpN : maxRepUp.toNat = maxRepUpNat := rfl
  set M : UInt64 := m₀ * 10 * 10 * 10 with hM_def
  have hm1 : (m₀ * 10).toNat = m₀.toNat * 10 :=
    m_mul_ten_no_overflow (by rw [hminN]; omega)
  have hm2 : (m₀ * 10 * 10).toNat = m₀.toNat * 100 := by
    rw [m_mul_ten_no_overflow (by rw [hminN, hm1]; omega), hm1]
    ring
  have hM_toNat : M.toNat = m₀.toNat * 1000 := by
    rw [hM_def, m_mul_ten_no_overflow (by rw [hminN, hm2]; omega), hm2]
    ring
  have hM_lo : 10 ^ 18 ≤ M.toNat := by rw [hM_toNat]; omega
  have hM_hi : M.toNat < 10 ^ 19 := by rw [hM_toNat]; omega
  have hM_mod : M.toNat % 1000 = 0 := by rw [hM_toNat]; omega
  have hm₀_ne : ¬ (m₀ == 0) = true := by
    intro h
    have : m₀ = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at h_lo
    simp at h_lo
  set g0 : Guard := if neg then Guard.new.set_negative else Guard.new with hg0_def
  have hg0_empty : g0.empty = true := by
    rw [hg0_def]; cases neg <;> decide
  unfold doNormalize
  rw [show (m₀ == 0) = false from Bool.eq_false_iff.mpr (fun h => hm₀_ne h)]
  simp only [Bool.false_eq_true, if_false]
  rw [doNormalize_scaleUp_three m₀ e h_lo h_hi he_lo]
  simp only []
  rw [← hg0_def, ← hM_def,
      doNormalize_scaleDown_id _ _ _ _ (by rw [UInt64.le_iff_toNat_le, hmaxN]; omega)]
  simp only []
  have h_zero_check : (decide (e - 3 < minExponent) || decide (M < largeRange.min)) = false := by
    rw [Bool.or_eq_false_iff]
    constructor
    · exact decide_eq_false (by omega)
    · exact decide_eq_false (by rw [UInt64.lt_iff_toNat_lt, hminN]; omega)
  rw [h_zero_check]
  simp only [Bool.false_eq_true, if_false]
  by_cases h_cap_fire : M > maxRepUp
  · -- Divide arm: capAtMaxRep pushes the zero low digit; doRoundUp rescales back.
    have h_cap : doNormalize_capAtMaxRep M (e - 3) g0
        = .ok (M / 10, e - 3 + 1, g0.push (M % 10)) := by
      unfold doNormalize_capAtMaxRep
      rw [if_pos h_cap_fire, if_neg (by omega : ¬ (e - 3 ≥ maxExponent))]
      rfl
    rw [h_cap]
    simp only []
    have hMmod10 : (M % 10) = 0 := by
      have h10' : (10 : UInt64).toNat = 10 := uint64_ten_toNat
      have h0' : (0 : UInt64).toNat = 0 := rfl
      rw [← UInt64.toNat_inj, UInt64.toNat_mod, h10', h0']
      omega
    have h_g_empty : (g0.push (M % 10)).empty = true :=
      Guard.push_zero_empty hg0_empty hMmod10
    rw [empty_guard_doRoundUp_divu10 _ mode neg M (e - 3) _ h_g_empty
        (by show maxRep < M
            rw [UInt64.lt_iff_toNat_lt, hmaxRepN]
            have := UInt64.lt_iff_toNat_lt.mp h_cap_fire
            rw [hmaxRepUpN] at this
            omega)
        (by rw [hmaxN]; omega)
        (by omega)
        (by omega) (by omega)]
    rfl
  · -- Keep arm: capAtMaxRep is the identity; the empty guard truncates in place.
    have h_cap : doNormalize_capAtMaxRep M (e - 3) g0 = .ok (M, e - 3, g0) := by
      unfold doNormalize_capAtMaxRep
      rw [if_neg h_cap_fire]
    rw [h_cap]
    simp only []
    have hM_le_up : M.toNat ≤ maxRepUp.toNat := by
      by_contra hc
      push_neg at hc
      exact h_cap_fire (UInt64.lt_iff_toNat_lt.mpr hc)
    rw [empty_guard_doRoundUp_id g0 mode neg M (e - 3) _ hg0_empty
        (by rw [UInt64.le_iff_toNat_le, hminN]; omega)
        (by
          intro ⟨h1, h2⟩
          have ha := UInt64.lt_iff_toNat_lt.mp h1
          have hb := UInt64.lt_iff_toNat_lt.mp h2
          rw [hmaxRepN] at ha
          rw [hmaxRepUpN] at hb
          omega)
        (by omega) (by omega)]
    rfl

/-! ## Decision booleans for 16-digit `doRoundUp` -/

/-- A guard representing a positive fraction is nonempty. -/
lemma Guard.not_empty_of_represents_pos {g : Guard} {f : ℚ}
    (hrep : represents g f) (hf : 0 < f) : g.empty = false := by
  rcases h : g.empty with _ | _
  · rfl
  · exfalso
    unfold Guard.empty at h
    rw [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨hdig, hxb⟩ := h
    have hxb' : g.xbit_ = false := by
      cases hx : g.xbit_
      · rfl
      · rw [hx] at hxb; exact absurd hxb (by decide)
    have : f = 0 := represents_eq_zero_of_digits_zero_xbit_false hdig hxb' hrep
    rw [this] at hf
    exact lt_irrefl 0 hf

/-- Nonempty guards have visible content: `digits_ > 0 || xbit_`. -/
lemma Guard.content_of_not_empty {g : Guard} (h : g.empty = false) :
    (g.digits_ > 0 || g.xbit_) = true := by
  unfold Guard.empty at h
  rw [Bool.and_eq_false_iff] at h
  rcases h with h | h
  · have hne : g.digits_ ≠ 0 := by
      intro h0
      rw [h0] at h
      exact absurd h (by decide)
    have h_pos_nat : 0 < g.digits_.toNat := by
      rcases Nat.eq_zero_or_pos g.digits_.toNat with h0 | h0
      · exact absurd (by rw [← UInt64.toNat_inj, h0]; rfl) hne
      · exact h0
    have h_lt : (0 : UInt64) < g.digits_ := by
      rw [UInt64.lt_iff_toNat_lt]
      have h0' : (0 : UInt64).toNat = 0 := rfl
      omega
    rw [Bool.or_eq_true]
    left
    exact decide_eq_true h_lt
  · rw [Bool.or_eq_true]
    right
    cases hx : g.xbit_
    · rw [hx] at h; exact absurd h (by decide)
    · rfl

/-- Round-decision bool, `.towards_zero`: never fires. -/
lemma round_bool_towards_zero (g : Guard) (m : UInt64) :
    (g.round .towards_zero == 1 || (g.round .towards_zero == 0 && m % 2 == 1)) = false := by
  unfold Guard.round
  by_cases h : g.empty = true
  · rw [if_pos h]; rfl
  · rw [if_neg h]; rfl

/-- Round-decision bool, `.downward`, positive sign: never fires. -/
lemma round_bool_downward_pos (g : Guard) (m : UInt64) (h_sbit : g.sbit_ = false) :
    (g.round .downward == 1 || (g.round .downward == 0 && m % 2 == 1)) = false := by
  unfold Guard.round
  by_cases h : g.empty = true
  · rw [if_pos h]; rfl
  · rw [if_neg h]
    simp only [h_sbit, Bool.false_eq_true, if_false]
    rfl

/-- Round-decision bool, `.downward`, negative sign, nonempty: fires. -/
lemma round_bool_downward_neg (g : Guard) (m : UInt64)
    (h_sbit : g.sbit_ = true) (h_ne : g.empty = false) :
    (g.round .downward == 1 || (g.round .downward == 0 && m % 2 == 1)) = true := by
  unfold Guard.round
  rw [if_neg (by rw [h_ne]; exact Bool.false_ne_true), h_sbit]
  simp only [if_true]
  rw [if_pos (Guard.content_of_not_empty h_ne)]
  rfl

/-- Round-decision bool, `.upward`, negative sign: never fires. -/
lemma round_bool_upward_neg (g : Guard) (m : UInt64) (h_sbit : g.sbit_ = true) :
    (g.round .upward == 1 || (g.round .upward == 0 && m % 2 == 1)) = false := by
  unfold Guard.round
  by_cases h : g.empty = true
  · rw [if_pos h]; rfl
  · rw [if_neg h, h_sbit]
    rfl

/-- Round-decision bool, `.upward`, positive sign, nonempty: fires. -/
lemma round_bool_upward_pos (g : Guard) (m : UInt64)
    (h_sbit : g.sbit_ = false) (h_ne : g.empty = false) :
    (g.round .upward == 1 || (g.round .upward == 0 && m % 2 == 1)) = true := by
  unfold Guard.round
  rw [if_neg (by rw [h_ne]; exact Bool.false_ne_true), h_sbit]
  simp only [Bool.false_eq_true, if_false]
  rw [if_pos (Guard.content_of_not_empty h_ne)]
  rfl

/-- Round-decision bool, any mode, empty guard: never fires. -/
lemma round_bool_empty (g : Guard) (m : UInt64) (mode : rounding_mode)
    (h : g.empty = true) :
    (g.round mode == 1 || (g.round mode == 0 && m % 2 == 1)) = false := by
  rw [empty_guard_round_neg_two g mode h]
  rfl

/-! ## `normalizeToRange` at the 16-digit range: per-mode characterization -/

/-- Exact case: dropped digits all zero. Identity up to the `/1000` shift,
every mode. -/
theorem normalizeToRange_16_exact (n : Number) (mode : rounding_mode)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (h_mod : n.mantissa_.toNat % 1000 = 0)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 3 ≤ maxExponent) :
    n.normalizeToRange cMinValue cMaxValue mode
      = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
             else (n.mantissa_ / 10 / 10 / 10).toInt64,
             n.exponent_ + 3) := by
  obtain ⟨g, _hrep, _hsbit, h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ mode h_lo h_hi he_lo he_hi
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = n.mantissa_.toNat / 1000 :=
    m_div_thousand_toNat n.mantissa_
  have h_empty : g.empty = true := h_empty_of h_mod
  unfold Number.normalizeToRange
  rw [h_red, doRoundUp_small_truncate g n.negative_ _ _ mode _
      (round_bool_empty g _ mode h_empty)
      (by rw [cMinValue_val, hm3]; omega)
      (by rw [cMaxValue_val, hm3]; omega)
      (by omega) (by omega)]
  rfl

/-- General per-mode characterization for mantissas up to `2·10^18` (the
sum-decade the `roundToScale` proof produces). The output mantissa is the
3-digit truncation, optionally bumped per the mode/sign decision. -/
theorem normalizeToRange_16_per_mode (n : Number) (mode : rounding_mode)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat ≤ 2 * 10 ^ 18)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 3 ≤ maxExponent) :
    ∃ m₁₆ : UInt64,
      n.normalizeToRange cMinValue cMaxValue mode
        = .ok (if n.negative_ then -m₁₆.toInt64 else m₁₆.toInt64, n.exponent_ + 3) ∧
      m₁₆.toNat ≤ 2 * 10 ^ 15 + 1 ∧
      (mode = .to_nearest →
          m₁₆.toNat = n.mantissa_.toNat / 1000 ∨ m₁₆.toNat = n.mantissa_.toNat / 1000 + 1) ∧
      (mode = .towards_zero → m₁₆.toNat = n.mantissa_.toNat / 1000) ∧
      (mode = .downward → m₁₆.toNat = n.mantissa_.toNat / 1000
          + (if n.negative_ = true ∧ n.mantissa_.toNat % 1000 ≠ 0 then 1 else 0)) ∧
      (mode = .upward → m₁₆.toNat = n.mantissa_.toNat / 1000
          + (if n.negative_ = false ∧ n.mantissa_.toNat % 1000 ≠ 0 then 1 else 0)) ∧
      (n.mantissa_.toNat % 1000 = 0 → m₁₆.toNat = n.mantissa_.toNat / 1000) := by
  obtain ⟨g, hrep, hsbit, h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ mode h_lo (by omega)
      he_lo he_hi
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  set M : ℕ := n.mantissa_.toNat with hM_def
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = M / 1000 := m_div_thousand_toNat n.mantissa_
  have h_div_lo : 10 ^ 15 ≤ M / 1000 := by omega
  have h_div_hi : M / 1000 ≤ 2 * 10 ^ 15 := by omega
  have h_add : ((n.mantissa_ / 10 / 10 / 10) + 1).toNat = M / 1000 + 1 := by
    have h1' : (1 : UInt64).toNat = 1 := rfl
    rw [UInt64.toNat_add, hm3, h1']
    exact Nat.mod_eq_of_lt (by omega)
  -- Common packagers for the two doRoundUp outcomes.
  have h_truncate : ∀ (_hb : (g.round mode == 1 || (g.round mode == 0
        && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = false),
      n.normalizeToRange cMinValue cMaxValue mode
        = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
               else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
    intro hb
    unfold Number.normalizeToRange
    rw [h_red, doRoundUp_small_truncate g n.negative_ _ _ mode _ hb
        (by rw [cMinValue_val, hm3]; omega)
        (by rw [cMaxValue_val, hm3]; omega)
        (by omega) (by omega)]
    rfl
  have h_fire : ∀ (_hb : (g.round mode == 1 || (g.round mode == 0
        && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true),
      n.normalizeToRange cMinValue cMaxValue mode
        = .ok (if n.negative_ then -((n.mantissa_ / 10 / 10 / 10) + 1).toInt64
               else ((n.mantissa_ / 10 / 10 / 10) + 1).toInt64, n.exponent_ + 3) := by
    intro hb
    unfold Number.normalizeToRange
    rw [h_red, doRoundUp_small_fire g n.negative_ _ _ mode _ hb
        (by rw [cMinValue_val, hm3]; omega)
        (by rw [cMaxValue_val, hm3]; omega)
        (by omega) (by omega)]
    rfl
  set mLow : UInt64 := n.mantissa_ / 10 / 10 / 10 with hmLow_def
  set mHigh : UInt64 := n.mantissa_ / 10 / 10 / 10 + 1 with hmHigh_def
  have h_low_char : mLow.toNat = n.mantissa_.toNat / 1000 := hm3
  by_cases h_mod : M % 1000 = 0
  · -- Empty guard: truncate in every mode (and the bump conditions are off).
    have h_empty : g.empty = true := h_empty_of h_mod
    have h_no_bump : ∀ b : Bool, (if n.negative_ = b ∧ n.mantissa_.toNat % 1000 ≠ 0
        then 1 else 0) = 0 := by
      intro b
      rw [if_neg (by intro ⟨_, h⟩; exact h h_mod)]
    exact ⟨mLow, h_truncate (round_bool_empty g _ mode h_empty), by omega,
      fun _ => Or.inl hm3, fun _ => hm3,
      fun _ => by rw [hm3, h_no_bump true, Nat.add_zero],
      fun _ => by rw [hm3, h_no_bump false, Nat.add_zero],
      fun _ => hm3⟩
  · -- Nonempty guard.
    have h_f_pos : 0 < ((M % 1000 : ℕ) : ℚ) / 1000 := by
      have : 0 < M % 1000 := Nat.pos_of_ne_zero h_mod
      have h1 : (0 : ℚ) < ((M % 1000 : ℕ) : ℚ) := by exact_mod_cast this
      positivity
    have h_ne : g.empty = false := Guard.not_empty_of_represents_pos hrep h_f_pos
    rcases mode with _ | _ | _ | _
    · -- to_nearest: membership via the decision bool.
      by_cases hb : (g.round .to_nearest == 1 || (g.round .to_nearest == 0
          && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true
      · exact ⟨mHigh, h_fire hb, by omega,
          fun _ => Or.inr h_add, fun h => rounding_mode.noConfusion h,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun h => absurd h h_mod⟩
      · exact ⟨mLow, h_truncate (Bool.eq_false_iff.mpr (fun h => hb h)), by omega,
          fun _ => Or.inl hm3, fun h => rounding_mode.noConfusion h,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun h => absurd h h_mod⟩
    · -- towards_zero: truncate.
      exact ⟨mLow, h_truncate (round_bool_towards_zero g _), by omega,
        fun h => rounding_mode.noConfusion h, fun _ => hm3,
        fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
        fun h => absurd h h_mod⟩
    · -- downward: fire iff negative.
      rcases hneg : n.negative_ with _ | _
      · refine ⟨mLow, ?_, by omega,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun _ => by
            rw [hm3, if_neg (by intro ⟨h, _⟩; exact Bool.noConfusion h), Nat.add_zero],
          fun h => rounding_mode.noConfusion h,
          fun h => absurd h h_mod⟩
        have := h_truncate (round_bool_downward_pos g _ (hneg ▸ hsbit))
        rw [hneg] at this
        exact this
      · refine ⟨mHigh, ?_, by omega,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun _ => by rw [h_add, if_pos ⟨rfl, h_mod⟩],
          fun h => rounding_mode.noConfusion h,
          fun h => absurd h h_mod⟩
        have := h_fire (round_bool_downward_neg g _ (hneg ▸ hsbit) h_ne)
        rw [hneg] at this
        exact this
    · -- upward: fire iff positive.
      rcases hneg : n.negative_ with _ | _
      · refine ⟨mHigh, ?_, by omega,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun h => rounding_mode.noConfusion h,
          fun _ => by rw [h_add, if_pos ⟨rfl, h_mod⟩],
          fun h => absurd h h_mod⟩
        have := h_fire (round_bool_upward_pos g _ (hneg ▸ hsbit) h_ne)
        rw [hneg] at this
        exact this
      · refine ⟨mLow, ?_, by omega,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun h => rounding_mode.noConfusion h,
          fun _ => by
            rw [hm3, if_neg (by intro ⟨h, _⟩; exact Bool.noConfusion h), Nat.add_zero],
          fun h => absurd h h_mod⟩
        have := h_truncate (round_bool_upward_neg g _ (hneg ▸ hsbit))
        rw [hneg] at this
        exact this

end XRPL.Model.Protocol

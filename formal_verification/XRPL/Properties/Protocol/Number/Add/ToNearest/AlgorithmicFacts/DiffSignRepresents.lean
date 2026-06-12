import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common
import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts.DiffSignStructural
import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts.PostAlignment
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DoRoundDown
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Exit-condition implies the recover output mantissa is at least the
normalization floor `mantissaFloor`. The exit condition is
`¬ (m < largeRange.min ∧ m * 10 ≤ maxRep)`, i.e. `m ≥ largeRange.min`
(so `m ≥ 10^18`) or `m * 10 > maxRep` (so `m ≥ mantissaFloorSucc`).
Either disjunct dominates the floor. -/
lemma recover_exit_mantissa_ge_floor {m : UInt64}
    (hexit : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep)) :
    (mantissaFloor : ℕ) ≤ m.toNat := by
  rw [not_and_or] at hexit
  rcases hexit with h1 | h2
  · rw [UInt64.lt_iff_toNat_lt, largeRange_min_toNat] at h1
    omega
  · rw [UInt64.le_iff_toNat_le, maxRep_toNat_val, UInt64.toNat_mul] at h2
    have h10 : (10 : UInt64).toNat = 10 := rfl
    rw [h10] at h2
    -- h2 : ¬ (m.toNat * 10) % 2^64 ≤ maxRepNat
    have hmod_le : (m.toNat * 10) % 2 ^ 64 ≤ m.toNat * 10 := Nat.mod_le _ _
    omega

/-- The recover output mantissa is at least the normalization floor
`mantissaFloor`, given `1 ≤ m.toNat`, well-formed guard, and fuel `≥ 34`.

The recover loop always reaches its exit condition within 34 pops (by
`recover_34_exits`), so the output satisfies `¬ (m_out < largeRange.min ∧
m_out * 10 ≤ maxRep)`, which forces `m_out ≥ floor` (by
`recover_exit_mantissa_ge_floor`). This handles the would-be fuel-exhaustion
edge: with fuel = 40 > 34 the loop never bottoms out at fuel = 0. -/
lemma recover_mantissa_ge_floor
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (hfuel : 34 ≤ fuel) :
    (mantissaFloor : ℕ) ≤ (Number.operator_add.recover m e g fuel).1.toNat := by
  rw [recover_eq_34_of_ge m e g fuel hm_pos hall hfuel]
  exact recover_exit_mantissa_ge_floor (recover_34_exits m e g hm_pos hall)

/-- `hdich`-discharge helper: if `g_aln` is the guard produced by aligning a
normalized mantissa `m < 10^19` and `g_aln.xbit_ = true`, then the aligned
mantissa `m_a ≤ 99`. Consequently, when subtracted from a mantissa `≥ 10^18`,
the residual `zm_pre ≥ 10^18 - 99` is so large that `zm_pre * 10 > maxRep`,
i.e. the recover loop-continue condition fails. This is exactly the
`hdich` precondition of `recover_preserves_represents`. -/
lemma diff_sign_hdich
    (other_m aln_m : UInt64) (e_other : Int) (g0 : Guard) (target : Int)
    (hg0 : represents g0 0)
    (hm : (Number.operator_add.alignDown other_m e_other g0 target).1 = aln_m)
    (hother_lt : other_m.toNat < 10 ^ 19)
    (larger_m : UInt64)
    (hlarger_ge : (10 : ℕ) ^ 18 ≤ larger_m.toNat)
    (haln_le : aln_m.toNat ≤ larger_m.toNat) :
    (Number.operator_add.alignDown other_m e_other g0 target).2.2.xbit_ = true →
      ¬ (larger_m - aln_m < largeRange.min
          ∧ (larger_m - aln_m) * 10 ≤ maxRep) := by
  intro hxbit
  have h99 : aln_m.toNat ≤ 99 :=
    hm ▸ alignDown_mantissa_le_99_of_xbit other_m e_other g0 target hg0 hother_lt hxbit
  have hle : aln_m ≤ larger_m := UInt64.le_iff_toNat_le.mpr haln_le
  have hzm_toNat : (larger_m - aln_m).toNat = larger_m.toNat - aln_m.toNat :=
    UInt64.toNat_sub_of_le _ _ hle
  have hzm_ge : (999999999999999901 : ℕ) ≤ larger_m.toNat - aln_m.toNat := by
    have : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
    omega
  rintro ⟨h1, h2⟩
  -- First conjunct: zm_pre < largeRange.min = 10^18. So zm_pre.toNat < 10^18.
  rw [UInt64.lt_iff_toNat_lt, largeRange_min_toNat, hzm_toNat] at h1
  have h1018 : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
  -- so zm_pre.toNat ∈ [10^18 - 99, 10^18), giving zm_pre.toNat * 10 < 2^64 (no overflow).
  have hnooverflow : (larger_m.toNat - aln_m.toNat) * 10 < 2 ^ 64 := by omega
  -- Second conjunct: zm_pre * 10 ≤ maxRep.
  rw [UInt64.le_iff_toNat_le, maxRep_toNat_val, UInt64.toNat_mul] at h2
  have h10 : (10 : UInt64).toNat = 10 := rfl
  rw [h10, hzm_toNat, Nat.mod_eq_of_lt hnooverflow] at h2
  omega

/-- `Guard.new.pop = (Guard.new, 0)`: popping the all-zero guard yields digit
`0` and leaves the guard unchanged. -/
lemma pop_guard_new : Guard.new.pop = (Guard.new, 0) := by
  unfold Guard.pop Guard.new
  congr 1

/-- Running `recover` on `(0, e, Guard.new)` leaves the mantissa at `0` and the
guard at `Guard.new`, decrementing the exponent by all the fuel. The all-zero
guard pops `0` every step, so the mantissa never grows past `0`. -/
lemma recover_zero_guard_new (e : Int) (fuel : ℕ) :
    Number.operator_add.recover 0 e Guard.new fuel = (0, e - fuel, Guard.new) := by
  induction fuel generalizing e with
  | zero => rw [recover_noop_zero]; simp
  | succ n ih =>
    have h1 : (0 : UInt64) < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt, largeRange_min_toNat]; decide
    have h2 : (0 : UInt64) * 10 ≤ maxRep := by
      rw [UInt64.le_iff_toNat_le]; decide
    rw [recover_step h1 h2, pop_guard_new]
    have hsub : (0 : UInt64) * 10 - 0 = 0 := by decide
    rw [hsub, ih]
    congr 1
    congr 1
    omega

/-- `doRoundDown` of mantissa `0` (with the all-zero `Guard.new`, any mode)
followed by `normalize` yields a Number with mantissa `0`. Used to dismiss the
degenerate `xe = ye, xm = ym` diff-sign sub-case (where `x + y = 0`). -/
lemma doRoundDown_zero_normalize_mantissa_zero (zn : Bool) (e : Int) (mode : rounding_mode)
    {result : Number}
    (hok : (Guard.new.doRoundDown zn 0 e largeRange.min mode).toNumber.normalize
        largeRange.min largeRange.max mode = .ok result) :
    result.mantissa_ = 0 := by
  -- The doRoundDown output has mantissa 0 (no round-down on Guard.new, bringIntoRange keeps 0).
  have h_rd_mant : (Guard.new.doRoundDown zn 0 e largeRange.min mode).toNumber.mantissa_ = 0 := by
    unfold Guard.doRoundDown Guard.bringIntoRange RoundResult.toNumber
    have hround : Guard.new.round mode = -1 := guard_new_round_neg_one_any mode
    rw [hround]
    norm_num
    split_ifs <;> rfl
  -- normalize of a mantissa-0 number returns Number.zero.
  unfold Number.normalize doNormalize at hok
  rw [h_rd_mant] at hok
  simp only [beq_self_eq_true, if_true] at hok
  rw [Except.ok.injEq] at hok
  rw [← hok]; rfl

/-- For diff-sign Numbers with `|x.toRat| < |y.toRat|`, the sum `x.toRat + y.toRat`
takes the sign of the dominant operand `y`. -/
lemma diff_sign_sum_sign_of_dominant {x y : Number}
    (h_diff_xy : x.negative_ ≠ y.negative_)
    (h_dom : |x.toRat| < |y.toRat|) :
    (y.negative_ = true → x.toRat + y.toRat ≤ 0) ∧
    (y.negative_ = false → 0 ≤ x.toRat + y.toRat) := by
  constructor
  · intro hyn
    have hxn : x.negative_ = false := by
      cases hh : x.negative_
      · rfl
      · rw [hh, hyn] at h_diff_xy; exact absurd rfl h_diff_xy
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
    rw [abs_of_nonneg hx_nn, abs_of_nonpos hy_np] at h_dom
    linarith
  · intro hyn
    have hxn : x.negative_ = true := by
      cases hh : x.negative_
      · rw [hh, hyn] at h_diff_xy; exact absurd rfl h_diff_xy
      · rfl
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
    rw [abs_of_nonpos hx_np, abs_of_nonneg hy_nn] at h_dom
    linarith

set_option maxHeartbeats 8000000 in
-- 9-way diff-sign case analysis; large elaboration requires a raised heartbeat limit
/-- Strong `represents`-based replacement for `_combined` on the diff-sign
addition branch. Produces witnesses `(zm, ze', f, zn, g, res_pos)` such that:
* `represents g f` — the hidden fraction `f ∈ [0, 1)` (NOT a loose `K_F` bound)
* `mantissaFloor ≤ zm.toNat` — the post-recover mantissa floor
* `|x.toRat + y.toRat| = (zm.toNat - f) * 10^ze'` — value equation
* `g.doRoundDown zn zm ze' largeRange.min .to_nearest = res_pos`
* `res_pos.toNumber.normalize ... = .ok result`

The degenerate (`zm_pre = 0`) and "+ f" sub-cases of `_combined` are shown
vacuous here: the aligned operand is always the strictly smaller magnitude
(`< 10^18 ≤ other`), so exactly the "− f" sub-case fires in each alignment
branch. The keystone `recover_preserves_represents` then threads `represents`
through the recover loop. -/
theorem operator_add_algorithmic_facts_diff_sign_represents
    (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (zn : Bool) (g : Guard) (res_pos : RoundResult),
      represents g f ∧
      (mantissaFloor : ℕ) < zm.toNat ∧
      zm.toNat < 10 ^ 19 ∧
      |x.toRat + y.toRat| = ((zm.toNat : ℚ) - f) * 10 ^ ze' ∧
      g.doRoundDown zn zm ze' largeRange.min mode = res_pos ∧
      res_pos.toNumber.normalize largeRange.min largeRange.max mode = .ok result ∧
      ((zn = true → x.toRat + y.toRat ≤ 0) ∧ (zn = false → 0 ≤ x.toRat + y.toRat)) ∧
      (zn = false → g.sbit_ = true ∨ (g.digits_ = 0 ∧ g.xbit_ = false)) := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.2
    rw [largeRange_max_val] at this; omega
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.2
    rw [largeRange_max_val] at this; omega
  have hx_ne_zero : ¬ x.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have hh : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    have : (10 : ℕ) ^ 18 ≤ 0 := hh ▸ hx_min
    norm_num at this
  have hy_ne_zero : ¬ y.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have hh : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    have : (10 : ℕ) ^ 18 ≤ 0 := hh ▸ hy_min
    norm_num at this
  have h_xneqyn : (x.negative_ == y.negative_) = false := by
    rw [beq_eq_false_iff_ne]; exact h_diff_sign
  have h_abs_add := abs_add_eq_of_diff_sign h_diff_sign
  unfold Number.operator_add at hok
  simp only [hy_ne_zero, hx_ne_zero, h_not_zero, Bool.false_eq_true, if_false] at hok
  rw [h_xneqyn] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  by_cases h_xe_lt_ye : x.exponent_ < y.exponent_
  · -- Case 1: xe < ye, align x to ye. Aligned operand = x (smaller magnitude).
    rw [if_pos h_xe_lt_ye] at hok
    simp only at hok
    set g₀ : Guard := if x.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
    have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq x.negative_
    set aln_result : UInt64 × Int × Guard :=
      Number.operator_add.alignDown x.mantissa_ x.exponent_ g₀ y.exponent_ with haln_def
    set xm_a : UInt64 := aln_result.1 with hxm_a_def
    set g_aln : Guard := aln_result.2.2 with hg_aln_def
    have he_common_eq : aln_result.2.1 = y.exponent_ := by
      rw [haln_def]
      have := alignDown_e_eq x.mantissa_ x.exponent_ g₀ y.exponent_
      rw [this]
      exact max_eq_right (le_of_lt h_xe_lt_ye)
    rw [he_common_eq] at hok
    set K : ℕ := (max x.exponent_ y.exponent_ - x.exponent_).toNat with hK_def
    have hmax_eq : max x.exponent_ y.exponent_ = y.exponent_ :=
      max_eq_right (le_of_lt h_xe_lt_ye)
    -- Aligned mantissa is strictly smaller than the other operand: xm_a < 10^18 ≤ y.mantissa_.
    have hK_pos : 1 ≤ K := by
      rw [hK_def, hmax_eq]
      have : x.exponent_ < y.exponent_ := h_xe_lt_ye
      omega
    have hxm_a_eq : xm_a.toNat = x.mantissa_.toNat / 10 ^ K := by
      rw [hxm_a_def, haln_def, alignDown_mantissa_eq, ← hK_def]
    have hxm_a_small : xm_a.toNat < 10 ^ 18 := by
      rw [hxm_a_eq]
      have hpow_ge : (10 : ℕ) ^ 1 ≤ 10 ^ K := Nat.pow_le_pow_right (by norm_num) hK_pos
      have hstep : x.mantissa_.toNat / 10 ^ K ≤ x.mantissa_.toNat / 10 ^ 1 :=
        Nat.div_le_div_left hpow_ge (by positivity)
      have : x.mantissa_.toNat / 10 ^ 1 < 10 ^ 18 := by
        have h1019 : (10 : ℕ) ^ 19 = 10 ^ 18 * 10 := by norm_num
        rw [h1019] at hx_mant_bound; omega
      omega
    have hxm_a_lt_ym : xm_a.toNat < y.mantissa_.toNat := by omega
    set f_aln : ℚ := (0 + ((x.mantissa_.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K with hf_aln_def
    have hf_aln_rep : represents g_aln f_aln := by
      have h := alignDown_represents x.mantissa_ x.exponent_ g₀ y.exponent_ 0 hg₀_rep
      have hK_eq : (max x.exponent_ y.exponent_ - x.exponent_).toNat = K := by rw [hK_def]
      rw [hK_eq] at h
      exact h
    have hf_aln_nn : 0 ≤ f_aln := represents_nonneg hf_aln_rep
    have hf_aln_lt : f_aln < 1 := represents_lt_one hf_aln_rep
    have h_x_abs_pre : |x.toRat| = ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_ := by
      have h_lemma := alignDown_abs_value x g₀ y.exponent_ (le_of_lt h_xe_lt_ye) 0
      simp only at h_lemma
      have hK_eq : (y.exponent_ - x.exponent_).toNat = K := by
        rw [hK_def, hmax_eq]
      rw [hK_eq] at h_lemma
      have : |x.toRat| + 0 * 10 ^ x.exponent_ = ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_ := by
        rw [hf_aln_def]; rw [hxm_a_def, haln_def] at *
        convert h_lemma using 2
      linarith
    have h_y_abs : |y.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := abs_toRat_eq y
    -- Only the "− f" sub-case is reachable. The other branches need ¬(xm_a > y.mantissa_).
    -- zm_pre = y.mantissa_ - xm_a.
    set zm_pre : UInt64 := y.mantissa_ - xm_a with hzm_pre_def
    have h_ym_ge_xm : xm_a ≤ y.mantissa_ :=
      UInt64.le_iff_toNat_le.mpr (Nat.le_of_lt hxm_a_lt_ym)
    have hzm_pre_toNat : zm_pre.toNat = y.mantissa_.toNat - xm_a.toNat := by
      rw [hzm_pre_def, UInt64.toNat_sub_of_le _ _ h_ym_ge_xm]
    have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
    have h_truth_eq_minus : |x.toRat + y.toRat| =
        ((zm_pre.toNat : ℚ) - f_aln) * 10 ^ y.exponent_ := by
      rw [h_abs_add, h_x_abs_pre, h_y_abs]
      have h_diff_neg : ((xm_a.toNat : ℚ) + f_aln) - (y.mantissa_.toNat : ℚ) ≤ 0 := by
        have hge : (1 : ℚ) ≤ (y.mantissa_.toNat : ℚ) - (xm_a.toNat : ℚ) := by
          have h1 : ((1 : ℕ) : ℚ) ≤ ((y.mantissa_.toNat - xm_a.toNat : ℕ) : ℚ) := by
            exact_mod_cast (by omega : 1 ≤ y.mantissa_.toNat - xm_a.toNat)
          rw [Nat.cast_sub (Nat.le_of_lt hxm_a_lt_ym)] at h1
          push_cast at h1; linarith
        linarith
      rw [show ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_
               - (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_
             = (((xm_a.toNat : ℚ) + f_aln) - (y.mantissa_.toNat : ℚ)) * 10 ^ y.exponent_
           from by ring]
      have h_pow_pos : (0 : ℚ) < 10 ^ y.exponent_ := by positivity
      have h_prod_np : (((xm_a.toNat : ℚ) + f_aln) - (y.mantissa_.toNat : ℚ)) * 10 ^ y.exponent_ ≤ 0 :=
        mul_nonpos_of_nonpos_of_nonneg h_diff_neg (le_of_lt h_pow_pos)
      rw [abs_of_nonpos h_prod_np]
      rw [hzm_pre_toNat]
      have hsub_q : ((y.mantissa_.toNat - xm_a.toNat : ℕ) : ℚ)
          = (y.mantissa_.toNat : ℚ) - (xm_a.toNat : ℚ) := by
        rw [Nat.cast_sub (Nat.le_of_lt hxm_a_lt_ym)]
      rw [hsub_q]; ring
    -- Apply the keystone: represents threaded through recover.
    have hdich : g_aln.xbit_ = true →
        ¬ (zm_pre < largeRange.min ∧ zm_pre * 10 ≤ maxRep) :=
      diff_sign_hdich x.mantissa_ xm_a x.exponent_ g₀ y.exponent_ hg₀_rep
        rfl hx_mant_bound y.mantissa_ hy_min (le_of_lt hxm_a_lt_ym)
    obtain ⟨f_exit, hrep_exit, hval_eq⟩ :=
      recover_preserves_represents zm_pre y.exponent_ g_aln 40 hzm_pre_pos hf_aln_rep hdich
    have hall_aln : allNibblesAtMost9 g_aln.digits_ := by
      obtain ⟨_, _, _, _, _, h⟩ := hf_aln_rep; exact h
    have hfloor : (mantissaFloor : ℕ)
        < (Number.operator_add.recover zm_pre y.exponent_ g_aln 40).1.toNat :=
      recover_mantissa_gt_floor zm_pre y.exponent_ g_aln 40 hzm_pre_pos hall_aln (by norm_num)
    have hzm_pre_lt : zm_pre.toNat < 10 ^ 19 := by rw [hzm_pre_toNat]; omega
    have hupper : (Number.operator_add.recover zm_pre y.exponent_ g_aln 40).1.toNat < 10 ^ 19 :=
      recover_mantissa_lt zm_pre y.exponent_ g_aln 40 hzm_pre_pos hall_aln hzm_pre_lt
    have h_not_xm_a_gt : ¬ xm_a > y.mantissa_ := by
      intro h
      have hh : y.mantissa_.toNat < xm_a.toNat := UInt64.lt_iff_toNat_lt.mp h
      omega
    have h_dom : |x.toRat| < |y.toRat| := by
      rw [h_x_abs_pre, h_y_abs]
      have h_pow_pos : (0 : ℚ) < 10 ^ y.exponent_ := by positivity
      have hle : ((xm_a.toNat : ℚ) + 1) ≤ (y.mantissa_.toNat : ℚ) := by
        have : (xm_a.toNat + 1 : ℕ) ≤ y.mantissa_.toNat := by omega
        exact_mod_cast this
      have hf1 : f_aln < 1 := hf_aln_lt
      have h_lt : (xm_a.toNat : ℚ) + f_aln < (y.mantissa_.toNat : ℚ) :=
        lt_of_lt_of_le (by linarith only [hf1]) hle
      exact mul_lt_mul_of_pos_right h_lt h_pow_pos
    have h_sign := diff_sign_sum_sign_of_dominant h_diff_sign h_dom
    have h_sbit_fact : y.negative_ = false →
        (Number.operator_add.recover (y.mantissa_ - xm_a) y.exponent_ g_aln 40).2.2.sbit_ = true
          ∨ ((Number.operator_add.recover (y.mantissa_ - xm_a) y.exponent_ g_aln 40).2.2.digits_ = 0
             ∧ (Number.operator_add.recover (y.mantissa_ - xm_a) y.exponent_ g_aln 40).2.2.xbit_ = false) := by
      intro hyn
      left
      rw [recover_sbit]
      have hg_aln_sbit : g_aln.sbit_ = g₀.sbit_ := by
        rw [hg_aln_def, haln_def, alignDown_sbit]
      rw [hg_aln_sbit, hg₀_def]
      have hxn : x.negative_ = true := by
        have hne : x.negative_ ≠ false := by rw [hyn] at h_diff_sign; exact h_diff_sign
        exact Bool.not_eq_false _ |>.mp hne
      rw [if_pos hxn]; rfl
    simp only [if_neg h_not_xm_a_gt] at hok
    generalize hr : Number.operator_add.recover (y.mantissa_ - xm_a) y.exponent_ g_aln 40 = rec_state
      at hok hval_eq hrep_exit hfloor hupper h_sbit_fact
    refine ⟨rec_state.1, rec_state.2.1, f_exit, y.negative_, rec_state.2.2,
            rec_state.2.2.doRoundDown y.negative_ rec_state.1 rec_state.2.1
              largeRange.min mode,
            hrep_exit, hfloor, hupper, ?_, rfl, ?_, h_sign, h_sbit_fact⟩
    · rw [h_truth_eq_minus]; exact hval_eq
    · exact hok
  · push_neg at h_xe_lt_ye
    by_cases h_xe_gt_ye : x.exponent_ > y.exponent_
    · -- Case 2: xe > ye, align y to xe. Aligned operand = y (smaller magnitude).
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_pos h_xe_gt_ye] at hok
      simp only at hok
      set g₀ : Guard := if y.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
      have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq y.negative_
      set aln_result : UInt64 × Int × Guard :=
        Number.operator_add.alignDown y.mantissa_ y.exponent_ g₀ x.exponent_ with haln_def
      set ym_a : UInt64 := aln_result.1 with hym_a_def
      set g_aln : Guard := aln_result.2.2 with hg_aln_def
      set K : ℕ := (max y.exponent_ x.exponent_ - y.exponent_).toNat with hK_def
      have hmax_eq : max y.exponent_ x.exponent_ = x.exponent_ :=
        max_eq_right (le_of_lt h_xe_gt_ye)
      have hK_pos : 1 ≤ K := by
        rw [hK_def, hmax_eq]; omega
      have hym_a_eq : ym_a.toNat = y.mantissa_.toNat / 10 ^ K := by
        rw [hym_a_def, haln_def, alignDown_mantissa_eq, ← hK_def]
      have hym_a_small : ym_a.toNat < 10 ^ 18 := by
        rw [hym_a_eq]
        have hpow_ge : (10 : ℕ) ^ 1 ≤ 10 ^ K := Nat.pow_le_pow_right (by norm_num) hK_pos
        have hstep : y.mantissa_.toNat / 10 ^ K ≤ y.mantissa_.toNat / 10 ^ 1 :=
          Nat.div_le_div_left hpow_ge (by positivity)
        have : y.mantissa_.toNat / 10 ^ 1 < 10 ^ 18 := by
          have h1019 : (10 : ℕ) ^ 19 = 10 ^ 18 * 10 := by norm_num
          rw [h1019] at hy_mant_bound; omega
        omega
      have hym_a_lt_xm : ym_a.toNat < x.mantissa_.toNat := by omega
      set f_aln : ℚ := (0 + ((y.mantissa_.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K with hf_aln_def
      have hf_aln_rep : represents g_aln f_aln := by
        have h := alignDown_represents y.mantissa_ y.exponent_ g₀ x.exponent_ 0 hg₀_rep
        have hK_eq : (max y.exponent_ x.exponent_ - y.exponent_).toNat = K := by rw [hK_def]
        rw [hK_eq] at h
        exact h
      have hf_aln_nn : 0 ≤ f_aln := represents_nonneg hf_aln_rep
      have hf_aln_lt : f_aln < 1 := represents_lt_one hf_aln_rep
      have h_y_abs_pre : |y.toRat| = ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_ := by
        have h_lemma := alignDown_abs_value y g₀ x.exponent_ (le_of_lt h_xe_gt_ye) 0
        simp only at h_lemma
        have hK_eq : (x.exponent_ - y.exponent_).toNat = K := by
          rw [hK_def, hmax_eq]
        rw [hK_eq] at h_lemma
        have : |y.toRat| + 0 * 10 ^ y.exponent_ = ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_ := by
          rw [hf_aln_def]; rw [hym_a_def, haln_def] at *
          convert h_lemma using 2
        linarith
      have h_x_abs : |x.toRat| = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := abs_toRat_eq x
      -- Only the "− f" sub-case (xm > ym_a) is reachable.
      have h_xm_gt_uint : x.mantissa_ > ym_a := UInt64.lt_iff_toNat_lt.mpr hym_a_lt_xm
      set zm_pre : UInt64 := x.mantissa_ - ym_a with hzm_pre_def
      have h_ym_le_xm : ym_a ≤ x.mantissa_ :=
        UInt64.le_iff_toNat_le.mpr (Nat.le_of_lt hym_a_lt_xm)
      have hzm_pre_toNat : zm_pre.toNat = x.mantissa_.toNat - ym_a.toNat := by
        rw [hzm_pre_def, UInt64.toNat_sub_of_le _ _ h_ym_le_xm]
      have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
      have h_truth_eq_minus : |x.toRat + y.toRat| =
          ((zm_pre.toNat : ℚ) - f_aln) * 10 ^ x.exponent_ := by
        rw [h_abs_add, h_x_abs, h_y_abs_pre]
        have h_diff_pos : (x.mantissa_.toNat : ℚ) - ((ym_a.toNat : ℚ) + f_aln) ≥ 0 := by
          have hge : (1 : ℚ) ≤ (x.mantissa_.toNat : ℚ) - (ym_a.toNat : ℚ) := by
            have h1 : ((1 : ℕ) : ℚ) ≤ ((x.mantissa_.toNat - ym_a.toNat : ℕ) : ℚ) := by
              exact_mod_cast (by omega : 1 ≤ x.mantissa_.toNat - ym_a.toNat)
            rw [Nat.cast_sub (Nat.le_of_lt hym_a_lt_xm)] at h1
            push_cast at h1; linarith
          linarith
        rw [show (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                 - ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_
               = ((x.mantissa_.toNat : ℚ) - ((ym_a.toNat : ℚ) + f_aln)) * 10 ^ x.exponent_
             from by ring]
        have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
        have h_prod_nn : (0 : ℚ)
            ≤ ((x.mantissa_.toNat : ℚ) - ((ym_a.toNat : ℚ) + f_aln)) * 10 ^ x.exponent_ :=
          mul_nonneg h_diff_pos (le_of_lt h_pow_pos)
        rw [abs_of_nonneg h_prod_nn]
        rw [hzm_pre_toNat]
        have hsub_q : ((x.mantissa_.toNat - ym_a.toNat : ℕ) : ℚ)
            = (x.mantissa_.toNat : ℚ) - (ym_a.toNat : ℚ) := by
          rw [Nat.cast_sub (Nat.le_of_lt hym_a_lt_xm)]
        rw [hsub_q]; ring
      have hdich : g_aln.xbit_ = true →
          ¬ (zm_pre < largeRange.min ∧ zm_pre * 10 ≤ maxRep) :=
        diff_sign_hdich y.mantissa_ ym_a y.exponent_ g₀ x.exponent_ hg₀_rep
          rfl hy_mant_bound x.mantissa_ hx_min (le_of_lt hym_a_lt_xm)
      obtain ⟨f_exit, hrep_exit, hval_eq⟩ :=
        recover_preserves_represents zm_pre x.exponent_ g_aln 40 hzm_pre_pos hf_aln_rep hdich
      have hall_aln : allNibblesAtMost9 g_aln.digits_ := by
        obtain ⟨_, _, _, _, _, h⟩ := hf_aln_rep; exact h
      have hfloor : (mantissaFloor : ℕ)
          < (Number.operator_add.recover zm_pre x.exponent_ g_aln 40).1.toNat :=
        recover_mantissa_gt_floor zm_pre x.exponent_ g_aln 40 hzm_pre_pos hall_aln (by norm_num)
      have hzm_pre_lt : zm_pre.toNat < 10 ^ 19 := by rw [hzm_pre_toNat]; omega
      have hupper : (Number.operator_add.recover zm_pre x.exponent_ g_aln 40).1.toNat < 10 ^ 19 :=
        recover_mantissa_lt zm_pre x.exponent_ g_aln 40 hzm_pre_pos hall_aln hzm_pre_lt
      have h_dom : |y.toRat| < |x.toRat| := by
        rw [h_y_abs_pre, h_x_abs]
        have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
        have h_lt : (ym_a.toNat : ℚ) + f_aln < (x.mantissa_.toNat : ℚ) := by
          have : ((ym_a.toNat : ℚ) + 1) ≤ (x.mantissa_.toNat : ℚ) := by
            have : (ym_a.toNat + 1 : ℕ) ≤ x.mantissa_.toNat := by omega
            exact_mod_cast this
          linarith
        exact mul_lt_mul_of_pos_right h_lt h_pow_pos
      have h_sign' := diff_sign_sum_sign_of_dominant (Ne.symm h_diff_sign) h_dom
      have h_sign : (x.negative_ = true → x.toRat + y.toRat ≤ 0) ∧
          (x.negative_ = false → 0 ≤ x.toRat + y.toRat) := by
        rw [add_comm x.toRat y.toRat]; exact h_sign'
      have h_sbit_fact : x.negative_ = false →
          (Number.operator_add.recover (x.mantissa_ - ym_a) x.exponent_ g_aln 40).2.2.sbit_ = true
            ∨ ((Number.operator_add.recover (x.mantissa_ - ym_a) x.exponent_ g_aln 40).2.2.digits_ = 0
               ∧ (Number.operator_add.recover (x.mantissa_ - ym_a) x.exponent_ g_aln 40).2.2.xbit_ = false) := by
        intro hxn
        left
        rw [recover_sbit]
        have hg_aln_sbit : g_aln.sbit_ = g₀.sbit_ := by
          rw [hg_aln_def, haln_def, alignDown_sbit]
        rw [hg_aln_sbit, hg₀_def]
        have hyn : y.negative_ = true := by
          have hne : y.negative_ ≠ false := by
            rw [hxn] at h_diff_sign; exact fun hc => h_diff_sign hc.symm
          exact Bool.not_eq_false _ |>.mp hne
        rw [if_pos hyn]; rfl
      simp only [if_pos h_xm_gt_uint] at hok
      generalize hr : Number.operator_add.recover (x.mantissa_ - ym_a) x.exponent_ g_aln 40 = rec_state
        at hok hval_eq hrep_exit hfloor hupper h_sbit_fact
      refine ⟨rec_state.1, rec_state.2.1, f_exit, x.negative_, rec_state.2.2,
              rec_state.2.2.doRoundDown x.negative_ rec_state.1 rec_state.2.1
                largeRange.min mode,
              hrep_exit, hfloor, hupper, ?_, rfl, ?_, h_sign, h_sbit_fact⟩
      · rw [h_truth_eq_minus]; exact hval_eq
      · exact hok
    · -- Case 3: xe = ye, no alignment. g = Guard.new, f_aln = 0.
      push_neg at h_xe_gt_ye
      have h_xe_eq_ye : x.exponent_ = y.exponent_ := le_antisymm h_xe_gt_ye h_xe_lt_ye
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_neg (not_lt.mpr h_xe_gt_ye)] at hok
      simp only at hok
      have h_x_abs : |x.toRat| = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := abs_toRat_eq x
      have h_y_abs : |y.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := abs_toRat_eq y
      have hg_new_rep : represents Guard.new (0 : ℚ) := represents_new
      have hg_new_nibbles : allNibblesAtMost9 Guard.new.digits_ := by
        obtain ⟨_, _, _, _, _, h⟩ := hg_new_rep; exact h
      have hg_new_xbit : Guard.new.xbit_ = false := rfl
      have hdich_new : Guard.new.xbit_ = true →
          ∀ (m : UInt64), ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
        rw [hg_new_xbit]; intro h; exact absurd h (by decide)
      by_cases h_xm_gt : x.mantissa_.toNat > y.mantissa_.toNat
      · -- xm > ym
        have h_xm_gt_uint : x.mantissa_ > y.mantissa_ := UInt64.lt_iff_toNat_lt.mpr h_xm_gt
        have h_ym_le_xm : y.mantissa_ ≤ x.mantissa_ :=
          UInt64.le_iff_toNat_le.mpr (Nat.le_of_lt h_xm_gt)
        set zm_pre : UInt64 := x.mantissa_ - y.mantissa_ with hzm_pre_def
        have hzm_pre_toNat : zm_pre.toNat = x.mantissa_.toNat - y.mantissa_.toNat := by
          rw [hzm_pre_def, UInt64.toNat_sub_of_le _ _ h_ym_le_xm]
        have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
        have h_truth_eq : |x.toRat + y.toRat| = ((zm_pre.toNat : ℚ) - 0) * 10 ^ x.exponent_ := by
          rw [h_abs_add, h_x_abs, h_y_abs, ← h_xe_eq_ye]
          have h_diff_nn : (0 : ℚ) ≤ (x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ) := by
            have : ((y.mantissa_.toNat : ℕ) : ℚ) ≤ ((x.mantissa_.toNat : ℕ) : ℚ) := by
              exact_mod_cast (Nat.le_of_lt h_xm_gt)
            linarith
          rw [show (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                   - (y.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                 = ((x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_ from by ring]
          have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
          have h_prod_nn : (0 : ℚ)
              ≤ ((x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_ :=
            mul_nonneg h_diff_nn (le_of_lt h_pow_pos)
          rw [abs_of_nonneg h_prod_nn, hzm_pre_toNat]
          have hsub_q : ((x.mantissa_.toNat - y.mantissa_.toNat : ℕ) : ℚ)
              = (x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ) := by
            rw [Nat.cast_sub (Nat.le_of_lt h_xm_gt)]
          rw [hsub_q]; ring
        have hdich : Guard.new.xbit_ = true →
            ¬ (zm_pre < largeRange.min ∧ zm_pre * 10 ≤ maxRep) := by
          intro h; exact hdich_new h zm_pre
        obtain ⟨f_exit, hrep_exit, hval_eq⟩ :=
          recover_preserves_represents zm_pre x.exponent_ Guard.new 40 hzm_pre_pos hg_new_rep hdich
        have hfloor : (mantissaFloor : ℕ)
            < (Number.operator_add.recover zm_pre x.exponent_ Guard.new 40).1.toNat :=
          recover_mantissa_gt_floor zm_pre x.exponent_ Guard.new 40 hzm_pre_pos hg_new_nibbles (by norm_num)
        have hzm_pre_lt : zm_pre.toNat < 10 ^ 19 := by rw [hzm_pre_toNat]; omega
        have hupper : (Number.operator_add.recover zm_pre x.exponent_ Guard.new 40).1.toNat < 10 ^ 19 :=
          recover_mantissa_lt zm_pre x.exponent_ Guard.new 40 hzm_pre_pos hg_new_nibbles hzm_pre_lt
        have h_dom : |y.toRat| < |x.toRat| := by
          rw [h_x_abs, h_y_abs, ← h_xe_eq_ye]
          have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
          have h_lt : (y.mantissa_.toNat : ℚ) < (x.mantissa_.toNat : ℚ) := by
            exact_mod_cast h_xm_gt
          exact mul_lt_mul_of_pos_right h_lt h_pow_pos
        have h_sign' := diff_sign_sum_sign_of_dominant (Ne.symm h_diff_sign) h_dom
        have h_sign : (x.negative_ = true → x.toRat + y.toRat ≤ 0) ∧
            (x.negative_ = false → 0 ≤ x.toRat + y.toRat) := by
          rw [add_comm x.toRat y.toRat]; exact h_sign'
        have h_sbit_fact : x.negative_ = false →
            (Number.operator_add.recover (x.mantissa_ - y.mantissa_) x.exponent_ Guard.new 40).2.2.sbit_ = true
              ∨ ((Number.operator_add.recover (x.mantissa_ - y.mantissa_) x.exponent_ Guard.new 40).2.2.digits_ = 0
                 ∧ (Number.operator_add.recover (x.mantissa_ - y.mantissa_) x.exponent_ Guard.new 40).2.2.xbit_ = false) := by
          intro _
          right
          exact ⟨recover_digits_zero _ _ _ _ rfl, recover_xbit _ _ _ _⟩
        simp only [if_pos h_xm_gt_uint] at hok
        generalize hr : Number.operator_add.recover (x.mantissa_ - y.mantissa_) x.exponent_ Guard.new 40 = rec_state
          at hok hval_eq hrep_exit hfloor hupper h_sbit_fact
        refine ⟨rec_state.1, rec_state.2.1, f_exit, x.negative_, rec_state.2.2,
                rec_state.2.2.doRoundDown x.negative_ rec_state.1 rec_state.2.1
                  largeRange.min mode,
                hrep_exit, hfloor, hupper, ?_, rfl, ?_, h_sign, h_sbit_fact⟩
        · rw [h_truth_eq]; exact hval_eq
        · exact hok
      · push_neg at h_xm_gt
        by_cases h_xm_eq : x.mantissa_.toNat = y.mantissa_.toNat
        · -- Degenerate: xm = ym, so x + y = 0, result.mantissa_ = 0 — contradicts hresult.
          exfalso
          have h_xm_eq_uint : x.mantissa_ = y.mantissa_ := UInt64.toNat_inj.mp h_xm_eq
          have h_not_xm_gt : ¬ x.mantissa_ > y.mantissa_ := by
            intro h
            have := UInt64.lt_iff_toNat_lt.mp h; omega
          have h_xm_le' : x.mantissa_ ≤ y.mantissa_ :=
            UInt64.le_iff_toNat_le.mpr (le_of_eq h_xm_eq)
          have h_sub_zero : y.mantissa_ - x.mantissa_ = (0 : UInt64) := by
            apply UInt64.toNat_inj.mp
            rw [UInt64.toNat_sub_of_le _ _ h_xm_le']
            have : (0 : UInt64).toNat = 0 := rfl
            rw [this]; omega
          simp only [if_neg h_not_xm_gt] at hok
          rw [h_sub_zero] at hok
          rw [recover_zero_guard_new] at hok
          -- hok : (Guard.new.doRoundDown ... 0 ...).toNumber.normalize ... = .ok result
          have h_result_mant : result.mantissa_ = 0 :=
            doRoundDown_zero_normalize_mantissa_zero y.negative_ (x.exponent_ - 40) mode hok
          exact hresult h_result_mant
        · -- xm < ym
          have h_xm_lt : x.mantissa_.toNat < y.mantissa_.toNat := by omega
          have h_xm_le : x.mantissa_ ≤ y.mantissa_ :=
            UInt64.le_iff_toNat_le.mpr (Nat.le_of_lt h_xm_lt)
          set zm_pre : UInt64 := y.mantissa_ - x.mantissa_ with hzm_pre_def
          have hzm_pre_toNat : zm_pre.toNat = y.mantissa_.toNat - x.mantissa_.toNat := by
            rw [hzm_pre_def, UInt64.toNat_sub_of_le _ _ h_xm_le]
          have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
          have h_truth_eq : |x.toRat + y.toRat| = ((zm_pre.toNat : ℚ) - 0) * 10 ^ x.exponent_ := by
            rw [h_abs_add, h_x_abs, h_y_abs, ← h_xe_eq_ye]
            have h_diff_nn : (0 : ℚ) ≤ (y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ) := by
              have : ((x.mantissa_.toNat : ℕ) : ℚ) ≤ ((y.mantissa_.toNat : ℕ) : ℚ) := by
                exact_mod_cast (Nat.le_of_lt h_xm_lt)
              linarith
            rw [show (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                     - (y.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                   = -(((y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_)
                 from by ring]
            rw [abs_neg]
            have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
            have h_prod_nn : (0 : ℚ)
                ≤ ((y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_ :=
              mul_nonneg h_diff_nn (le_of_lt h_pow_pos)
            rw [abs_of_nonneg h_prod_nn, hzm_pre_toNat]
            have hsub_q : ((y.mantissa_.toNat - x.mantissa_.toNat : ℕ) : ℚ)
                = (y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ) := by
              rw [Nat.cast_sub (Nat.le_of_lt h_xm_lt)]
            rw [hsub_q]; ring
          have hdich : Guard.new.xbit_ = true →
              ¬ (zm_pre < largeRange.min ∧ zm_pre * 10 ≤ maxRep) := by
            intro h; exact hdich_new h zm_pre
          obtain ⟨f_exit, hrep_exit, hval_eq⟩ :=
            recover_preserves_represents zm_pre x.exponent_ Guard.new 40 hzm_pre_pos hg_new_rep hdich
          have hfloor : (mantissaFloor : ℕ)
              < (Number.operator_add.recover zm_pre x.exponent_ Guard.new 40).1.toNat :=
            recover_mantissa_gt_floor zm_pre x.exponent_ Guard.new 40 hzm_pre_pos hg_new_nibbles (by norm_num)
          have hzm_pre_lt : zm_pre.toNat < 10 ^ 19 := by rw [hzm_pre_toNat]; omega
          have hupper : (Number.operator_add.recover zm_pre x.exponent_ Guard.new 40).1.toNat < 10 ^ 19 :=
            recover_mantissa_lt zm_pre x.exponent_ Guard.new 40 hzm_pre_pos hg_new_nibbles hzm_pre_lt
          have h_not_xm_gt : ¬ x.mantissa_ > y.mantissa_ := by
            intro h
            have := UInt64.lt_iff_toNat_lt.mp h; omega
          have h_dom : |x.toRat| < |y.toRat| := by
            rw [h_x_abs, h_y_abs, ← h_xe_eq_ye]
            have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
            have h_lt : (x.mantissa_.toNat : ℚ) < (y.mantissa_.toNat : ℚ) := by
              exact_mod_cast h_xm_lt
            exact mul_lt_mul_of_pos_right h_lt h_pow_pos
          have h_sign := diff_sign_sum_sign_of_dominant h_diff_sign h_dom
          have h_sbit_fact : y.negative_ = false →
              (Number.operator_add.recover (y.mantissa_ - x.mantissa_) x.exponent_ Guard.new 40).2.2.sbit_ = true
                ∨ ((Number.operator_add.recover (y.mantissa_ - x.mantissa_) x.exponent_ Guard.new 40).2.2.digits_ = 0
                   ∧ (Number.operator_add.recover (y.mantissa_ - x.mantissa_) x.exponent_ Guard.new 40).2.2.xbit_ = false) := by
            intro _
            right
            exact ⟨recover_digits_zero _ _ _ _ rfl, recover_xbit _ _ _ _⟩
          simp only [if_neg h_not_xm_gt] at hok
          generalize hr : Number.operator_add.recover (y.mantissa_ - x.mantissa_) x.exponent_ Guard.new 40 = rec_state
            at hok hval_eq hrep_exit hfloor hupper h_sbit_fact
          refine ⟨rec_state.1, rec_state.2.1, f_exit, y.negative_, rec_state.2.2,
                  rec_state.2.2.doRoundDown y.negative_ rec_state.1 rec_state.2.1
                    largeRange.min mode,
                  hrep_exit, hfloor, hupper, ?_, rfl, ?_, h_sign, h_sbit_fact⟩
          · rw [h_truth_eq]; exact hval_eq
          · exact hok

end XRPL.Model.Protocol

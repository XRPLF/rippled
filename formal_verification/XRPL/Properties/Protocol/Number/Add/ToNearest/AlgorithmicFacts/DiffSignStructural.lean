import XRPL.Properties.Protocol.Number.Add.Common
import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts.PostAlignment
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Algorithmic decomposition for `operator_add` — diff-sign branch (to_nearest) -/

/-- For diff-sign Numbers, `|x.toRat + y.toRat| = abs (|x.toRat| - |y.toRat|)`. -/
lemma abs_add_eq_of_diff_sign {x y : Number} (h : x.negative_ ≠ y.negative_) :
    |x.toRat + y.toRat| = |(|x.toRat| - |y.toRat|)| := by
  cases hxn : x.negative_
  · have hyn : y.negative_ = true := by
      cases hh : y.negative_
      · rw [hxn, hh] at h; exact absurd rfl h
      · rfl
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
    rw [abs_of_nonneg hx_nn, abs_of_nonpos hy_np]
    -- x + y = x - (-y), since y ≤ 0 so -y ≥ 0
    have h_eq : x.toRat + y.toRat = x.toRat - (-y.toRat) := by ring
    rw [h_eq]
  · have hyn : y.negative_ = false := by
      cases hh : y.negative_
      · rfl
      · rw [hxn, hh] at h; exact absurd rfl h
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
    rw [abs_of_nonpos hx_np, abs_of_nonneg hy_nn]
    -- x + y = y - (-x); we want | y - x | but get | -x - y |
    have h_eq : x.toRat + y.toRat = -(-x.toRat - y.toRat) := by ring
    rw [h_eq, abs_neg]

set_option maxHeartbeats 3200000 in
-- Generalize tactics + exact-on-`hok` matching against deeply-nested let-unfolded
-- subterms are expensive to elaborate; the bump above is conservative.
/-- Conservative diff-sign branch decomposition.

This minimal version asserts that in the diff-sign branch, after running
the alignment + recover + doRoundDown + normalize pipeline, the result
satisfies a structural existence claim: there exist values `(zm, ze', g, zn)`
such that `result` arises from `g.doRoundDown zn zm ze' largeRange.min .to_nearest`
followed by `normalize`. The value equation linking
`|x.toRat + y.toRat|` to the post-recover state is not asserted in this
version — the diff-sign branch's value preservation in `(zm ± f) * 10^e`
form depends on a 4-way sub-case analysis (which operand was aligned ×
which has larger magnitude post-alignment), and the resulting slack
constraints require the bound-proof machinery to absorb.

Downstream bound proofs should case-split on the alignment branch
(via the same 3-way `xe vs ye` split used here) and apply
`recover_value_preserved_of_represents` / `recover_value_in_unit_interval`
directly on each branch to obtain a sharp value equation. -/
theorem operator_add_algorithmic_facts_diff_sign (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (_hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (zn : Bool) (g : Guard) (res_pos : RoundResult),
      g.doRoundDown zn zm ze' largeRange.min .to_nearest = res_pos ∧
      res_pos.toNumber.normalize largeRange.min largeRange.max .to_nearest = .ok result := by
  -- Mantissa bounds from isNormalized.
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; exact this
  -- Knock out the early returns.
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
  -- xn != yn is false (since diff sign means inequality of bools)
  have h_xneqyn : (x.negative_ == y.negative_) = false := by
    rw [beq_eq_false_iff_ne]; exact h_diff_sign
  -- Unfold operator_add.
  unfold Number.operator_add at hok
  simp only [hy_ne_zero, hx_ne_zero, h_not_zero, Bool.false_eq_true, if_false] at hok
  -- After eliminating early returns, hok has the form: the entire alignment +
  -- recover + doRoundDown + normalize pipeline produces result. The branch
  -- on `xn == yn` decomposes into same-sign (we're not here) or diff-sign
  -- (h_xneqyn). The diff-sign body has:
  -- 1. align: (xm, xe, ym, ye, g_aln) — three cases
  -- 2. subtract: (zm, zn) = if xm > ym then (xm - ym, xn) else (ym - xm, yn)
  -- 3. recover: (zm', ze', g) = recover zm xe g_aln 40
  -- 4. doRoundDown: res = g.doRoundDown zn zm' ze' largeRange.min .to_nearest
  -- 5. normalize: result = res.toNumber.normalize largeRange.min largeRange.max .to_nearest
  -- We just extract the existential by picking the right witnesses from hok.
  rw [h_xneqyn] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  -- The 3-way alignment split. Generalize the recover call to extract witnesses.
  by_cases h_xe_lt_ye : x.exponent_ < y.exponent_
  · -- Case 1: xe < ye, align x.
    rw [if_pos h_xe_lt_ye] at hok
    simp only at hok
    generalize hr : Number.operator_add.recover _ _ _ 40 = rec_state at hok
    -- After the let `(zm, zn) := if .. then (xm-ym, xn) else (ym-xm, yn)`,
    -- the projection `.2` gives the zn. Generalize the whole `.2`.
    set zn_witness : Bool :=
      (if (Number.operator_add.alignDown x.mantissa_ x.exponent_
              (if x.negative_ then Guard.new.set_negative else Guard.new) y.exponent_).1
            > y.mantissa_ then
            ((Number.operator_add.alignDown x.mantissa_ x.exponent_
                (if x.negative_ then Guard.new.set_negative else Guard.new) y.exponent_).1
              - y.mantissa_, x.negative_)
          else
            (y.mantissa_ - (Number.operator_add.alignDown x.mantissa_ x.exponent_
              (if x.negative_ then Guard.new.set_negative else Guard.new) y.exponent_).1,
              y.negative_)).2 with hzn_def
    refine ⟨rec_state.1, rec_state.2.1, zn_witness, rec_state.2.2, _, rfl, ?_⟩
    exact hok
  · push_neg at h_xe_lt_ye
    by_cases h_xe_gt_ye : x.exponent_ > y.exponent_
    · -- Case 2: xe > ye, align y.
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_pos h_xe_gt_ye] at hok
      simp only at hok
      generalize hr : Number.operator_add.recover _ _ _ 40 = rec_state at hok
      set zn_witness : Bool :=
        (if x.mantissa_ > (Number.operator_add.alignDown y.mantissa_ y.exponent_
              (if y.negative_ then Guard.new.set_negative else Guard.new) x.exponent_).1 then
              (x.mantissa_ - (Number.operator_add.alignDown y.mantissa_ y.exponent_
                  (if y.negative_ then Guard.new.set_negative else Guard.new) x.exponent_).1,
                x.negative_)
            else
              ((Number.operator_add.alignDown y.mantissa_ y.exponent_
                  (if y.negative_ then Guard.new.set_negative else Guard.new) x.exponent_).1
                - x.mantissa_, y.negative_)).2 with hzn_def
      refine ⟨rec_state.1, rec_state.2.1, zn_witness, rec_state.2.2, _, rfl, ?_⟩
      exact hok
    · -- Case 3: xe = ye, no alignment.
      push_neg at h_xe_gt_ye
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_neg (not_lt.mpr h_xe_gt_ye)] at hok
      simp only at hok
      generalize hr : Number.operator_add.recover _ _ _ 40 = rec_state at hok
      set zn_witness : Bool :=
        (if x.mantissa_ > y.mantissa_ then (x.mantissa_ - y.mantissa_, x.negative_)
          else (y.mantissa_ - x.mantissa_, y.negative_)).2 with hzn_def
      refine ⟨rec_state.1, rec_state.2.1, zn_witness, rec_state.2.2, _, rfl, ?_⟩
      exact hok


end XRPL.Model.Protocol

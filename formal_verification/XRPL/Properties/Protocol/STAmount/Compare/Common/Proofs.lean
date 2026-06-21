import XRPL.Properties.Protocol.STAmount.Common.DiscreteDefs
import XRPL.Properties.Protocol.STAmount.Common.RoundToScalePlumbing
import XRPL.Properties.Protocol.STAmount.Common.STAmountCommonProps

namespace XRPL.Model.Protocol

/-! # Correctness of the `STAmount` comparison operators

`STAmount` compares lexicographically by (sign, exponent `mOffset`, magnitude
`mValue`), the rippled `STAmount::operator<` algorithm. This file proves that, on
**well-formed comparable** operands, every comparison operator (`<`, `≤`, `>`, `≥`,
`==`, `!=`) decides the rational order of `toRat`.

The hypothesis `CmpFaithful lhs rhs` captures exactly what makes the lexicographic
order faithful: the operands are comparable, their zeros are sign-cleared, and either
they share an exponent (integral XRP/MPT, both `mOffset = 0`) or both are 16-digit
normalized IOU mantissas (`mValue ∈ [10¹⁵, 10¹⁶)`), so that a strictly smaller
exponent forces a strictly smaller magnitude. Per-asset adapters build it from the
`NativeCanonical` / `MPTCanonical` / `IOUCanonical` structures. -/

/-- A `Bool` equals `decide P` exactly when it is `true` iff `P` holds. -/
private lemma bool_eq_decide_of_iff {b : Bool} {P : Prop} [Decidable P]
    (h : b = true ↔ P) : b = decide P := by
  by_cases hP : P
  · rw [decide_eq_true hP]; exact h.mpr hP
  · rw [decide_eq_false hP]
    cases hb : b
    · rfl
    · exact absurd (h.mp hb) hP

/-- A 16-digit normalized magnitude band: `mValue ∈ [10¹⁵, 10¹⁶)`. Within this band a
strictly smaller exponent forces a strictly smaller magnitude. -/
def STAmount.Banded (s : STAmount) : Prop :=
  10 ^ 15 ≤ s.mValue.toNat ∧ s.mValue.toNat < 10 ^ 16

/-- The conditions under which `STAmount`'s lexicographic comparison faithfully decides
the rational order of `lhs` and `rhs`. -/
structure STAmount.CmpFaithful (lhs rhs : STAmount) : Prop where
  comparable : STAmount.areComparable lhs rhs = true
  lhs_zero_sign : lhs.mValue = 0 → lhs.mIsNegative = false
  rhs_zero_sign : rhs.mValue = 0 → rhs.mIsNegative = false
  offset_or_band : lhs.mOffset = rhs.mOffset ∨ (STAmount.Banded lhs ∧ STAmount.Banded rhs)

/-- `areComparable` is symmetric. -/
lemma STAmount.areComparable_comm (a b : STAmount) :
    STAmount.areComparable a b = STAmount.areComparable b a := by
  -- the wrapper types lack `LawfulBEq`, so drop each `==` to its `BitVec` field.
  have hcur : ∀ c1 c2 : Currency, (c1 == c2) = (c2 == c1) := by
    intro c1 c2; obtain ⟨v1⟩ := c1; obtain ⟨v2⟩ := c2
    show (v1 == v2) = (v2 == v1); exact Bool.beq_comm
  have hmpt : ∀ m1 m2 : MPTIssue, (m1 == m2) = (m2 == m1) := by
    intro m1 m2; obtain ⟨⟨v1⟩⟩ := m1; obtain ⟨⟨v2⟩⟩ := m2
    show (v1 == v2) = (v2 == v1); exact Bool.beq_comm
  unfold STAmount.areComparable Asset.areComparable
  cases a.mAsset with
  | issue i1 =>
    cases b.mAsset with
    | issue i2 =>
      show (i1.native == i2.native && i1.currency == i2.currency)
         = (i2.native == i1.native && i2.currency == i1.currency)
      rw [Bool.beq_comm (a := i1.native), hcur i1.currency i2.currency]
    | mptIssue m2 => rfl
  | mptIssue m1 =>
    cases b.mAsset with
    | issue i2 => rfl
    | mptIssue m2 => show (m1 == m2) = (m2 == m1); exact hmpt m1 m2

/-- `CmpFaithful` is symmetric. -/
lemma STAmount.CmpFaithful.symm {lhs rhs : STAmount} (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.CmpFaithful rhs lhs where
  comparable := by rw [STAmount.areComparable_comm]; exact h.comparable
  lhs_zero_sign := h.rhs_zero_sign
  rhs_zero_sign := h.lhs_zero_sign
  offset_or_band := by
    rcases h.offset_or_band with he | ⟨hl, hr⟩
    · exact Or.inl he.symm
    · exact Or.inr ⟨hr, hl⟩

/-! ## Magnitude lemmas -/

/-- `mValue = 0` iff the value is zero. -/
lemma STAmount.toRat_eq_zero_iff (s : STAmount) : s.toRat = 0 ↔ s.mValue = 0 := by
  rw [← abs_eq_zero, STAmount.abs_toRat, mul_eq_zero]
  constructor
  · rintro (h | h)
    · have h0 : s.mValue.toNat = 0 := by exact_mod_cast h
      rw [← UInt64.toNat_inj, h0]; rfl
    · exact absurd h (ne_of_gt (zpow_pos (by norm_num) _))
  · intro h; left; rw [h]; rfl

/-- A non-negative-flagged STAmount has non-negative value. -/
lemma STAmount.toRat_nonneg_of (s : STAmount) (hn : s.mIsNegative = false) : 0 ≤ s.toRat := by
  rw [STAmount.toRat_of_nonneg s hn]; positivity

/-- A non-negative-flagged nonzero STAmount has strictly positive value. -/
lemma STAmount.toRat_pos_of (s : STAmount) (hn : s.mIsNegative = false) (hm : s.mValue ≠ 0) :
    0 < s.toRat := by
  have h0 : s.toRat ≠ 0 := fun h => hm ((STAmount.toRat_eq_zero_iff s).mp h)
  exact lt_of_le_of_ne (STAmount.toRat_nonneg_of s hn) (Ne.symm h0)

/-- A negative-flagged STAmount has non-positive value. -/
lemma STAmount.toRat_nonpos_of (s : STAmount) (hn : s.mIsNegative = true) : s.toRat ≤ 0 := by
  rw [STAmount.toRat_of_neg s hn]
  have : (0:ℚ) ≤ (s.mValue.toNat : ℚ) * 10 ^ s.mOffset := by positivity
  linarith

/-- A negative-flagged nonzero STAmount has strictly negative value. -/
lemma STAmount.toRat_neg_of (s : STAmount) (hn : s.mIsNegative = true) (hm : s.mValue ≠ 0) :
    s.toRat < 0 := by
  have h0 : s.toRat ≠ 0 := fun h => hm ((STAmount.toRat_eq_zero_iff s).mp h)
  exact lt_of_le_of_ne (STAmount.toRat_nonpos_of s hn) h0

/-- Exponent dominance: in the 16-digit band, a strictly smaller exponent means a
strictly smaller magnitude. -/
lemma STAmount.abs_lt_of_offset_lt (lhs rhs : STAmount)
    (hl : STAmount.Banded lhs) (hr : STAmount.Banded rhs) (hoff : lhs.mOffset < rhs.mOffset) :
    |lhs.toRat| < |rhs.toRat| := by
  rw [STAmount.abs_toRat, STAmount.abs_toRat]
  have hlhi : (lhs.mValue.toNat : ℚ) < 10 ^ 16 := by exact_mod_cast hl.2
  have hrlo : (10 ^ 15 : ℚ) ≤ (rhs.mValue.toNat : ℚ) := by exact_mod_cast hr.1
  have hpowl : (0 : ℚ) < 10 ^ lhs.mOffset := zpow_pos (by norm_num) _
  have hpowr : (0 : ℚ) < 10 ^ rhs.mOffset := zpow_pos (by norm_num) _
  calc (lhs.mValue.toNat : ℚ) * 10 ^ lhs.mOffset
      < 10 ^ 16 * 10 ^ lhs.mOffset := mul_lt_mul_of_pos_right hlhi hpowl
    _ = 10 ^ 15 * 10 ^ (lhs.mOffset + 1) := by
        rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring
    _ ≤ 10 ^ 15 * 10 ^ rhs.mOffset :=
        mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) (by omega)) (by norm_num)
    _ ≤ (rhs.mValue.toNat : ℚ) * 10 ^ rhs.mOffset :=
        mul_le_mul_of_nonneg_right hrlo (le_of_lt hpowr)

/-- Same exponent: magnitude order is `mValue` order. -/
lemma STAmount.abs_lt_iff_of_offset_eq (lhs rhs : STAmount) (hoff : lhs.mOffset = rhs.mOffset) :
    |lhs.toRat| < |rhs.toRat| ↔ lhs.mValue < rhs.mValue := by
  rw [STAmount.abs_toRat, STAmount.abs_toRat, hoff]
  have hpow : (0 : ℚ) < 10 ^ rhs.mOffset := zpow_pos (by norm_num) _
  constructor
  · intro h
    rw [UInt64.lt_iff_toNat_lt]
    exact_mod_cast lt_of_mul_lt_mul_right h (le_of_lt hpow)
  · intro h
    have h' : (lhs.mValue.toNat : ℚ) < rhs.mValue.toNat := by
      exact_mod_cast UInt64.lt_iff_toNat_lt.mp h
    exact mul_lt_mul_of_pos_right h' hpow

/-- The signed value of a negative STAmount is the negated magnitude. -/
private lemma STAmount.toRat_eq_neg_abs (s : STAmount) (hn : s.mIsNegative = true) :
    s.toRat = -|s.toRat| := by
  rw [abs_of_nonpos (STAmount.toRat_nonpos_of s hn)]; ring

/-- The signed value of a non-negative STAmount is its magnitude. -/
private lemma STAmount.toRat_eq_abs (s : STAmount) (hn : s.mIsNegative = false) :
    s.toRat = |s.toRat| := by
  rw [abs_of_nonneg (STAmount.toRat_nonneg_of s hn)]

/-- `mValue ≠ 0` iff the value is nonzero. -/
private lemma STAmount.mValue_ne_zero_iff (s : STAmount) : s.mValue ≠ 0 ↔ s.toRat ≠ 0 :=
  (STAmount.toRat_eq_zero_iff s).not.symm

/-! ## `operator_lt` and its derived operators -/

/-- **Correctness of `operator_lt`.** On well-formed comparable operands, the
lexicographic comparison decides the rational order. -/
theorem STAmount.operator_lt_eq_proof (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_lt lhs rhs = .ok (decide (lhs.toRat < rhs.toRat)) := by
  unfold STAmount.operator_lt
  rw [if_neg (show ¬ ((!STAmount.areComparable lhs rhs) = true) from by rw [h.comparable]; decide)]
  congr 1
  apply bool_eq_decide_of_iff
  by_cases hsign : lhs.mIsNegative = rhs.mIsNegative
  · -- same sign: the leading sign branch is not taken.
    rw [if_neg (show ¬ ((lhs.mIsNegative != rhs.mIsNegative) = true) from by
      rw [hsign]; simp)]
    by_cases hlz : lhs.mValue = 0
    · -- `lhs = 0`; sign-cleared, so both flags are `false`.
      have hlf : lhs.mIsNegative = false := h.lhs_zero_sign hlz
      have hrf : rhs.mIsNegative = false := by rw [← hsign, hlf]
      rw [if_pos (beq_iff_eq.mpr hlz), hrf]
      simp only [Bool.false_eq_true, if_false]
      have hl0 : lhs.toRat = 0 := (STAmount.toRat_eq_zero_iff lhs).mpr hlz
      rw [hl0, bne_iff_ne, ne_eq, ← STAmount.toRat_eq_zero_iff rhs]
      constructor
      · intro hne; exact lt_of_le_of_ne (STAmount.toRat_nonneg_of rhs hrf) (Ne.symm hne)
      · intro hlt; exact ne_of_gt hlt
    · -- `lhs ≠ 0`.
      rw [if_neg (by simpa using hlz)]
      by_cases hrz : rhs.mValue = 0
      · -- `rhs = 0`; same sign forces both flags `false`, so `lhs > 0`.
        have hrf : rhs.mIsNegative = false := h.rhs_zero_sign hrz
        have hlf : lhs.mIsNegative = false := by rw [hsign, hrf]
        rw [if_pos (beq_iff_eq.mpr hrz)]
        have hr0 : rhs.toRat = 0 := (STAmount.toRat_eq_zero_iff rhs).mpr hrz
        have hlp : 0 < lhs.toRat := STAmount.toRat_pos_of lhs hlf hlz
        rw [hr0]
        exact iff_of_false (by decide) (by linarith)
      · -- both nonzero, same sign: the exponent/value lexicographic compare.
        rw [if_neg (by simpa using hrz)]
        have hlp : lhs.mValue ≠ 0 := hlz
        have hrp : rhs.mValue ≠ 0 := hrz
        split_ifs with ho1 ho2 hv1 hv2
        · -- `offL > offR`: dominance ⟹ `|rhs| < |lhs|`.
          have hb : STAmount.Banded lhs ∧ STAmount.Banded rhs := by
            rcases h.offset_or_band with he | hb
            · exfalso; omega
            · exact hb
          have hdom := STAmount.abs_lt_of_offset_lt rhs lhs hb.2 hb.1 (by omega)
          cases hsl : lhs.mIsNegative
          · have hrf : rhs.mIsNegative = false := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_abs lhs hsl, STAmount.toRat_eq_abs rhs hrf]
            exact iff_of_false (by decide) (by linarith)
          · have hrt : rhs.mIsNegative = true := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_neg_abs lhs hsl, STAmount.toRat_eq_neg_abs rhs hrt]
            exact iff_of_true rfl (by linarith)
        · -- `offL < offR`: dominance ⟹ `|lhs| < |rhs|`.
          have hb : STAmount.Banded lhs ∧ STAmount.Banded rhs := by
            rcases h.offset_or_band with he | hb
            · exfalso; omega
            · exact hb
          have hdom := STAmount.abs_lt_of_offset_lt lhs rhs hb.1 hb.2 (by omega)
          cases hsl : lhs.mIsNegative
          · have hrf : rhs.mIsNegative = false := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_abs lhs hsl, STAmount.toRat_eq_abs rhs hrf]
            exact iff_of_true rfl (by linarith)
          · have hrt : rhs.mIsNegative = true := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_neg_abs lhs hsl, STAmount.toRat_eq_neg_abs rhs hrt]
            exact iff_of_false (by decide) (by linarith)
        · -- equal exponents, `valL > valR`: `|rhs| < |lhs|`.
          have hoff : lhs.mOffset = rhs.mOffset := by omega
          have hdom : |rhs.toRat| < |lhs.toRat| :=
            (STAmount.abs_lt_iff_of_offset_eq rhs lhs hoff.symm).mpr hv1
          cases hsl : lhs.mIsNegative
          · have hrf : rhs.mIsNegative = false := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_abs lhs hsl, STAmount.toRat_eq_abs rhs hrf]
            exact iff_of_false (by decide) (by linarith)
          · have hrt : rhs.mIsNegative = true := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_neg_abs lhs hsl, STAmount.toRat_eq_neg_abs rhs hrt]
            exact iff_of_true rfl (by linarith)
        · -- equal exponents, `valL < valR`: `|lhs| < |rhs|`.
          have hoff : lhs.mOffset = rhs.mOffset := by omega
          have hdom : |lhs.toRat| < |rhs.toRat| :=
            (STAmount.abs_lt_iff_of_offset_eq lhs rhs hoff).mpr hv2
          clear hv1
          cases hsl : lhs.mIsNegative
          · have hrf : rhs.mIsNegative = false := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_abs lhs hsl, STAmount.toRat_eq_abs rhs hrf]
            exact iff_of_true rfl (by linarith)
          · have hrt : rhs.mIsNegative = true := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_neg_abs lhs hsl, STAmount.toRat_eq_neg_abs rhs hrt]
            exact iff_of_false (by decide) (by linarith)
        · -- equal exponents and equal values: `lhs = rhs` in magnitude, so not `<`.
          have hoff : lhs.mOffset = rhs.mOffset := by omega
          have hvnat : lhs.mValue.toNat = rhs.mValue.toNat := by
            have hv1' : ¬ rhs.mValue.toNat < lhs.mValue.toNat := by
              rw [← UInt64.lt_iff_toNat_lt]; exact hv1
            have hv2' : ¬ lhs.mValue.toNat < rhs.mValue.toNat := by
              rw [← UInt64.lt_iff_toNat_lt]; exact hv2
            omega
          have habs : |lhs.toRat| = |rhs.toRat| := by
            rw [STAmount.abs_toRat, STAmount.abs_toRat, hoff, hvnat]
          cases hsl : lhs.mIsNegative
          · have hrf : rhs.mIsNegative = false := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_abs lhs hsl, STAmount.toRat_eq_abs rhs hrf]
            exact iff_of_false (by decide) (by rw [habs]; exact lt_irrefl _)
          · have hrt : rhs.mIsNegative = true := by rw [← hsign, hsl]
            rw [STAmount.toRat_eq_neg_abs lhs hsl, STAmount.toRat_eq_neg_abs rhs hrt]
            exact iff_of_false (by decide) (by rw [habs]; exact lt_irrefl _)
  · -- different signs: the leading sign branch is taken; result is `lhs.mIsNegative`.
    rw [if_pos (show (lhs.mIsNegative != rhs.mIsNegative) = true from by
      rw [bne_iff_ne]; exact hsign)]
    cases hsl : lhs.mIsNegative
    · -- `lhs ≥ 0`, `rhs < 0`: never `lhs < rhs`.
      have hrt : rhs.mIsNegative = true := by
        cases hsr : rhs.mIsNegative
        · exact absurd (hsl.trans hsr.symm) hsign
        · rfl
      have hrm : rhs.mValue ≠ 0 := fun h0 => by
        rw [h.rhs_zero_sign h0] at hrt; exact absurd hrt (by decide)
      have h1 : 0 ≤ lhs.toRat := STAmount.toRat_nonneg_of lhs hsl
      have h2 : rhs.toRat < 0 := STAmount.toRat_neg_of rhs hrt hrm
      exact iff_of_false (by decide) (by linarith)
    · -- `lhs < 0`, `rhs ≥ 0`: always `lhs < rhs`.
      have hrf : rhs.mIsNegative = false := by
        cases hsr : rhs.mIsNegative
        · rfl
        · exact absurd (hsl.trans hsr.symm) hsign
      have hlm : lhs.mValue ≠ 0 := fun h0 => by
        rw [h.lhs_zero_sign h0] at hsl; exact absurd hsl (by decide)
      have h1 : lhs.toRat < 0 := STAmount.toRat_neg_of lhs hsl hlm
      have h2 : 0 ≤ rhs.toRat := STAmount.toRat_nonneg_of rhs hrf
      exact iff_of_true rfl (by linarith)

/-- **Correctness of `operator_gt`.** -/
theorem STAmount.operator_gt_eq_proof (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_gt lhs rhs = .ok (decide (rhs.toRat < lhs.toRat)) := by
  unfold STAmount.operator_gt
  exact STAmount.operator_lt_eq_proof rhs lhs h.symm

/-- **Correctness of `operator_le`.** -/
theorem STAmount.operator_le_eq_proof (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_le lhs rhs = .ok (decide (lhs.toRat ≤ rhs.toRat)) := by
  unfold STAmount.operator_le
  rw [STAmount.operator_lt_eq_proof rhs lhs h.symm]
  show Except.ok (!decide (rhs.toRat < lhs.toRat)) = Except.ok (decide (lhs.toRat ≤ rhs.toRat))
  rw [← decide_not]
  exact congrArg Except.ok (decide_eq_decide.mpr not_lt)

/-- **Correctness of `operator_ge`.** -/
theorem STAmount.operator_ge_eq_proof (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs) :
    STAmount.operator_ge lhs rhs = .ok (decide (rhs.toRat ≤ lhs.toRat)) := by
  unfold STAmount.operator_ge
  rw [STAmount.operator_lt_eq_proof lhs rhs h]
  show Except.ok (!decide (lhs.toRat < rhs.toRat)) = Except.ok (decide (rhs.toRat ≤ lhs.toRat))
  rw [← decide_not]
  exact congrArg Except.ok (decide_eq_decide.mpr not_lt)

/-- **Correctness of `operator_eq`.** On well-formed comparable operands of the **same
asset**, field equality decides rational equality (canonical representations are
unique per value). -/
theorem STAmount.operator_eq_eq_proof (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs)
    (hasset : lhs.mAsset = rhs.mAsset) :
    STAmount.operator_eq lhs rhs = decide (lhs.toRat = rhs.toRat) := by
  apply bool_eq_decide_of_iff
  unfold STAmount.operator_eq
  rw [h.comparable]
  simp only [Bool.true_and, Bool.and_eq_true, beq_iff_eq]
  constructor
  · rintro ⟨⟨⟨hsg, hof⟩, hva⟩, _⟩
    unfold STAmount.toRat
    rw [hsg, hof, hva]
  · intro htoRat
    have habs : |lhs.toRat| = |rhs.toRat| := congrArg abs htoRat
    have hoffeq : lhs.mOffset = rhs.mOffset := by
      rcases h.offset_or_band with he | ⟨hbl, hbr⟩
      · exact he
      · by_contra hne
        rcases lt_or_gt_of_ne hne with hlt | hgt
        · exact absurd habs (ne_of_lt (STAmount.abs_lt_of_offset_lt lhs rhs hbl hbr hlt))
        · exact absurd habs.symm (ne_of_lt (STAmount.abs_lt_of_offset_lt rhs lhs hbr hbl hgt))
    have hsigneq : lhs.mIsNegative = rhs.mIsNegative := by
      cases hsl : lhs.mIsNegative <;> cases hsr : rhs.mIsNegative <;> try rfl
      · exfalso
        have h1 : 0 ≤ lhs.toRat := STAmount.toRat_nonneg_of lhs hsl
        have h2 : rhs.toRat ≤ 0 := STAmount.toRat_nonpos_of rhs hsr
        have hz : rhs.toRat = 0 := by linarith [htoRat]
        rw [h.rhs_zero_sign ((STAmount.toRat_eq_zero_iff rhs).mp hz)] at hsr
        exact absurd hsr (by decide)
      · exfalso
        have h1 : lhs.toRat ≤ 0 := STAmount.toRat_nonpos_of lhs hsl
        have h2 : 0 ≤ rhs.toRat := STAmount.toRat_nonneg_of rhs hsr
        have hz : lhs.toRat = 0 := by linarith [htoRat]
        rw [h.lhs_zero_sign ((STAmount.toRat_eq_zero_iff lhs).mp hz)] at hsl
        exact absurd hsl (by decide)
    have hvaleq : lhs.mValue = rhs.mValue := by
      have hmul : (lhs.mValue.toNat : ℚ) * 10 ^ lhs.mOffset
          = (rhs.mValue.toNat : ℚ) * 10 ^ rhs.mOffset := by
        rw [← STAmount.abs_toRat, ← STAmount.abs_toRat]; exact habs
      rw [hoffeq] at hmul
      have hpow : (10 : ℚ) ^ rhs.mOffset ≠ 0 := ne_of_gt (zpow_pos (by norm_num) _)
      rw [← UInt64.toNat_inj]
      exact_mod_cast mul_right_cancel₀ hpow hmul
    exact ⟨⟨⟨hsigneq, hoffeq⟩, hvaleq⟩, hasset⟩

/-- **Correctness of `operator_ne`.** -/
theorem STAmount.operator_ne_eq_proof (lhs rhs : STAmount) (h : STAmount.CmpFaithful lhs rhs)
    (hasset : lhs.mAsset = rhs.mAsset) :
    STAmount.operator_ne lhs rhs = decide (lhs.toRat ≠ rhs.toRat) := by
  unfold STAmount.operator_ne
  rw [STAmount.operator_eq_eq_proof lhs rhs h hasset, ← decide_not]

/-! ## Per-asset adapters for `CmpFaithful`

Each asset class supplies the `CmpFaithful` premise from its canonical form: native
XRP and MPT share `mOffset = 0`; IOU amounts are 16-digit normalized (`Banded`). -/

/-- Two canonical IOU amounts of comparable assets are comparison-faithful (both are
16-digit normalized, hence `Banded`; their nonzero mantissas make the zero clauses
vacuous). -/
lemma STAmount.CmpFaithful.ofIOU (lhs rhs : STAmount)
    (hc1 : lhs.IOUCanonical) (hc2 : rhs.IOUCanonical)
    (hcmp : STAmount.areComparable lhs rhs = true) : STAmount.CmpFaithful lhs rhs where
  comparable := hcmp
  lhs_zero_sign := fun h0 => by have := hc1.mant_lo; rw [h0] at this; simp at this
  rhs_zero_sign := fun h0 => by have := hc2.mant_lo; rw [h0] at this; simp at this
  offset_or_band := Or.inr ⟨⟨hc1.mant_lo, hc1.mant_hi⟩, ⟨hc2.mant_lo, hc2.mant_hi⟩⟩

/-- Two canonical MPT amounts of comparable assets are comparison-faithful (both have
`mOffset = 0` and sign-cleared zeros). -/
lemma STAmount.CmpFaithful.ofMPT (lhs rhs : STAmount)
    (hc1 : lhs.MPTCanonical) (hc2 : rhs.MPTCanonical)
    (hcmp : STAmount.areComparable lhs rhs = true) : STAmount.CmpFaithful lhs rhs where
  comparable := hcmp
  lhs_zero_sign := hc1.zero_sign_cleared
  rhs_zero_sign := hc2.zero_sign_cleared
  offset_or_band := Or.inl (by rw [hc1.offset_zero, hc2.offset_zero])

/-- Two canonical native (XRP) amounts with sign-cleared zeros are comparison-faithful
(both are XRP, hence comparable, with `mOffset = 0`). -/
lemma STAmount.CmpFaithful.ofNative (lhs rhs : STAmount)
    (hc1 : lhs.NativeCanonical) (hc2 : rhs.NativeCanonical)
    (h0l : lhs.mValue = 0 → lhs.mIsNegative = false)
    (h0r : rhs.mValue = 0 → rhs.mIsNegative = false) : STAmount.CmpFaithful lhs rhs where
  comparable := by
    unfold STAmount.areComparable; rw [hc1.is_xrp, hc2.is_xrp]; decide
  lhs_zero_sign := h0l
  rhs_zero_sign := h0r
  offset_or_band := Or.inl (by rw [hc1.offset_zero, hc2.offset_zero])

end XRPL.Model.Protocol

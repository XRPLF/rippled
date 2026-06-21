import Mathlib.Tactic

import XRPL.Properties.Approx
import XRPL.Model.Protocol.STAmount

namespace XRPL.Model.Protocol

instance : RatValued STAmount where
  toRat := STAmount.toRat

instance : RatValued IOUAmount where
  toRat := IOUAmount.toRat

/-- A well-formed native (XRP) STAmount: XRP asset, stored canonically with
`mOffset = 0`, and within the legal drops range (`isLegalNet`). The model's
native arithmetic reads `signedDrops` (which ignores `mOffset`), so these are the
preconditions under which it is faithful. -/
structure STAmount.NativeCanonical (s : STAmount) : Prop where
  is_xrp : s.mAsset = xrpAsset
  offset_zero : s.mOffset = 0
  in_range : s.mValue.toNat ≤ kMaxNativeN

/-- `result` is the value `truth` correctly rounded onto the scale grid
`10^s·ℤ` (faithfully for `.to_nearest`: one of the two enclosing grid
points). Discrete-grid analog of `RoundsWithin` for quantization at a fixed
decimal scale; the grid is uniform and total, so no `Option` and no
existential. -/
def RoundsToRepresentableAt {A : Type} [RatValued A] (result : A) (truth : ℚ) (s : ℤ)
    (mode : rounding_mode) : Prop :=
  match mode with
  | .to_nearest   =>
      RatValued.toRat result = ⌊truth / 10 ^ s⌋ * 10 ^ s ∨
      RatValued.toRat result = ⌈truth / 10 ^ s⌉ * 10 ^ s
  | .upward       => RatValued.toRat result = ⌈truth / 10 ^ s⌉ * 10 ^ s
  | .downward     => RatValued.toRat result = ⌊truth / 10 ^ s⌋ * 10 ^ s
  | .towards_zero =>
      RatValued.toRat result
        = (if truth ≥ 0 then ⌊truth / 10 ^ s⌋ else ⌈truth / 10 ^ s⌉) * 10 ^ s

/-- An exact result rounds within zero relative error in every mode. -/
lemma RoundsWithin_of_eq {A : Type} [RatValued A] (result : A) (truth : ℚ)
    (mode : rounding_mode) (h : RatValued.toRat result = truth) :
    RoundsWithin result truth mode 0 := by
  unfold RoundsWithin
  cases mode <;> simp [h]

/-- `RoundsWithin` is monotone in the error budget: a bound at `ε` is also a bound
at any `ε' ≥ ε`. Lets the exact (`ε = 0`) native/MPT paths share a single
relative-error statement with the double-rounded IOU path. -/
lemma RoundsWithin_mono {A : Type} [RatValued A] (result : A) (truth ε ε' : ℚ)
    (mode : rounding_mode) (h : RoundsWithin result truth mode ε) (hε : ε ≤ ε') :
    RoundsWithin result truth mode ε' := by
  have hmul : |truth| * ε ≤ |truth| * ε' := mul_le_mul_of_nonneg_left hε (abs_nonneg _)
  unfold RoundsWithin at h ⊢
  cases mode <;>
    first
    | exact le_trans h hmul
    | exact ⟨h.1, le_trans h.2 hmul⟩

/-- A faithful rounding (any mode, relative error `ε ≤ 1`) at most doubles the
magnitude: `|result| ≤ 2·|truth|`. The crude magnitude bound the exponent-overflow
argument needs, uniform across modes. -/
lemma RoundsWithin_abs_le_two {A : Type} [RatValued A] (result : A) (truth ε : ℚ)
    (mode : rounding_mode) (h : RoundsWithin result truth mode ε) (hε : ε ≤ 1) :
    |RatValued.toRat result| ≤ |truth| * 2 := by
  set r := RatValued.toRat result with hr
  have habs_nn : 0 ≤ |truth| := abs_nonneg _
  have hmul : |truth| * ε ≤ |truth| * 1 := mul_le_mul_of_nonneg_left hε habs_nn
  unfold RoundsWithin at h
  cases mode with
  | to_nearest =>
    have htri := abs_sub_abs_le_abs_sub r truth
    nlinarith [h, htri, hmul]
  | downward =>
    obtain ⟨hd, hm⟩ := h
    have h1 : |r - truth| ≤ |truth| * ε := by rw [abs_of_nonpos (by linarith), neg_sub]; exact hm
    have htri := abs_sub_abs_le_abs_sub r truth
    nlinarith [h1, htri, hmul]
  | upward =>
    obtain ⟨hd, hm⟩ := h
    have h1 : |r - truth| ≤ |truth| * ε := by rw [abs_of_nonneg (by linarith)]; exact hm
    have htri := abs_sub_abs_le_abs_sub r truth
    nlinarith [h1, htri, hmul]
  | towards_zero =>
    obtain ⟨hd, _⟩ := h
    nlinarith [hd, habs_nn]

/-! ## Nested-grid composition

Rounding at `10^E` and then at `10^s = 10^(E+c)` equals rounding at `10^s`
directly, for the directed modes. (For `to_nearest` only two-neighbor
membership survives the composition; the main theorem handles that case by
direct membership, not by composition.) -/

/-- Floor composes through a coarser grid: `⌊⌊x⌋ / n⌋ = ⌊x / n⌋`. -/
lemma Int.floor_floor_div (x : ℚ) (n : ℕ) (hn : 0 < n) :
    ⌊(⌊x⌋ : ℚ) / (n : ℚ)⌋ = ⌊x / (n : ℚ)⌋ := by
  have hn_q : (0 : ℚ) < (n : ℚ) := by exact_mod_cast hn
  apply le_antisymm
  · -- ⌊⌊x⌋/n⌋ ≤ x/n since n·⌊⌊x⌋/n⌋ ≤ ⌊x⌋ ≤ x
    apply Int.le_floor.mpr
    have h1 : ((⌊(⌊x⌋ : ℚ) / (n : ℚ)⌋ : ℤ) : ℚ) ≤ (⌊x⌋ : ℚ) / (n : ℚ) := Int.floor_le _
    have h2 : (⌊x⌋ : ℚ) ≤ x := Int.floor_le x
    calc ((⌊(⌊x⌋ : ℚ) / (n : ℚ)⌋ : ℤ) : ℚ) ≤ (⌊x⌋ : ℚ) / (n : ℚ) := h1
      _ ≤ x / (n : ℚ) := div_le_div_of_nonneg_right h2 (le_of_lt hn_q)
  · -- n·⌊x/n⌋ ≤ x integer ⟹ n·⌊x/n⌋ ≤ ⌊x⌋ ⟹ ⌊x/n⌋ ≤ ⌊x⌋/n
    apply Int.le_floor.mpr
    have h1 : ((⌊x / (n : ℚ)⌋ : ℤ) : ℚ) * (n : ℚ) ≤ x := by
      have := Int.floor_le (x / (n : ℚ))
      calc ((⌊x / (n : ℚ)⌋ : ℤ) : ℚ) * (n : ℚ) ≤ (x / (n : ℚ)) * (n : ℚ) :=
            mul_le_mul_of_nonneg_right this (le_of_lt hn_q)
        _ = x := div_mul_cancel₀ x (ne_of_gt hn_q)
    have h2 : ((⌊x / (n : ℚ)⌋ * (n : ℤ) : ℤ) : ℚ) ≤ x := by push_cast; exact_mod_cast h1
    have h3 : ⌊x / (n : ℚ)⌋ * (n : ℤ) ≤ ⌊x⌋ := Int.le_floor.mpr h2
    have h4 : ((⌊x / (n : ℚ)⌋ * (n : ℤ) : ℤ) : ℚ) ≤ (⌊x⌋ : ℚ) := by exact_mod_cast h3
    rw [le_div_iff₀ hn_q]
    push_cast at h4 ⊢
    linarith

/-- Ceil composes through a coarser grid: `⌈⌈x⌉ / n⌉ = ⌈x / n⌉`. -/
lemma Int.ceil_ceil_div (x : ℚ) (n : ℕ) (hn : 0 < n) :
    ⌈(⌈x⌉ : ℚ) / (n : ℚ)⌉ = ⌈x / (n : ℚ)⌉ := by
  have hn_q : (0 : ℚ) < (n : ℚ) := by exact_mod_cast hn
  apply le_antisymm
  · -- ⌈x⌉ ≤ n·⌈x/n⌉ (the latter is an integer ≥ x) ⟹ ⌈⌈x⌉/n⌉ ≤ ⌈x/n⌉
    apply Int.ceil_le.mpr
    have h1 : x ≤ ((⌈x / (n : ℚ)⌉ : ℤ) : ℚ) * (n : ℚ) := by
      have := Int.le_ceil (x / (n : ℚ))
      calc x = (x / (n : ℚ)) * (n : ℚ) := (div_mul_cancel₀ x (ne_of_gt hn_q)).symm
        _ ≤ ((⌈x / (n : ℚ)⌉ : ℤ) : ℚ) * (n : ℚ) :=
            mul_le_mul_of_nonneg_right this (le_of_lt hn_q)
    have h2 : x ≤ ((⌈x / (n : ℚ)⌉ * (n : ℤ) : ℤ) : ℚ) := by push_cast; exact_mod_cast h1
    have h3 : ⌈x⌉ ≤ ⌈x / (n : ℚ)⌉ * (n : ℤ) := Int.ceil_le.mpr h2
    have h4 : ((⌈x⌉ : ℤ) : ℚ) ≤ ((⌈x / (n : ℚ)⌉ * (n : ℤ) : ℤ) : ℚ) := by exact_mod_cast h3
    rw [div_le_iff₀ hn_q]
    push_cast at h4 ⊢
    linarith
  · -- x/n ≤ ⌈x⌉/n ⟹ ⌈x/n⌉ ≤ ⌈⌈x⌉/n⌉
    apply Int.ceil_le_ceil
    exact div_le_div_of_nonneg_right (Int.le_ceil x) (le_of_lt hn_q)

/-! ## Relative-error composition for double-rounded arithmetic

STAmount IOU arithmetic rounds twice (the `Number` operation, then the
`ofNumber`/canonicalize snap onto the 16-digit grid). The two relative errors
compose by a triangle inequality, so the result inherits a relative-error bound
despite the double rounding. -/

/-- Two relative-error roundings compose: `mid` within `ε₁` of `truth` and
`result` within `ε₂` of `mid` gives `result` within `ε₁ + ε₂ + ε₁·ε₂` of `truth`. -/
lemma rel_error_trans {result mid truth ε₁ ε₂ : ℚ}
    (h1 : |mid - truth| ≤ |truth| * ε₁) (h2 : |result - mid| ≤ |mid| * ε₂)
    (hε₂ : 0 ≤ ε₂) :
    |result - truth| ≤ |truth| * (ε₁ + ε₂ + ε₁ * ε₂) := by
  have hmid : |mid| ≤ |truth| + |truth| * ε₁ := by
    have h := abs_sub_abs_le_abs_sub mid truth
    linarith
  have hstep : |mid| * ε₂ ≤ (|truth| + |truth| * ε₁) * ε₂ :=
    mul_le_mul_of_nonneg_right hmid hε₂
  calc |result - truth| ≤ |result - mid| + |mid - truth| := abs_sub_le _ _ _
    _ ≤ |mid| * ε₂ + |truth| * ε₁ := by linarith
    _ ≤ (|truth| + |truth| * ε₁) * ε₂ + |truth| * ε₁ := by linarith
    _ = |truth| * (ε₁ + ε₂ + ε₁ * ε₂) := by ring

/-- `RoundsWithin` (to_nearest) form of `rel_error_trans`, the composition an
IOU `multiply`/`add` needs: a `Number`-operation bound (`ε₁`) then the IOU
re-rounding bound (`ε₂`). -/
lemma RoundsWithin_to_nearest_trans {A : Type} [RatValued A]
    (result : A) (mid truth ε₁ ε₂ : ℚ)
    (h1 : |mid - truth| ≤ |truth| * ε₁)
    (h2 : |RatValued.toRat result - mid| ≤ |mid| * ε₂) (hε₂ : 0 ≤ ε₂) :
    RoundsWithin result truth .to_nearest (ε₁ + ε₂ + ε₁ * ε₂) :=
  rel_error_trans h1 h2 hε₂

/-- `RoundsWithin` depends only on the embedded rational value: transport across an
equal-`toRat` boundary (here `STAmount` ↔ its backing `IOUAmount`). -/
lemma RoundsWithin_toRat_congr {A B : Type} [RatValued A] [RatValued B]
    (a : A) (b : B) (truth ε : ℚ) (mode : rounding_mode)
    (h : RatValued.toRat a = RatValued.toRat b) (hb : RoundsWithin b truth mode ε) :
    RoundsWithin a truth mode ε := by
  unfold RoundsWithin at hb ⊢; rw [h]; exact hb

/-- **`RoundsWithin` transitivity, every mode.** A double-rounding composes: if
`intermediate` rounds `truth` within `ε₁` and `result` rounds `intermediate`'s value
within `ε₂` (both in direction `mode`), then `result` rounds `truth` within
`ε₁ + ε₂ + ε₁·ε₂`. For the directed modes the correct-side clause composes
(`result ≤ intermediate ≤ truth`, etc.); the magnitude is `rel_error_trans`. This is
the engine behind the directed-mode IOU headlines (`Number`-op bound ∘ re-round bound). -/
lemma RoundsWithin_trans {A B : Type} [RatValued A] [RatValued B]
    (result : A) (intermediate : B) (truth ε₁ ε₂ : ℚ) (mode : rounding_mode)
    (h1 : RoundsWithin intermediate truth mode ε₁)
    (h2 : RoundsWithin result (RatValued.toRat intermediate) mode ε₂)
    (hε₁ : 0 ≤ ε₁) (hε₂ : 0 ≤ ε₂) :
    RoundsWithin result truth mode (ε₁ + ε₂ + ε₁ * ε₂) := by
  set mid := RatValued.toRat intermediate with hmid
  set r := RatValued.toRat result with hr
  unfold RoundsWithin at h1 h2 ⊢
  cases mode with
  | to_nearest => exact rel_error_trans h1 h2 hε₂
  | downward =>
    obtain ⟨hd1, hm1⟩ := h1
    obtain ⟨hd2, hm2⟩ := h2
    have ha1 : |mid - truth| ≤ |truth| * ε₁ := by
      rw [abs_of_nonpos (by linarith), neg_sub]; exact hm1
    have ha2 : |r - mid| ≤ |mid| * ε₂ := by
      rw [abs_of_nonpos (by linarith), neg_sub]; exact hm2
    have hmag := rel_error_trans ha1 ha2 hε₂
    refine ⟨le_trans hd2 hd1, ?_⟩
    calc truth - r = |r - truth| := by rw [abs_of_nonpos (by linarith [le_trans hd2 hd1]), neg_sub]
      _ ≤ |truth| * (ε₁ + ε₂ + ε₁ * ε₂) := hmag
  | upward =>
    obtain ⟨hd1, hm1⟩ := h1
    obtain ⟨hd2, hm2⟩ := h2
    have ha1 : |mid - truth| ≤ |truth| * ε₁ := by
      rw [abs_of_nonneg (by linarith)]; exact hm1
    have ha2 : |r - mid| ≤ |mid| * ε₂ := by
      rw [abs_of_nonneg (by linarith)]; exact hm2
    have hmag := rel_error_trans ha1 ha2 hε₂
    refine ⟨le_trans hd1 hd2, ?_⟩
    calc r - truth = |r - truth| := by rw [abs_of_nonneg (by linarith [le_trans hd1 hd2])]
      _ ≤ |truth| * (ε₁ + ε₂ + ε₁ * ε₂) := hmag
  | towards_zero =>
    obtain ⟨hd1, hm1⟩ := h1
    obtain ⟨hd2, hm2⟩ := h2
    refine ⟨le_trans hd2 hd1, ?_⟩
    have hmidε : |mid| * ε₂ ≤ |truth| * ε₂ := mul_le_mul_of_nonneg_right hd1 hε₂
    have hsum : (|truth| - |mid|) + (|mid| - |r|) ≤ |truth| * ε₁ + |truth| * ε₂ := by
      have := le_trans hm2 hmidε
      linarith
    have hfinal : |truth| * ε₁ + |truth| * ε₂ ≤ |truth| * (ε₁ + ε₂ + ε₁ * ε₂) := by
      have h0 : 0 ≤ |truth| * (ε₁ * ε₂) := by
        have : 0 ≤ ε₁ * ε₂ := mul_nonneg hε₁ hε₂
        positivity
      nlinarith [h0]
    linarith

/-- STAmount discrete/ULP guarantee: `result` lands within `k` ULP of `truth`
(ULP = `10^result.exponent`, the IOU grid step), on the correct side for directed
modes. `k = 1` is the achievable target for IOU arithmetic, the relaxed
`RoundsToRepresentable` that survives double-rounding. -/
def STAmount.RoundsToRepresentableWithin
    (result : STAmount) (truth : ℚ) (k : ℕ) (mode : rounding_mode) : Prop :=
  (match mode with
   | .downward => result.toRat ≤ truth
   | .upward   => truth ≤ result.toRat
   | _         => True) ∧
  |result.toRat - truth| ≤ (k : ℚ) * (10 : ℚ) ^ result.exponent

/-- An exact result lands within `0` ULP of `truth` in every mode. -/
lemma RoundsToRepresentableWithin_of_eq (result : STAmount) (truth : ℚ) (mode : rounding_mode)
    (h : result.toRat = truth) : STAmount.RoundsToRepresentableWithin result truth 0 mode := by
  unfold STAmount.RoundsToRepresentableWithin
  refine ⟨?_, ?_⟩
  · cases mode <;> simp [h]
  · rw [h]; simp

/-- **Double-rounding ⟹ tight ULP *magnitude* bound** (mode- and sign-independent). The
IOU pipeline rounds the exact `truth` to a 19-digit `Number` `r` (relative error `ε`,
tiny) then snaps `r` to the 16-digit result (within `csnap` ULP of `r`). Since
`|r| ≤ 10¹⁶·ULP` and `ε ≤ 10⁻¹⁷`, the Number step contributes `< ⅕` ULP, so the result
lands within `k` ULP whenever `csnap + ⅕ ≤ k`. This is the accuracy guarantee with no
directional claim. It holds for any operand signs. -/
lemma STAmount.double_round_abs_le
    (result : STAmount) (r : Number) (truth : ℚ) (k : ℕ) (csnap ε : ℚ)
    (hsnap : |result.toRat - r.toRat| ≤ csnap * (10 : ℚ) ^ result.exponent)
    (hr_ulp : |r.toRat| ≤ 10 ^ 16 * (10 : ℚ) ^ result.exponent)
    (hop : |r.toRat - truth| ≤ |truth| * ε)
    (hε0 : 0 ≤ ε) (hεle : ε ≤ (10 : ℚ) ^ (-17 : ℤ))
    (hk : csnap + 1 / 5 ≤ (k : ℚ)) :
    |result.toRat - truth| ≤ (k : ℚ) * (10 : ℚ) ^ result.exponent := by
  set U : ℚ := (10 : ℚ) ^ result.exponent with hU
  have hU_pos : 0 < U := zpow_pos (by norm_num) _
  have hε_half : ε ≤ 1 / 2 :=
    le_trans hεle (by rw [show ((-17) : ℤ) = -(17 : ℕ) from rfl, zpow_neg, zpow_natCast]; norm_num)
  have hop' : |truth - r.toRat| ≤ |truth| * ε := by rw [abs_sub_comm]; exact hop
  have htri : |truth| - |r.toRat| ≤ |truth| * ε :=
    le_trans (abs_sub_abs_le_abs_sub truth r.toRat) hop'
  have htruth_2r : |truth| ≤ 2 * |r.toRat| := by
    have hh : |truth| * ε ≤ |truth| * (1 / 2) := mul_le_mul_of_nonneg_left hε_half (abs_nonneg _)
    linarith [htri, hh]
  have hop_ulp : |r.toRat - truth| ≤ (1 / 5) * U :=
    calc |r.toRat - truth| ≤ |truth| * ε := hop
      _ ≤ 2 * |r.toRat| * ε := by nlinarith [htruth_2r, hε0]
      _ ≤ 2 * (10 ^ 16 * U) * ε := by nlinarith [hr_ulp, hε0]
      _ ≤ 2 * (10 ^ 16 * U) * (10 : ℚ) ^ (-17 : ℤ) :=
          mul_le_mul_of_nonneg_left hεle (by positivity)
      _ = (1 / 5) * U := by
          rw [show ((-17) : ℤ) = -(17 : ℕ) from rfl, zpow_neg, zpow_natCast]; field_simp; ring
  calc |result.toRat - truth| ≤ |result.toRat - r.toRat| + |r.toRat - truth| := abs_sub_le _ _ _
    _ ≤ csnap * U + (1 / 5) * U := by linarith [hsnap, hop_ulp]
    _ ≤ (k : ℚ) * U := by nlinarith [hk, hU_pos]

/-- **Double-rounding ⟹ tight ULP bound**, with the directed side-clause passed through.
With `csnap = ½` (`to_nearest`) gives `k = 1`; `csnap = 1` (directed) gives `k = 2`. -/
lemma STAmount.RoundsToRepresentableWithin_of_double_round
    (result : STAmount) (r : Number) (truth : ℚ) (mode : rounding_mode) (k : ℕ) (csnap ε : ℚ)
    (h_dir : (match mode with
              | .downward => result.toRat ≤ truth
              | .upward => truth ≤ result.toRat
              | _ => True))
    (hsnap : |result.toRat - r.toRat| ≤ csnap * (10 : ℚ) ^ result.exponent)
    (hr_ulp : |r.toRat| ≤ 10 ^ 16 * (10 : ℚ) ^ result.exponent)
    (hop : |r.toRat - truth| ≤ |truth| * ε)
    (hε0 : 0 ≤ ε) (hεle : ε ≤ (10 : ℚ) ^ (-17 : ℤ))
    (hk : csnap + 1 / 5 ≤ (k : ℚ)) :
    STAmount.RoundsToRepresentableWithin result truth k mode :=
  ⟨h_dir, STAmount.double_round_abs_le result r truth k csnap ε hsnap hr_ulp hop hε0 hεle hk⟩

/-- Extract the magnitude bound `|result − truth| ≤ |truth|·ε` from `RoundsWithin` in
**any** mode, when both `result` and `truth` are non-negative. (For `to_nearest` it is
the bound directly; for the directed modes it follows from the one-sided clause.) -/
lemma RoundsWithin_abs_diff_le_of_nonneg {A : Type} [RatValued A] (result : A) (truth ε : ℚ)
    (mode : rounding_mode) (h : RoundsWithin result truth mode ε)
    (hr : 0 ≤ RatValued.toRat result) (ht : 0 ≤ truth) :
    |RatValued.toRat result - truth| ≤ |truth| * ε := by
  unfold RoundsWithin at h
  cases mode with
  | to_nearest => exact h
  | downward =>
    obtain ⟨hle, hm⟩ := h
    rw [abs_of_nonpos (by linarith), neg_sub]; exact hm
  | upward =>
    obtain ⟨hge, hm⟩ := h
    rw [abs_of_nonneg (by linarith)]; exact hm
  | towards_zero =>
    obtain ⟨hle, hm⟩ := h
    rw [abs_of_nonneg hr, abs_of_nonneg ht] at hle hm
    rw [abs_of_nonpos (by linarith), neg_sub, abs_of_nonneg ht]; exact hm

end XRPL.Model.Protocol

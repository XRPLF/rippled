import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.IOUAmount.Common.ToRatLemmas

/-! # Proof bodies for the `IOUAmount` accessor correctness headlines.

The `mantissa` / `exponent` accessors are faithful (`mantissa · 10^exponent = toRat`) and
`toBool` decides non-zeroness. Mirrors `Number/Accessors/`. The thin headlines live in
`IOUAmount.Accessors.Accessors`. -/

namespace XRPL.Model.Protocol

/-- **The public `mantissa` / `exponent` accessors are faithful.** -/
theorem IOUAmount.mantissa_exponent_eq_toRat_proof (a : IOUAmount) :
    ((IOUAmount.mantissa a).toInt : ℚ) * (10 : ℚ) ^ (IOUAmount.exponent a) = a.toRat :=
  (IOUAmount.toRat_eq a).symm

/-- `toRat = 0` exactly when the mantissa is zero. -/
private lemma IOUAmount.toRat_eq_zero_iff (a : IOUAmount) : a.toRat = 0 ↔ a.mantissa_ = 0 := by
  rw [IOUAmount.toRat_eq, mul_eq_zero]
  constructor
  · rintro (h | h)
    · rw [Int.cast_eq_zero] at h
      rw [← Int64.toInt_inj, show ((0 : Int64)).toInt = 0 from by decide]; exact h
    · exact absurd h (by positivity)
  · intro h; left; rw [h]; norm_num [show ((0 : Int64)).toInt = 0 from by decide]

/-- **`toBool` decides non-zeroness.** -/
theorem IOUAmount.toBool_iff_proof (a : IOUAmount) :
    IOUAmount.toBool a = true ↔ a.toRat ≠ 0 := by
  unfold IOUAmount.toBool
  rw [bne_iff_ne, ne_eq, ne_eq, IOUAmount.toRat_eq_zero_iff]

end XRPL.Model.Protocol

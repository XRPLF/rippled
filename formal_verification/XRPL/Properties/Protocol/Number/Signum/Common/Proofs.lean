import XRPL.Properties.Protocol.Number.Common.ToRatLemmas


namespace XRPL.Model.Protocol

/-- A normalized number is nonzero exactly when its mantissa is nonzero. The
only normalized value with `mantissa_ = 0` is the canonical `Number.zero`
(positive), so a negative normalized number always has a nonzero mantissa. -/
lemma Number.mantissa_ne_zero_of_negative (n : Number) (hn : n.isNormalized)
    (hneg : n.negative_ = true) : n.mantissa_ ≠ 0 := by
  rcases hn with heq | ⟨hlo, _⟩
  · rw [heq] at hneg; exact absurd hneg (by decide)
  · intro hm; rw [hm] at hlo; exact absurd hlo (by decide)


end XRPL.Model.Protocol

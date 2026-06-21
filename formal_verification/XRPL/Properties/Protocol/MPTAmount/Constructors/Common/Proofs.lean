import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount
import XRPL.Properties.Protocol.Number.ToRep.ToRep

/-! # Proof bodies for the `MPTAmount` constructor correctness headlines. -/

namespace XRPL.Model.Protocol

/-- **`ofInt64` is value-exact.** -/
theorem MPTAmount.ofInt64_toRat_proof (v : Int64) :
    (MPTAmount.ofInt64 v).toRat = (v.toInt : ℚ) := rfl

/-- **`ofNumber` rounds to within one** (it delegates to `Number.to_rep`). -/
theorem MPTAmount.ofNumber_within_one_proof (n : Number) (mode : rounding_mode)
    (result : MPTAmount) (hn : n.isNormalized)
    (hok : MPTAmount.ofNumber n mode = .ok result) :
    |result.toRat - n.toRat| < 1 := by
  unfold MPTAmount.ofNumber at hok
  split at hok
  · rename_i rep heq
    have hr : result.toRat = (rep.toInt : ℚ) := by
      rw [← (Except.ok.inj hok)]; rfl
    rw [hr]; exact to_rep_within_one n mode rep hn heq
  · exact absurd hok (by simp)

end XRPL.Model.Protocol

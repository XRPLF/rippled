import Mathlib.Tactic

import XRPL.Properties.Approx
import XRPL.Properties.Protocol.Number.Common.ClosestRepresentable
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize


namespace XRPL.Model.Protocol

/-- `result` rounds the exact rational `truth` to one of the two adjacent
representable Numbers. Discrete-grid analog of `RoundsWithin`. -/
def Number.RoundsToRepresentable (result : Number) (truth : ℚ) (mode : rounding_mode) : Prop :=
  match mode with
  | .to_nearest   =>
      (∃ n, Number.lower truth = some n ∧ result.toRat = n.toRat) ∨
      (∃ n, Number.upper truth = some n ∧ result.toRat = n.toRat)
  | .upward       =>
      ∃ n, Number.upper truth = some n ∧ result.toRat = n.toRat
  | .downward     =>
      ∃ n, Number.lower truth = some n ∧ result.toRat = n.toRat
  | .towards_zero =>
      ∃ n, (if truth ≥ 0 then Number.lower truth else Number.upper truth) = some n ∧
            result.toRat = n.toRat

end XRPL.Model.Protocol

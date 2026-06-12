import Mathlib.Tactic

import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.ClosestRepresentable
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `result` rounds the exact rational `truth` to one of the two adjacent
representable Numbers. Discrete-grid analog of `Rounds`. -/
def Number.RoundsDiscrete (result : Number) (truth : ℚ) (mode : rounding_mode) : Prop :=
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

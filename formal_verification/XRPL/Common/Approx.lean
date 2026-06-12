import Mathlib.Tactic

import XRPL.Model.Protocol.Number

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Approx A` — `A` is a carrier type that maps to an exact rational value.

Mirrors `girving/interval`'s `Approx` class, specialized to target `ℚ`
(since all our carriers — `Number`, `STAmount`, `MPTAmount` — map exactly to
rationals via `toRat`).

For each carrier `A`, providing an `Approx A` instance lets `Rounds` and any
future polymorphic helper resolve the `toRat` mapping automatically. -/
class Approx (A : Type) where
  toRat : A → ℚ

instance : Approx Number where
  toRat := Number.toRat

/-- `Rounds result truth mode ε` — `result` rounds the exact rational `truth`
in direction `mode` with relative error bounded by `ε`.

For `.to_nearest`, the error is two-sided: `|result - truth| ≤ |truth| · ε`.
For `.upward` / `.downward`, the result is on one side of truth AND the gap is
bounded. For `.towards_zero`, the magnitude is bounded and result is closer to
zero than truth.

Polymorphic over any carrier type `A` with an `Approx A` instance. This lets
the same predicate apply to `Number`, `STAmount`, and `MPTAmount` results
without duplication. -/
def Rounds {A : Type} [Approx A] (result : A) (truth : ℚ) (mode : rounding_mode) (ε : ℚ) : Prop :=
  match mode with
  | .to_nearest   => |Approx.toRat result - truth| ≤ |truth| * ε
  | .upward       => truth ≤ Approx.toRat result ∧
                     Approx.toRat result - truth ≤ |truth| * ε
  | .downward     => Approx.toRat result ≤ truth ∧
                     truth - Approx.toRat result ≤ |truth| * ε
  | .towards_zero => |Approx.toRat result| ≤ |truth| ∧
                     |truth| - |Approx.toRat result| ≤ |truth| * ε

end XRPL.Model.Protocol

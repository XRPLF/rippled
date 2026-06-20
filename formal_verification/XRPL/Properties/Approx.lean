import Mathlib.Tactic

import XRPL.Model.Protocol.Number

/-!
Every amount type (`Number`, `STAmount`...) maps to an
exact rational. `RatValued A` packages that map as `toRat : A → ℚ`, and `RoundsWithin`
uses it to state what it means for a computed `result` to be a correct rounding
of an exact rational `truth` in a given rounding `mode`
within relative error `ε`.
-/

namespace XRPL.Model.Protocol

class RatValued (A : Type) where
  toRat : A → ℚ

instance : RatValued Number where
  toRat := Number.toRat

/-- `result` rounds `truth` in direction `mode` with relative error at most `ε`. -/
def RoundsWithin {A : Type} [RatValued A] (result : A) (truth : ℚ) (mode : rounding_mode) (ε : ℚ) : Prop :=
  match mode with
  | .to_nearest   => |RatValued.toRat result - truth| ≤ |truth| * ε
  | .upward       => truth ≤ RatValued.toRat result ∧
                     RatValued.toRat result - truth ≤ |truth| * ε
  | .downward     => RatValued.toRat result ≤ truth ∧
                     truth - RatValued.toRat result ≤ |truth| * ε
  | .towards_zero => |RatValued.toRat result| ≤ |truth| ∧
                     |truth| - |RatValued.toRat result| ≤ |truth| * ε

/-- Dual of `RoundsWithin`: `result` witnesses that relative error `lo` is *exceeded*,
proving a `RoundsWithin` bound's numerator is optimal (the bound is tight). -/
def RoundsWithinWitness {A : Type} [RatValued A] (result : A) (truth : ℚ) (lo : ℚ) : Prop :=
  |RatValued.toRat result - truth| > |truth| * lo

end XRPL.Model.Protocol

import XRPL.Model.Protocol.STNumber


namespace XRPL.Model.Protocol

-- SoeDefault canonicalization: a present field equal to its default is removed.
-- Mirrors `if (style == SoeDefault && field.isDefault()) sle.makeFieldAbsent(field)`.
def makeFieldAbsent {α : Type} (isDefault : α → Bool) : Option α → Option α
  | none => none
  | some x => if isDefault x then none else some x

-- Round one NeedsAsset STNumber field to the asset's precision, then makeFieldAbsent
def associateNumberField (asset : Asset) (mode : rounding_mode) :
    Option STNumber → Except String (Option STNumber)
  | none => .ok none
  | some n =>
    match n.associateAsset asset mode with
    | .error e => .error e
    | .ok rounded => .ok (makeFieldAbsent STNumber.isDefault (some rounded))

end XRPL.Model.Protocol

import XRPL.Model.Protocol.Asset
import XRPL.Model.Protocol.Number
import XRPL.Model.Protocol.STAmount


namespace XRPL.Model.Protocol

structure STNumber where
  value_ : Number
  deriving DecidableEq, Repr

def STNumber.ofNumber (value : Number := Number.zero) : STNumber := { value_ := value }
def STNumber.toNumber (s : STNumber) : Number := s.value_

def STNumber.value (s : STNumber) : Number := s.value_
def STNumber.setValue (s : STNumber) (v : Number) : STNumber := { s with value_ := v }
def STNumber.operator_assign (s : STNumber) (rhs : Number) : STNumber := s.setValue rhs

-- C++ `STNumber::isDefault()` — `value_ == Number()`.
def STNumber.isDefault (s : STNumber) : Bool :=
  s.value_.operator_eq Number.zero

-- C++ `STNumber::associateAsset(a)` rounds value_ to the asset's precision.
def STNumber.associateAsset (s : STNumber) (a : Asset) (mode : rounding_mode)
    : Except String STNumber :=
  match STAmount.roundToAsset a s.value_ mode with
  | .error e => .error e
  | .ok rounded => .ok { s with value_ := rounded }

end XRPL.Model.Protocol

import XRPL.Model.Protocol.Number

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol

structure FFINumberResult where
  mantissa : UInt64
  exponent : Int64
  status : UInt8
  negative : UInt8

def decodeMode (m : UInt8) : rounding_mode := match m.toNat with
  | 0 => .to_nearest | 1 => .towards_zero | 2 => .downward | _ => .upward

def decodeNumber (neg : UInt8) (mant : UInt64) (exp : Int64) : Number :=
  Number.unchecked (neg != 0) mant exp.toInt

-- `Number.mantissa`/`Number.exponent` apply the C++ transformation
def encodeNumber (n : Number) : FFINumberResult :=
  ⟨n.mantissa.toInt.natAbs.toUInt64, n.exponent.toInt64, 0, if n.negative_ then 1 else 0⟩

def encodeResult (r : Except String Number) : FFINumberResult :=
  match r with
  | .ok n => encodeNumber n
  | .error _ => ⟨0, 0, 1, 0⟩

end XRPL.FFI

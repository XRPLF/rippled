import XRPL.Model.Protocol.Number


namespace XRPL.FFI

open XRPL.Model.Protocol

/-- Construct a `Number` from the raw fields C++ passes over the FFI. -/
def decodeNumber (neg : UInt8) (mant : UInt64) (exp : Int64) : Number :=
  Number.unchecked (neg != 0) mant exp.toInt

end XRPL.FFI

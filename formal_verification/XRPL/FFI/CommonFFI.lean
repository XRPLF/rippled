import XRPL.Model.Protocol.Number


namespace XRPL.FFI

open XRPL.Model.Protocol

def decodeNumber (neg : UInt8) (mant : UInt64) (exp : Int64) : Number :=
  Number.unchecked (neg != 0) mant exp.toInt

end XRPL.FFI

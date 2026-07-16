import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.Number


namespace XRPL.FFI

open XRPL.Model.Protocol (Number)

/-- FFI method for `Number.operator_lt`. Each number has 3 fields: mantissa, exponent, negative
where return result is int ("1" means first number is less than second one, otherwise "0").
The C++ tests declare this symbol `extern "C"` and call it directly. -/
@[export lean_number_lt]
def lean_number_lt (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_lt (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

end XRPL.FFI

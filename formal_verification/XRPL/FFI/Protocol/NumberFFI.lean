import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.Number


namespace XRPL.FFI

open XRPL.Model.Protocol (Number)

@[export lean_number_lt]
def lean_number_lt (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_lt (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

end XRPL.FFI

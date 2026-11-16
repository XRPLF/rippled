import XRPL.Model.Protocol.IntAmount

namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_int_amount_signum]
def lean_int_amount_signum (value : Int64) : Int64 :=
  (IntAmount.signum { value }).toInt64

end XRPL.FFI

import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.XRPAmount


namespace XRPL.FFI

open XRPL.Model.Protocol (XRPAmount)

@[export lean_xrp_of_int64]
def lean_xrp_of_int64 (drops : Int64) : Int64 :=
  (XRPAmount.ofInt64 drops).value

@[export lean_xrp_of_number]
def lean_xrp_of_number (neg : UInt8) (mant : UInt64) (exp : Int64) (mode : UInt8) : FFIXRPResult :=
  encodeXRPResult (XRPAmount.ofNumber (decodeNumber neg mant exp) (decodeMode mode))

@[export lean_xrp_to_number]
def lean_xrp_to_number (v : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult ((decodeXRP v).toNumber (decodeMode mode))

@[export lean_xrp_eq]
def lean_xrp_eq (v1 v2 : Int64) : UInt8 :=
  if XRPAmount.operator_eq (decodeXRP v1) (decodeXRP v2) then 1 else 0

@[export lean_xrp_ne]
def lean_xrp_ne (v1 v2 : Int64) : UInt8 :=
  if XRPAmount.operator_ne (decodeXRP v1) (decodeXRP v2) then 1 else 0

@[export lean_xrp_eq_int]
def lean_xrp_eq_int (v : Int64) (n : Int64) : UInt8 :=
  if XRPAmount.operator_eq_int (decodeXRP v) n then 1 else 0

@[export lean_xrp_ne_int]
def lean_xrp_ne_int (v : Int64) (n : Int64) : UInt8 :=
  if XRPAmount.operator_ne_int (decodeXRP v) n then 1 else 0

@[export lean_xrp_lt]
def lean_xrp_lt (v1 v2 : Int64) : UInt8 :=
  if XRPAmount.operator_lt (decodeXRP v1) (decodeXRP v2) then 1 else 0

@[export lean_xrp_le]
def lean_xrp_le (v1 v2 : Int64) : UInt8 :=
  if XRPAmount.operator_le (decodeXRP v1) (decodeXRP v2) then 1 else 0

@[export lean_xrp_gt]
def lean_xrp_gt (v1 v2 : Int64) : UInt8 :=
  if XRPAmount.operator_gt (decodeXRP v1) (decodeXRP v2) then 1 else 0

@[export lean_xrp_ge]
def lean_xrp_ge (v1 v2 : Int64) : UInt8 :=
  if XRPAmount.operator_ge (decodeXRP v1) (decodeXRP v2) then 1 else 0

@[export lean_xrp_add]
def lean_xrp_add (v1 v2 : Int64) : Int64 :=
  ((decodeXRP v1).operator_add (decodeXRP v2)).value

@[export lean_xrp_sub]
def lean_xrp_sub (v1 v2 : Int64) : Int64 :=
  ((decodeXRP v1).operator_sub (decodeXRP v2)).value

@[export lean_xrp_neg]
def lean_xrp_neg (v : Int64) : Int64 :=
  (decodeXRP v).operator_neg.value

@[export lean_xrp_mul]
def lean_xrp_mul (v : Int64) (rhs : Int64) : Int64 :=
  ((decodeXRP v).operator_mul rhs).value

@[export lean_xrp_add_int]
def lean_xrp_add_int (v : Int64) (n : Int64) : Int64 :=
  ((decodeXRP v).operator_add_int n).value

@[export lean_xrp_sub_int]
def lean_xrp_sub_int (v : Int64) (n : Int64) : Int64 :=
  ((decodeXRP v).operator_sub_int n).value

@[export lean_xrp_mul_ratio]
def lean_xrp_mul_ratio (v : Int64) (num den : UInt32) (roundUp : UInt8) : FFIXRPResult :=
  encodeXRPResult (XRPAmount.mulRatio (decodeXRP v) num den (roundUp != 0))

end XRPL.FFI

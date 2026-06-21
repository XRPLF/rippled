import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.MPTAmount


namespace XRPL.FFI

open XRPL.Model.Protocol (MPTAmount)

@[export lean_mpt_of_int64]
def lean_mpt_of_int64 (v : Int64) : Int64 :=
  (MPTAmount.ofInt64 v).value

@[export lean_mpt_of_number]
def lean_mpt_of_number (neg : UInt8) (mant : UInt64) (exp : Int64) (mode : UInt8) : FFIMPTResult :=
  encodeMPTResult (MPTAmount.ofNumber (decodeNumber neg mant exp) (decodeMode mode))

@[export lean_mpt_to_number]
def lean_mpt_to_number (v : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult ((decodeMPT v).toNumber (decodeMode mode))

@[export lean_mpt_eq]
def lean_mpt_eq (v1 v2 : Int64) : UInt8 :=
  if MPTAmount.operator_eq (decodeMPT v1) (decodeMPT v2) then 1 else 0

@[export lean_mpt_ne]
def lean_mpt_ne (v1 v2 : Int64) : UInt8 :=
  if MPTAmount.operator_ne (decodeMPT v1) (decodeMPT v2) then 1 else 0

@[export lean_mpt_eq_int]
def lean_mpt_eq_int (v : Int64) (n : Int64) : UInt8 :=
  if MPTAmount.operator_eq_int (decodeMPT v) n then 1 else 0

@[export lean_mpt_ne_int]
def lean_mpt_ne_int (v : Int64) (n : Int64) : UInt8 :=
  if MPTAmount.operator_ne_int (decodeMPT v) n then 1 else 0

@[export lean_mpt_lt]
def lean_mpt_lt (v1 v2 : Int64) : UInt8 :=
  if MPTAmount.operator_lt (decodeMPT v1) (decodeMPT v2) then 1 else 0

@[export lean_mpt_le]
def lean_mpt_le (v1 v2 : Int64) : UInt8 :=
  if MPTAmount.operator_le (decodeMPT v1) (decodeMPT v2) then 1 else 0

@[export lean_mpt_gt]
def lean_mpt_gt (v1 v2 : Int64) : UInt8 :=
  if MPTAmount.operator_gt (decodeMPT v1) (decodeMPT v2) then 1 else 0

@[export lean_mpt_ge]
def lean_mpt_ge (v1 v2 : Int64) : UInt8 :=
  if MPTAmount.operator_ge (decodeMPT v1) (decodeMPT v2) then 1 else 0

@[export lean_mpt_add]
def lean_mpt_add (v1 v2 : Int64) : Int64 :=
  ((decodeMPT v1).operator_add (decodeMPT v2)).value

@[export lean_mpt_sub]
def lean_mpt_sub (v1 v2 : Int64) : Int64 :=
  ((decodeMPT v1).operator_sub (decodeMPT v2)).value

@[export lean_mpt_neg]
def lean_mpt_neg (v : Int64) : Int64 :=
  (decodeMPT v).operator_neg.value

@[export lean_mpt_add_int]
def lean_mpt_add_int (v : Int64) (n : Int64) : Int64 :=
  ((decodeMPT v).operator_add_int n).value

@[export lean_mpt_sub_int]
def lean_mpt_sub_int (v : Int64) (n : Int64) : Int64 :=
  ((decodeMPT v).operator_sub_int n).value

@[export lean_mpt_mul_ratio]
def lean_mpt_mul_ratio (v : Int64) (num den : UInt32) (roundUp : UInt8) : FFIMPTResult :=
  encodeMPTResult (MPTAmount.mulRatio (decodeMPT v) num den (roundUp != 0))

end XRPL.FFI

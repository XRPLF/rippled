import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.IOUAmount


namespace XRPL.FFI

open XRPL.Model.Protocol (IOUAmount)

@[export lean_iou_amount_build]
def lean_iou_amount_build (mantissa exponent : Int64) : IOUAmount :=
  { mantissa_ := mantissa, exponent_ := exponent.toInt }
@[export lean_iou_amount_mantissa]
def lean_iou_amount_mantissa (a : IOUAmount) : Int64 := a.mantissa
@[export lean_iou_amount_exponent]
def lean_iou_amount_exponent (a : IOUAmount) : Int64 := a.exponent.toInt64

@[export lean_iou_from_number]
def lean_iou_from_number (neg : UInt8) (mant : UInt64) (exp : Int64) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult (IOUAmount.fromNumber (decodeNumber neg mant exp) (decodeMode mode))

@[export lean_iou_of_mantissa_exp]
def lean_iou_of_mantissa_exp (m : Int64) (e : Int64) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult (IOUAmount.ofMantissaExp m e.toInt (decodeMode mode))

@[export lean_iou_of_number]
def lean_iou_of_number (neg : UInt8) (mant : UInt64) (exp : Int64) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult (IOUAmount.ofNumber (decodeNumber neg mant exp) (decodeMode mode))

@[export lean_iou_to_number]
def lean_iou_to_number (m : Int64) (e : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult ((decodeIOU m e).toNumber (decodeMode mode))

@[export lean_iou_eq]
def lean_iou_eq (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) : UInt8 :=
  if IOUAmount.operator_eq (decodeIOU m1 e1) (decodeIOU m2 e2) then 1 else 0

@[export lean_iou_ne]
def lean_iou_ne (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) : UInt8 :=
  if IOUAmount.operator_ne (decodeIOU m1 e1) (decodeIOU m2 e2) then 1 else 0

@[export lean_iou_lt]
def lean_iou_lt (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) (mode : UInt8) : FFIBoolResult :=
  encodeBoolResult (IOUAmount.operator_lt (decodeIOU m1 e1) (decodeIOU m2 e2) (decodeMode mode))

@[export lean_iou_le]
def lean_iou_le (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) (mode : UInt8) : FFIBoolResult :=
  encodeBoolResult (IOUAmount.operator_le (decodeIOU m1 e1) (decodeIOU m2 e2) (decodeMode mode))

@[export lean_iou_gt]
def lean_iou_gt (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) (mode : UInt8) : FFIBoolResult :=
  encodeBoolResult (IOUAmount.operator_gt (decodeIOU m1 e1) (decodeIOU m2 e2) (decodeMode mode))

@[export lean_iou_ge]
def lean_iou_ge (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) (mode : UInt8) : FFIBoolResult :=
  encodeBoolResult (IOUAmount.operator_ge (decodeIOU m1 e1) (decodeIOU m2 e2) (decodeMode mode))

@[export lean_iou_neg]
def lean_iou_neg (m : Int64) (e : Int64) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult ((decodeIOU m e).operator_neg (decodeMode mode))

@[export lean_iou_add]
def lean_iou_add (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult (IOUAmount.operator_add (decodeIOU m1 e1) (decodeIOU m2 e2) (decodeMode mode))

@[export lean_iou_sub]
def lean_iou_sub (m1 : Int64) (e1 : Int64) (m2 : Int64) (e2 : Int64) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult (IOUAmount.operator_sub (decodeIOU m1 e1) (decodeIOU m2 e2) (decodeMode mode))

@[export lean_iou_mul_ratio]
def lean_iou_mul_ratio (m : Int64) (e : Int64) (num den : UInt32) (roundUp : UInt8) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult (IOUAmount.mulRatio (decodeIOU m e) num den (roundUp != 0) (decodeMode mode))

end XRPL.FFI

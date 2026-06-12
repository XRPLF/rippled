import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.Number

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol (Number largeRange)

@[export lean_number_build]
def lean_number_build (negative : UInt8) (mantissa : UInt64) (exponent : Int64) : Number :=
  Number.unchecked (negative != 0) mantissa exponent.toInt
@[export lean_number_negative]
def lean_number_negative (n : Number) : UInt8 := if n.negative_ then 1 else 0
@[export lean_number_mantissa]
def lean_number_mantissa (n : Number) : UInt64 := n.mantissa_
@[export lean_number_exponent]
def lean_number_exponent (n : Number) : Int64 := n.exponent_.toInt64

@[export lean_number_mul]
def lean_number_mul (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult (Number.operator_mul (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) (decodeMode mode))

@[export lean_number_div]
def lean_number_div (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult (Number.operator_div (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) (decodeMode mode))

@[export lean_number_add]
def lean_number_add (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult (Number.operator_add (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) (decodeMode mode))

@[export lean_number_sub]
def lean_number_sub (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult (Number.operator_sub (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) (decodeMode mode))

@[export lean_number_neg]
def lean_number_neg (neg : UInt8) (mant : UInt64) (exp : Int64) : FFINumberResult :=
  encodeNumber (decodeNumber neg mant exp).operator_neg

@[export lean_number_normalize]
def lean_number_normalize (neg : UInt8) (mant : UInt64) (exp : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult ((decodeNumber neg mant exp).normalize largeRange.min largeRange.max (decodeMode mode))

@[export lean_number_signum]
def lean_number_signum (neg : UInt8) (mant : UInt64) (exp : Int64) : Int64 :=
  (decodeNumber neg mant exp).signum.toInt64

@[export lean_number_eq]
def lean_number_eq (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_eq (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

@[export lean_number_ne]
def lean_number_ne (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_ne (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

@[export lean_number_lt]
def lean_number_lt (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_lt (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

@[export lean_number_le]
def lean_number_le (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_le (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

@[export lean_number_gt]
def lean_number_gt (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_gt (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

@[export lean_number_ge]
def lean_number_ge (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) : UInt8 :=
  if Number.operator_ge (decodeNumber neg1 mant1 exp1) (decodeNumber neg2 mant2 exp2) then 1 else 0

end XRPL.FFI

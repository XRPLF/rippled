import XRPL.Model.Protocol.Number


namespace XRPL.Model.Protocol

structure MPTAmount where
  value_ : Int64
  deriving DecidableEq, Repr

def maxMPTokenAmount : Nat := 2^63 - 1
def MPTAmount.zero : MPTAmount := { value_ := 0 }
def MPTAmount.minPositiveAmount : MPTAmount := { value_ := 1 }

def MPTAmount.value (x : MPTAmount) : Int64 := x.value_
def MPTAmount.toRat (x : MPTAmount) : ℚ := (x.value_.toInt : ℚ)
def MPTAmount.toBool (x : MPTAmount) : Bool := x.value_ != 0
def MPTAmount.signum (x : MPTAmount) : Int :=
  if x.value_ < 0 then -1 else if x.value_ > 0 then 1 else 0

def MPTAmount.ofInt64 (v : Int64) : MPTAmount := { value_ := v }

def MPTAmount.ofNumber (n : Number) (mode : rounding_mode) : Except String MPTAmount :=
  match n.to_rep mode with
  | .ok r => .ok { value_ := r }
  | .error e => .error e

def MPTAmount.toNumber (x : MPTAmount) (mode : rounding_mode) : Except String Number :=
  Number.from_rep x.value_ 0 largeRange.min largeRange.max mode

def MPTAmount.operator_eq (x y : MPTAmount) : Bool := x.value_ == y.value_
def MPTAmount.operator_ne (x y : MPTAmount) : Bool := x.value_ != y.value_
def MPTAmount.operator_eq_int (x : MPTAmount) (v : Int64) : Bool := x.value_ == v
def MPTAmount.operator_ne_int (x : MPTAmount) (v : Int64) : Bool := x.value_ != v
def MPTAmount.operator_lt (x y : MPTAmount) : Bool := x.value_ < y.value_
def MPTAmount.operator_le (x y : MPTAmount) : Bool := x.value_ ≤ y.value_
def MPTAmount.operator_gt (x y : MPTAmount) : Bool := x.value_ > y.value_
def MPTAmount.operator_ge (x y : MPTAmount) : Bool := x.value_ ≥ y.value_

def MPTAmount.operator_add (x y : MPTAmount) : MPTAmount :=
  { value_ := x.value_ + y.value_ }

def MPTAmount.operator_sub (x y : MPTAmount) : MPTAmount :=
  { value_ := x.value_ - y.value_ }

def MPTAmount.operator_neg (x : MPTAmount) : MPTAmount :=
  { value_ := -x.value_ }

def MPTAmount.operator_add_int (x : MPTAmount) (v : Int64) : MPTAmount :=
  { value_ := x.value_ + v }

def MPTAmount.operator_sub_int (x : MPTAmount) (v : Int64) : MPTAmount :=
  { value_ := x.value_ - v }

def MPTAmount.mulRatio (amt : MPTAmount) (num den : UInt32) (roundUp : Bool)
    : Except String MPTAmount :=
  if den == 0 then .error "division by zero"
  else
    let neg := amt.value_ < 0
    let d : Int := (den.toNat : Int)
    let m : Int := amt.value_.toInt * (num.toNat : Int)
    let q : Int := Int.tdiv m d
    let r : Int :=
      if Int.tmod m d ≠ 0 then
        if !neg && roundUp then q + 1
        else if neg && !roundUp then q - 1
        else q
      else q
    let int64Max : Int := Int64.maxValue.toInt
    let int64Min : Int := Int64.minValue.toInt
    if r > int64Max then .error "MPT mulRatio overflow"
    -- C++ saturates to INT64_MIN via `convert_to<int64_t>` on negative underflow.
    else if r < int64Min then .ok { value_ := Int64.minValue }
    else .ok { value_ := r.toInt64 }

end XRPL.Model.Protocol

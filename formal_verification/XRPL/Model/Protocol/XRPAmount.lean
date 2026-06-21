import XRPL.Model.Protocol.Number


namespace XRPL.Model.Protocol

structure XRPAmount where
  drops_ : Int64
  deriving DecidableEq, Repr

def kDropsPerXRP : XRPAmount := { drops_ := 1000000 }
def XRPAmount.zero : XRPAmount := { drops_ := 0 }
def XRPAmount.minPositiveAmount : XRPAmount := { drops_ := 1 }

def XRPAmount.value (x : XRPAmount) : Int64 := x.drops_
def XRPAmount.toRat (x : XRPAmount) : ℚ := (x.drops_.toInt : ℚ)
def XRPAmount.toBool (x : XRPAmount) : Bool := x.drops_ != 0
def XRPAmount.signum (x : XRPAmount) : Int :=
  if x.drops_ < 0 then -1 else if x.drops_ > 0 then 1 else 0

def XRPAmount.ofInt64 (drops : Int64) : XRPAmount := { drops_ := drops }

def XRPAmount.ofNumber (n : Number) (mode : rounding_mode) : Except String XRPAmount :=
  match n.to_rep mode with
  | .ok r => .ok { drops_ := r }
  | .error e => .error e

def XRPAmount.toNumber (x : XRPAmount) (mode : rounding_mode) : Except String Number :=
  Number.from_rep x.drops_ 0 largeRange.min largeRange.max mode

def XRPAmount.operator_eq (x y : XRPAmount) : Bool := x.drops_ == y.drops_
def XRPAmount.operator_ne (x y : XRPAmount) : Bool := x.drops_ != y.drops_
def XRPAmount.operator_eq_int (x : XRPAmount) (v : Int64) : Bool := x.drops_ == v
def XRPAmount.operator_ne_int (x : XRPAmount) (v : Int64) : Bool := x.drops_ != v
def XRPAmount.operator_lt (x y : XRPAmount) : Bool := x.drops_ < y.drops_
def XRPAmount.operator_le (x y : XRPAmount) : Bool := x.drops_ ≤ y.drops_
def XRPAmount.operator_gt (x y : XRPAmount) : Bool := x.drops_ > y.drops_
def XRPAmount.operator_ge (x y : XRPAmount) : Bool := x.drops_ ≥ y.drops_

def XRPAmount.operator_add (x y : XRPAmount) : XRPAmount :=
  { drops_ := x.drops_ + y.drops_ }

def XRPAmount.operator_sub (x y : XRPAmount) : XRPAmount :=
  { drops_ := x.drops_ - y.drops_ }

def XRPAmount.operator_neg (x : XRPAmount) : XRPAmount :=
  { drops_ := -x.drops_ }

def XRPAmount.operator_mul (x : XRPAmount) (rhs : Int64) : XRPAmount :=
  { drops_ := x.drops_ * rhs }

def XRPAmount.operator_add_int (x : XRPAmount) (v : Int64) : XRPAmount :=
  { drops_ := x.drops_ + v }

def XRPAmount.operator_sub_int (x : XRPAmount) (v : Int64) : XRPAmount :=
  { drops_ := x.drops_ - v }

def XRPAmount.mulRatio (amt : XRPAmount) (num den : UInt32) (roundUp : Bool)
    : Except String XRPAmount :=
  if den == 0 then .error "division by zero"
  else
    let neg := amt.drops_ < 0
    let d : Int := (den.toNat : Int)
    let m : Int := amt.drops_.toInt * (num.toNat : Int)
    let q : Int := Int.tdiv m d
    let r : Int :=
      if Int.tmod m d ≠ 0 then
        if !neg && roundUp then q + 1
        else if neg && !roundUp then q - 1
        else q
      else q
    let int64Max : Int := Int64.maxValue.toInt
    let int64Min : Int := Int64.minValue.toInt
    if r > int64Max then .error "XRP mulRatio overflow"
    -- C++ saturates to INT64_MIN via `convert_to<int64_t>` on negative underflow.
    else if r < int64Min then .ok { drops_ := Int64.minValue }
    else .ok { drops_ := r.toInt64 }

end XRPL.Model.Protocol

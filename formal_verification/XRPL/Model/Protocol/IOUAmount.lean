import XRPL.Model.Protocol.Number


namespace XRPL.Model.Protocol

structure IOUAmount where
  mantissa_ : Int64
  exponent_ : Int
  deriving DecidableEq, Repr

-- floor(log10(INT64_MAX)) = 18.
def IOUAmount.kFL64 : Nat := 18
def IOUAmount.zero : IOUAmount := { mantissa_ := 0, exponent_ := -100 }
def IOUAmount.minPositiveAmount : IOUAmount :=
  { mantissa_ := cMinValue.toInt64, exponent_ := cMinOffset }

def IOUAmount.mantissa (a : IOUAmount) : Int64 := a.mantissa_
def IOUAmount.exponent (a : IOUAmount) : Int := a.exponent_

-- C++ `explicit operator bool()`
def IOUAmount.toBool (x : IOUAmount) : Bool := x.mantissa_ != 0
def IOUAmount.signum (x : IOUAmount) : Int :=
  if x.mantissa_ < 0 then -1 else if x.mantissa_ != 0 then 1 else 0

def IOUAmount.toRat (a : IOUAmount) : ℚ :=
  let sign : Int := if a.mantissa_ < 0 then -1 else 1
  let m : Int := a.mantissa_.toInt.natAbs
  if a.exponent_ ≥ 0 then
    mkRat (sign * m * (10 : Int) ^ a.exponent_.toNat) 1
  else
    mkRat (sign * m) ((10 : Nat) ^ (-a.exponent_).toNat)

def IOUAmount.fromNumber (n : Number) (mode : rounding_mode) : Except String IOUAmount :=
  match n.normalizeToRange cMinValue cMaxValue mode with
  | .error e => .error e
  | .ok (m, e) => .ok { mantissa_ := m, exponent_ := e }

def IOUAmount.normalize (a : IOUAmount) (mode : rounding_mode) : Except String IOUAmount :=
  if a.mantissa_ == 0 then .ok IOUAmount.zero
  else
    -- `Number{mantissa_, exponent_}` ctor normalizes against largeRange (throws on overflow).
    match Number.from_rep a.mantissa_ a.exponent_ largeRange.min largeRange.max mode with
    | .error e => .error e
    | .ok v =>
      match IOUAmount.fromNumber v mode with
      | .error e => .error e
      | .ok result =>
        if result.exponent_ > cMaxOffset then .error "value overflow"
        else if result.exponent_ < cMinOffset then .ok IOUAmount.zero
        else .ok result

-- C++ `IOUAmount(mantissa_type, exponent_type)`
def IOUAmount.ofMantissaExp (m : Int64) (e : Int) (mode : rounding_mode) : Except String IOUAmount :=
  IOUAmount.normalize ⟨m, e⟩ mode

-- C++ `explicit IOUAmount(Number const&)`
def IOUAmount.ofNumber (n : Number) (mode : rounding_mode) : Except String IOUAmount :=
  match IOUAmount.fromNumber n mode with
  | .error e => .error e
  | .ok r =>
    if r.exponent_ > cMaxOffset then .error "value overflow"
    else if r.exponent_ < cMinOffset then .ok IOUAmount.zero
    else .ok r

-- C++ `operator Number()` = `Number{mantissa_, exponent_}`
def IOUAmount.toNumber (a : IOUAmount) (mode : rounding_mode) : Except String Number :=
  Number.from_rep a.mantissa_ a.exponent_ largeRange.min largeRange.max mode

def IOUAmount.operator_eq (x y : IOUAmount) : Bool :=
  x.exponent_ == y.exponent_ && x.mantissa_ == y.mantissa_

-- C++ `operator!=` (boost::totally_ordered).
def IOUAmount.operator_ne (x y : IOUAmount) : Bool :=
  !x.operator_eq y

-- C++ `operator<(other)` = `Number{*this} < Number{other}`.
def IOUAmount.operator_lt (x y : IOUAmount) (mode : rounding_mode)
    : Except String Bool :=
  match x.toNumber mode with
  | .error e => .error e
  | .ok xn =>
    match y.toNumber mode with
    | .error e => .error e
    | .ok yn => .ok (Number.operator_lt xn yn)

-- boost::totally_ordered<IOUAmount>: operator<= := !(y < x)
def IOUAmount.operator_le (x y : IOUAmount) (mode : rounding_mode)
    : Except String Bool :=
  match IOUAmount.operator_lt y x mode with
  | .error e => .error e
  | .ok lt => .ok (!lt)

def IOUAmount.operator_gt (x y : IOUAmount) (mode : rounding_mode)
    : Except String Bool :=
  IOUAmount.operator_lt y x mode

def IOUAmount.operator_ge (x y : IOUAmount) (mode : rounding_mode)
    : Except String Bool :=
  match IOUAmount.operator_lt x y mode with
  | .error e => .error e
  | .ok lt => .ok (!lt)

-- Unary negation invokes the normalizing `IOUAmount(int64, int)` ctor.
def IOUAmount.operator_neg (x : IOUAmount) (mode : rounding_mode) : Except String IOUAmount :=
  IOUAmount.normalize ⟨-x.mantissa_, x.exponent_⟩ mode

def IOUAmount.operator_add (x y : IOUAmount) (mode : rounding_mode)
    : Except String IOUAmount :=
  if y.mantissa_ == 0 then .ok x
  else if x.mantissa_ == 0 then .ok y
  else
    match x.toNumber mode with
    | .error e => .error e
    | .ok xn =>
      match y.toNumber mode with
      | .error e => .error e
      | .ok yn =>
        match Number.operator_add xn yn mode with
        | .error e => .error e
        | .ok sum => IOUAmount.ofNumber sum mode

-- C++ `operator-=(other)` = `*this += -other`.
def IOUAmount.operator_sub (x y : IOUAmount) (mode : rounding_mode)
    : Except String IOUAmount :=
  match y.operator_neg mode with
  | .error e => .error e
  | .ok ny => IOUAmount.operator_add x ny mode

def IOUAmount.mulRatio (amt : IOUAmount) (num den : UInt32) (roundUp : Bool)
    (mode : rounding_mode) : Except String IOUAmount :=
  if den == 0 then .error "division by zero"
  else
    let neg := amt.mantissa_ < 0
    let denN : Nat := den.toNat
    let absM : Nat := amt.mantissa_.toInt.natAbs
    let mul : Nat := absM * num.toNat
    let low0 : Nat := mul / denN
    let rem0 : Nat := mul % denN
    let exp0 : Int := amt.exponent_
    let (low1, rem1, exp1) : Nat × Nat × Int :=
      if rem0 ≠ 0 then
        let roomToGrow : Int := (IOUAmount.kFL64 : Int) - (Nat.clog 10 low0 : Int)
        let (lo, rm, ex) : Nat × Nat × Int :=
          if roomToGrow > 0 then
            let p : Nat := 10 ^ roomToGrow.toNat
            (low0 * p, rem0 * p, exp0 - roomToGrow)
          else (low0, rem0, exp0)
        let addRem : Nat := rm / denN
        (lo + addRem, rm - addRem * denN, ex)
      else (low0, rem0, exp0)
    let mustShrink : Int := (Nat.clog 10 low1 : Int) - IOUAmount.kFL64
    let (low2, hasRem, exp2) : Nat × Bool × Int :=
      if mustShrink > 0 then
        let p : Nat := 10 ^ mustShrink.toNat
        let lo : Nat := low1 / p
        let newHasRem : Bool := (rem1 ≠ 0) || (low1 - lo * p ≠ 0)
        (lo, newHasRem, exp1 + mustShrink)
      else (low1, rem1 ≠ 0, exp1)
    let mantissa0 : Int64 := (low2 : Int).toInt64
    let mantissa : Int64 := if neg then -mantissa0 else mantissa0
    match IOUAmount.ofMantissaExp mantissa exp2 mode with
    | .error e => .error e
    | .ok result =>
      if hasRem && roundUp && !neg then
        if result.mantissa_ == 0 then .ok IOUAmount.minPositiveAmount
        else IOUAmount.ofMantissaExp (result.mantissa_ + 1) result.exponent_ mode
      else if hasRem && !roundUp && neg then
        if result.mantissa_ == 0 then
          .ok { mantissa_ := -cMinValue.toInt64, exponent_ := cMinOffset }
        else IOUAmount.ofMantissaExp (result.mantissa_ - 1) result.exponent_ mode
      else .ok result

end XRPL.Model.Protocol

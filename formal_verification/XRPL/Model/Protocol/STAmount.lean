import XRPL.Model.Protocol.Asset
import XRPL.Model.Protocol.IOUAmount
import XRPL.Model.Protocol.MPTAmount
import XRPL.Model.Protocol.Number
import XRPL.Model.Protocol.XRPAmount

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

def kMinOffset : Int := -96
def kMaxOffset : Int := 80
def kMinValue : UInt64 := 1000000000000000
def kMaxValue : UInt64 := 9999999999999999
def kMaxNativeN : Nat := 10^17
def kTen_TO17 : UInt64 := 100000000000000000
def kTen_TO14 : UInt64 := 100000000000000
def kTen_TO14M1 : UInt64 := 99999999999999

def int64Max : Int := Int64.maxValue.toInt
def int64Min : Int := Int64.minValue.toInt

structure STAmount where
  mAsset : Asset
  mValue : UInt64
  mOffset : Int
  mIsNegative : Bool
  deriving DecidableEq, Repr

def STAmount.iouOne : IOUAmount := { mantissa_ := 1, exponent_ := 0 }
def STAmount.iouMaxLoss : IOUAmount := { mantissa_ := 1, exponent_ := -4 }

def STAmount.exponent (s : STAmount) : Int := s.mOffset
def STAmount.mantissa (s : STAmount) : UInt64 := s.mValue
def STAmount.integral (s : STAmount) : Bool := s.mAsset.integral
def STAmount.native (s : STAmount) : Bool := s.mAsset.isNative
def STAmount.negative (s : STAmount) : Bool := s.mIsNegative
def STAmount.asset (s : STAmount) : Asset := s.mAsset
def STAmount.getIssuer (s : STAmount) : AccountID := s.mAsset.getIssuer
def STAmount.holdsIssue (s : STAmount) : Bool := s.mAsset.holdsIssue
def STAmount.holdsMPTIssue (s : STAmount) : Bool := s.mAsset.holdsMPTIssue
def STAmount.isZero (s : STAmount) : Bool := s.mValue == 0

def STAmount.signum (s : STAmount) : Int :=
  if s.mValue == 0 then 0 else if s.mIsNegative then -1 else 1

-- Signed-int64 view of an integral STAmount (`mOffset == 0`).
def STAmount.signedDrops (s : STAmount) : Int :=
  if s.mIsNegative then -(s.mValue.toNat : Int) else (s.mValue.toNat : Int)

def STAmount.isLegalNet (s : STAmount) : Bool :=
  !s.native || s.mValue.toNat ≤ kMaxNativeN

def STAmount.areComparable (lhs rhs : STAmount) : Bool :=
  Asset.areComparable lhs.mAsset rhs.mAsset

-- C++ STAmount::clear(Asset): zero of the given asset; offset -100 for IOU so that
-- zero sorts below small positive values with negative exponents
def STAmount.clear (asset : Asset) : STAmount :=
  { mAsset := asset
  , mValue := 0
  , mOffset := if asset.integral then 0 else -100
  , mIsNegative := false }

def STAmount.zeroed (s : STAmount) : STAmount := STAmount.clear s.mAsset

def STAmount.toRat (s : STAmount) : ℚ :=
  let sign : Int := if s.mIsNegative then -1 else 1
  let m : Int := s.mValue.toNat
  if s.mOffset ≥ 0 then
    mkRat (sign * m * (10 : Int) ^ s.mOffset.toNat) 1
  else
    mkRat (sign * m) ((10 : Nat) ^ (-s.mOffset).toNat)

def STAmount.xrp (s : STAmount) : Except String XRPAmount :=
  if s.native then .ok { drops_ := s.signedDrops.toInt64 }
  else .error "Cannot return non-native STAmount as XRPAmount"

def STAmount.iou (s : STAmount) (mode : rounding_mode) : Except String IOUAmount :=
  if s.integral then
    .error "Cannot return non-IOU STAmount as IOUAmount"
  else IOUAmount.ofMantissaExp s.signedDrops.toInt64 s.mOffset mode

def STAmount.mpt (s : STAmount) : Except String MPTAmount :=
  if s.holdsMPTIssue then .ok { value_ := s.signedDrops.toInt64 }
  else .error "Cannot return STAmount as MPTAmount"

def STAmount.toNumber (s : STAmount) (mode : rounding_mode) : Except String Number :=
  match s.mAsset with
  | .issue iss =>
    if iss.isXRP then
      match s.xrp with
      | .error e => .error e
      | .ok x => x.toNumber mode
    else
      match s.iou mode with
      | .error e => .error e
      | .ok i => i.toNumber mode
  | .mptIssue _ =>
    match s.mpt with
    | .error e => .error e
    | .ok m => m.toNumber mode

private def STAmount.canonicalize (s : STAmount) (mode : rounding_mode) : Except String STAmount :=
  if s.integral then
    -- XRP or MPT
    if s.mValue == 0 || s.mOffset ≤ -20 then
      .ok { s with mValue := 0, mOffset := 0, mIsNegative := false }
    else if s.native && s.mOffset > 17 then
      .error "Native currency amount out of range"
    else if !s.native && s.mOffset > 18 then
      .error "MPT amount out of range"
    else
      let num : Number := Number.unchecked s.mIsNegative s.mValue s.mOffset
      if s.native then
        match XRPAmount.ofNumber num mode with
        | .error e => .error e
        | .ok xrp =>
          let value : Int64 := xrp.value
          let absVal : Nat := value.toInt.natAbs
          if absVal > kMaxNativeN then .error "Native currency amount out of range"
          else .ok { s with mValue := absVal.toUInt64, mOffset := 0, mIsNegative := value < 0 }
      else
        match MPTAmount.ofNumber num mode with
        | .error e => .error e
        | .ok mpt =>
          let value : Int64 := mpt.value_
          let absVal : Nat := value.toInt.natAbs
          if absVal > maxMPTokenAmount then .error "MPT amount out of range"
          else .ok { s with mValue := absVal.toUInt64, mOffset := 0, mIsNegative :=  value < 0 }
  else
    match s.iou mode with
    | .error e => .error e
    | .ok i =>
      let neg := i.signum < 0
      let absMant : Int64 := if neg then -i.mantissa_ else i.mantissa_
      .ok { s with mValue := absMant.toUInt64
                 , mOffset := i.exponent_
                 , mIsNegative := neg }

def STAmount.unchecked (mAsset : Asset) (mValue : UInt64) (mOffset : Int) (mIsNegative : Bool) : STAmount :=
  { mAsset, mValue, mOffset, mIsNegative }

def STAmount.uncheckedFromInt64 (asset : Asset) (v : Int64) (offset : Int) : STAmount :=
  if v < 0 then STAmount.unchecked asset (-v).toUInt64 offset true
  else STAmount.unchecked asset v.toUInt64 offset false

def STAmount.checked (asset : Asset) (mValue : UInt64) (mOffset : Int) (mIsNegative : Bool)
    (mode : rounding_mode) : Except String STAmount :=
  let n : STAmount := STAmount.unchecked asset mValue mOffset mIsNegative
  n.canonicalize mode

-- C++ `STAmount(Asset)`: a zero amount, canonicalized. A zero never rounds or overflows,
-- so the mode is irrelevant and the error branch is unreachable.
def STAmount.ofAsset (asset : Asset) : STAmount :=
  match STAmount.checked asset 0 0 false rounding_mode.to_nearest with
  | .ok s => s
  | .error _ => STAmount.unchecked asset 0 0 false

def STAmount.ofInt64 (asset : Asset) (mantissa : Int64) (exponent : Int)
    (mode : rounding_mode) : Except String STAmount :=
  let n : STAmount := STAmount.uncheckedFromInt64 asset mantissa exponent
  n.canonicalize mode

def STAmount.ofNativeInt64 (drops : Int64) : STAmount :=
  STAmount.uncheckedFromInt64 xrpAsset drops 0

def STAmount.ofIOUAmount (a : IOUAmount) (issue : Issue) (mode : rounding_mode) : Except String STAmount :=
  let neg := a.signum < 0
  let absMant : Int64 := if neg then -a.mantissa_ else a.mantissa_
  let n : STAmount := STAmount.unchecked (.issue issue) absMant.toUInt64 a.exponent_ neg
  n.canonicalize mode

def STAmount.ofXRPAmount (a : XRPAmount) (mode : rounding_mode) : Except String STAmount :=
  let neg := a.signum < 0
  let absDrops : Int64 := if neg then -a.value else a.value
  let n : STAmount := STAmount.unchecked xrpAsset absDrops.toUInt64 0 neg
  n.canonicalize mode

def STAmount.ofMPTAmount (a : MPTAmount) (issue : MPTIssue) (mode : rounding_mode) : Except String STAmount :=
  let neg := a.signum < 0
  let absVal : Int64 := if neg then -a.value else a.value
  let n : STAmount := STAmount.unchecked (.mptIssue issue) absVal.toUInt64 0 neg
  n.canonicalize mode

def STAmount.ofNumber (asset : Asset) (n : Number) (mode : rounding_mode)
    : Except String STAmount :=
  let neg : Bool := n.signum < 0
  let working : Number := if neg then n.operator_neg else n
  if asset.integral then
    match working.to_rep mode with
    | .error e => .error e
    | .ok intValue =>
      STAmount.checked asset intValue.toUInt64 0 neg mode
  else
    match working.normalizeToRange kMinValue kMaxValue mode with
    | .error e => .error e
    | .ok (mantissa, exponent) =>
      STAmount.checked asset mantissa.toUInt64 exponent neg mode

def STAmount.operator_eq (lhs rhs : STAmount) : Bool :=
  STAmount.areComparable lhs rhs &&
    lhs.mIsNegative == rhs.mIsNegative &&
    lhs.mOffset == rhs.mOffset &&
    lhs.mValue == rhs.mValue &&
    lhs.mAsset == rhs.mAsset

def STAmount.operator_ne (lhs rhs : STAmount) : Bool :=
  !lhs.operator_eq rhs

def STAmount.operator_lt (lhs rhs : STAmount) : Except String Bool :=
  if !STAmount.areComparable lhs rhs then
    .error "Can't compare amounts that are't comparable!"
  else
    .ok <|
      if lhs.mIsNegative != rhs.mIsNegative then lhs.mIsNegative
      else if lhs.mValue == 0 then
        if rhs.mIsNegative then false else rhs.mValue != 0
      else if rhs.mValue == 0 then false
      else if lhs.mOffset > rhs.mOffset then lhs.mIsNegative
      else if lhs.mOffset < rhs.mOffset then !lhs.mIsNegative
      else if lhs.mValue > rhs.mValue then lhs.mIsNegative
      else if lhs.mValue < rhs.mValue then !lhs.mIsNegative
      else false

def STAmount.operator_le (lhs rhs : STAmount) : Except String Bool :=
  match STAmount.operator_lt rhs lhs with
  | .error e => .error e
  | .ok lt => .ok (!lt)

def STAmount.operator_gt (lhs rhs : STAmount) : Except String Bool :=
  STAmount.operator_lt rhs lhs

def STAmount.operator_ge (lhs rhs : STAmount) : Except String Bool :=
  match STAmount.operator_lt lhs rhs with
  | .error e => .error e
  | .ok lt => .ok (!lt)

def STAmount.operator_neg (s : STAmount) : STAmount :=
  if s.mValue == 0 then s else { s with mIsNegative := !s.mIsNegative }

def STAmount.operator_add (v1 v2 : STAmount) (mode : rounding_mode)
    : Except String STAmount :=
  if !STAmount.areComparable v1 v2 then
    .error "Can't add amounts that are't comparable!"
  else if v2.mValue == 0 then .ok v1
  else if v1.mValue == 0 then
    STAmount.checked v1.mAsset v2.mValue v2.mOffset v2.mIsNegative mode
  else
    match v1.mAsset with
    | .issue iss =>
      if iss.isXRP then
        let sumDrops : Int64 := v1.signedDrops.toInt64 + v2.signedDrops.toInt64
        .ok (STAmount.ofNativeInt64 sumDrops)
      else
        match v1.iou mode with
        | .error e => .error e
        | .ok i1 =>
          match v2.iou mode with
          | .error e => .error e
          | .ok i2 =>
            match IOUAmount.operator_add i1 i2 mode with
            | .error e => .error e
            | .ok sumI => STAmount.ofIOUAmount sumI iss mode
    | .mptIssue _ =>
      let sumDrops : Int64 := v1.signedDrops.toInt64 + v2.signedDrops.toInt64
      STAmount.ofInt64 v1.mAsset sumDrops 0 mode

def STAmount.operator_sub (v1 v2 : STAmount) (mode : rounding_mode)
    : Except String STAmount :=
  STAmount.operator_add v1 v2.operator_neg mode

private def STAmount.muldiv (multiplier multiplicand divisor : UInt64) : Except String UInt64 :=
  let prod : UInt128 := toUInt128 multiplier * toUInt128 multiplicand
  let q : UInt128 := prod / toUInt128 divisor
  let uint64Max : UInt128 := 2^64 - 1
  if q > uint64Max then
    .error s!"overflow: ({multiplier} * {multiplicand}) / {divisor}"
  else
    .ok (toUInt64 q)

private def STAmount.muldivRound (multiplier multiplicand divisor rounding : UInt64)
    : Except String UInt64 :=
  let prod : UInt128 := toUInt128 multiplier * toUInt128 multiplicand + toUInt128 rounding
  let q : UInt128 := prod / toUInt128 divisor
  let uint64Max : UInt128 := 2^64 - 1
  if q > uint64Max then
    .error s!"overflow: (({multiplier} * {multiplicand}) + {rounding}) / {divisor}"
  else
    .ok (toUInt64 q)

-- Caller guarantees `val > 0`; the `val ≠ 0` guard keeps the recursion total.
def STAmount.normalizeIntegralMantissa (val : UInt64) (offset : Int) : UInt64 × Int :=
  if val < kMinValue && val ≠ 0 then
    normalizeIntegralMantissa (val * 10) (offset - 1)
  else (val, offset)
termination_by kMinValue.toNat - val.toNat
decreasing_by
  rename_i h
  obtain ⟨hlt, hne⟩ := by simpa [Bool.and_eq_true, decide_eq_true_eq] using h
  have hpos : 0 < val.toNat := by
    have : val.toNat ≠ 0 := fun hz => hne (by
      apply UInt64.toNat.inj; simpa using hz)
    omega
  have hlt' : val.toNat < kMinValue.toNat := UInt64.lt_iff_toNat_lt.mp hlt
  have h10v : (10 : UInt64).toNat = 10 := rfl
  have hcmv : kMinValue.toNat = 10^15 := rfl
  have hbnd : val.toNat * (10 : UInt64).toNat < 2^64 := by
    rw [h10v]; omega
  have h10 : (val * 10).toNat = val.toNat * 10 := by
    rw [UInt64.toNat_mul, Nat.mod_eq_of_lt hbnd, h10v]
  rw [h10]; omega

private def STAmount.canonicalizeRound_integralLoop
    (value : UInt64) (offset : Int) (loops : Nat) : UInt64 × Int × Nat :=
  if offset < -1 then
    canonicalizeRound_integralLoop (value / 10) (offset + 1) (loops + 1)
  else (value, offset, loops)
termination_by (-1 - offset).toNat
decreasing_by
  rename_i h
  have : offset < -1 := h
  omega

-- Strict variant: tracks a non-zero remainder bit across the loop for `roundUp`.
private def STAmount.canonicalizeRoundStrict_integralLoop
    (value : UInt64) (offset : Int) (hadRemainder : Bool) : UInt64 × Int × Bool :=
  if offset < -1 then
    let newValue : UInt64 := value / 10
    let hadRemainder' : Bool := hadRemainder || (value != newValue * 10)
    canonicalizeRoundStrict_integralLoop newValue (offset + 1) hadRemainder'
  else (value, offset, hadRemainder)
termination_by (-1 - offset).toNat
decreasing_by
  rename_i h
  have : offset < -1 := h
  omega

private def STAmount.canonicalizeRound_nonIntegralLoop
    (value : UInt64) (offset : Int) : UInt64 × Int :=
  if value > 10 * kMaxValue then
    canonicalizeRound_nonIntegralLoop (value / 10) (offset + 1)
  else (value, offset)
termination_by value.toNat
decreasing_by
  rename_i h
  have hgt : value > 10 * kMaxValue := h
  have hgt' : value.toNat > (10 * kMaxValue).toNat := UInt64.lt_iff_toNat_lt.mp hgt
  have hbnd : (10 * kMaxValue).toNat ≥ 10 := by decide
  have h10 : (10 : UInt64).toNat = 10 := rfl
  have hdiv : (value / 10).toNat = value.toNat / 10 := by
    rw [UInt64.toNat_div, h10]
  rw [hdiv]; omega

-- Legacy XRPL rounding (pre-2022). `_roundUp` is unused on this path; the
-- integral addend is `loops≥2 ? 9 : 10`, which gives drops-conversion the
-- documented "≥0.1 rounds up, <0.1 rounds down" behavior.
def STAmount.canonicalizeRound (integral : Bool) (value : UInt64) (offset : Int) (_roundUp : Bool)
    : UInt64 × Int :=
  if integral then
    if offset < 0 then
      let (v0, o0, loops) := STAmount.canonicalizeRound_integralLoop value offset 0
      let addend : UInt64 := if loops ≥ 2 then 9 else 10
      let v1 : UInt64 := v0 + addend
      (v1 / 10, o0 + 1)
    else (value, offset)
  else if value > kMaxValue then
    let (v0, o0) := STAmount.canonicalizeRound_nonIntegralLoop value offset
    let v1 : UInt64 := v0 + 9
    (v1 / 10, o0 + 1)
  else (value, offset)

-- Strict variant: tracks remainder bits and uses `roundUp` to choose 9 vs 10.
def STAmount.canonicalizeRoundStrict (integral : Bool) (value : UInt64) (offset : Int) (roundUp : Bool)
    : UInt64 × Int :=
  if integral then
    if offset < 0 then
      let (v0, o0, hadRem) := STAmount.canonicalizeRoundStrict_integralLoop value offset false
      let addend : UInt64 := if hadRem && roundUp then 10 else 9
      let v1 : UInt64 := v0 + addend
      (v1 / 10, o0 + 1)
    else (value, offset)
  else if value > kMaxValue then
    let (v0, o0) := STAmount.canonicalizeRound_nonIntegralLoop value offset
    let v1 : UInt64 := v0 + 9
    (v1 / 10, o0 + 1)
  else (value, offset)

def STAmount.divide (num den : STAmount) (asset : Asset) (mode : rounding_mode)
    : Except String STAmount :=
  if den.isZero then .error "division by zero"
  else if num.isZero then STAmount.checked asset 0 0 false mode
  else
    let (numVal, numOffset) :=
      if num.integral then
        STAmount.normalizeIntegralMantissa num.mantissa num.exponent
      else (num.mantissa, num.exponent)
    let (denVal, denOffset) :=
      if den.integral then
        STAmount.normalizeIntegralMantissa den.mantissa den.exponent
      else (den.mantissa, den.exponent)
    match STAmount.muldiv numVal kTen_TO17 denVal with
    | .error e => .error e
    | .ok q =>
      let mant : UInt64 := q + 5
      let exp : Int := numOffset - denOffset - 17
      let neg : Bool := num.negative != den.negative
      STAmount.checked asset mant exp neg mode

def STAmount.multiply (v1 v2 : STAmount) (asset : Asset) (mode : rounding_mode)
    : Except String STAmount :=
  if v1.isZero || v2.isZero then
    STAmount.checked asset 0 0 false mode
  else if v1.native && v2.native && asset.isNative then
    let aS : Int64 := v1.signedDrops.toInt64
    let bS : Int64 := v2.signedDrops.toInt64
    let minV : UInt64 := (if aS ≤ bS then aS else bS).toUInt64
    let maxV : UInt64 := (if aS ≤ bS then bS else aS).toUInt64
    if minV > 3000000000 then  -- sqrt(cMaxNative)
      .error "Native value overflow"
    else if (maxV >>> 32) * minV > 2095475792 then  -- cMaxNative / 2^32
      .error "Native value overflow"
    else
      -- C++ `STAmount(SField, uint64_t)` does NOT canonicalize.
      let prod : UInt64 := minV * maxV
      .ok (STAmount.unchecked xrpAsset prod 0 false)
  else if v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue then
    let aS : Int64 := v1.signedDrops.toInt64
    let bS : Int64 := v2.signedDrops.toInt64
    let minV : UInt64 := (if aS ≤ bS then aS else bS).toUInt64
    let maxV : UInt64 := (if aS ≤ bS then bS else aS).toUInt64
    if minV > 3037000499 then  -- sqrt(maxMPTokenAmount) ~ 3037000499.98
      .error "MPT value overflow"
    else if (maxV >>> 32) * minV > 2147483648 then  -- maxMPTokenAmount / 2^32
      .error "MPT value overflow"
    else
      -- C++ `STAmount(Asset, uint64_t)` canonicalizes
      let prod : UInt64 := minV * maxV
      STAmount.checked asset prod 0 false mode
  else
    match v1.toNumber mode with
    | .error e => .error e
    | .ok n1 =>
      match v2.toNumber mode with
      | .error e => .error e
      | .ok n2 =>
        match Number.operator_mul n1 n2 mode with
        | .error e => .error e
        | .ok r => STAmount.ofNumber asset r mode

-- `canonFn` selects legacy vs strict canonicalization
def STAmount.mulRoundImpl
    (v1 v2 : STAmount) (asset : Asset) (roundUp : Bool) (mode : rounding_mode)
    (canonFn : Bool → UInt64 → Int → Bool → UInt64 × Int)
    (saveRound : Bool)
    : Except String STAmount :=
  if v1.isZero || v2.isZero then
    STAmount.checked asset 0 0 false mode
  else if v1.native && v2.native && asset.isNative then
    let aS : Int64 := v1.signedDrops.toInt64
    let bS : Int64 := v2.signedDrops.toInt64
    let minV : UInt64 := (if aS ≤ bS then aS else bS).toUInt64
    let maxV : UInt64 := (if aS ≤ bS then bS else aS).toUInt64
    if minV > 3000000000 then
      .error "Native value overflow"
    else if (maxV >>> 32) * minV > 2095475792 then
      .error "Native value overflow"
    else
      let prod : UInt64 := minV * maxV
      .ok (STAmount.unchecked xrpAsset prod 0 false)
  else if v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue then
    let aS : Int64 := v1.signedDrops.toInt64
    let bS : Int64 := v2.signedDrops.toInt64
    let minV : UInt64 := (if aS ≤ bS then aS else bS).toUInt64
    let maxV : UInt64 := (if aS ≤ bS then bS else aS).toUInt64
    if minV > 3037000499 then
      .error "MPT value overflow"
    else if (maxV >>> 32) * minV > 2147483648 then
      .error "MPT value overflow"
    else
      -- MPT branch returns before C++'s MightSaveRound scope opens, so ambient
      -- `mode` applies regardless of `saveRound`.
      let prod : UInt64 := minV * maxV
      STAmount.checked asset prod 0 false mode
  else
    let (value1, offset1) :=
      if v1.integral then STAmount.normalizeIntegralMantissa v1.mantissa v1.exponent
      else (v1.mantissa, v1.exponent)
    let (value2, offset2) :=
      if v2.integral then STAmount.normalizeIntegralMantissa v2.mantissa v2.exponent
      else (v2.mantissa, v2.exponent)
    let resultNegative : Bool := v1.negative != v2.negative
    let roundingAddend : UInt64 := if resultNegative != roundUp then kTen_TO14M1 else 0
    match STAmount.muldivRound value1 value2 kTen_TO14 roundingAddend with
    | .error e => .error e
    | .ok amt0 =>
      let offset0 : Int := offset1 + offset2 + 14
      let (amount, offset) :=
        if resultNegative != roundUp then canonFn asset.integral amt0 offset0 roundUp
        else (amt0, offset0)
      -- C++ `MightSaveRound(TowardsZero)`: hardcoded mode, applied only when
      -- `saveRound = true` (NumberRoundModeGuard variant).
      let createMode : rounding_mode := if saveRound then .towards_zero else mode
      match STAmount.checked asset amount offset resultNegative createMode with
      | .error e => .error e
      | .ok result =>
        if roundUp && !resultNegative && result.isZero then
          if asset.integral then
            STAmount.checked asset 1 0 resultNegative mode
          else
            STAmount.checked asset kMinValue kMinOffset resultNegative mode
        else .ok result

def STAmount.mulRound (v1 v2 : STAmount) (asset : Asset) (roundUp : Bool) (mode : rounding_mode)
    : Except String STAmount :=
  STAmount.mulRoundImpl v1 v2 asset roundUp mode STAmount.canonicalizeRound (saveRound := false)

def STAmount.mulRoundStrict (v1 v2 : STAmount) (asset : Asset) (roundUp : Bool) (mode : rounding_mode)
    : Except String STAmount :=
  STAmount.mulRoundImpl v1 v2 asset roundUp mode STAmount.canonicalizeRoundStrict (saveRound := true)

def STAmount.divRoundImpl
    (num den : STAmount) (asset : Asset) (roundUp : Bool) (mode : rounding_mode)
    (saveRound : Bool)
    : Except String STAmount :=
  if den.isZero then .error "division by zero"
  else if num.isZero then STAmount.checked asset 0 0 false mode
  else
    let (numVal, numOffset) :=
      if num.integral then STAmount.normalizeIntegralMantissa num.mantissa num.exponent
      else (num.mantissa, num.exponent)
    let (denVal, denOffset) :=
      if den.integral then STAmount.normalizeIntegralMantissa den.mantissa den.exponent
      else (den.mantissa, den.exponent)
    let resultNegative : Bool := num.negative != den.negative
    let roundingAddend : UInt64 := if resultNegative != roundUp then denVal - 1 else 0
    match STAmount.muldivRound numVal kTen_TO17 denVal roundingAddend with
    | .error e => .error e
    | .ok amt0 =>
      let offset0 : Int := numOffset - denOffset - 17
      let (amount, offset) :=
        if resultNegative != roundUp then
          STAmount.canonicalizeRound asset.integral amt0 offset0 roundUp
        else (amt0, offset0)
      -- C++ `MightSaveRound(roundUp ^ resultNegative ? Upward : Downward)`:
      -- hardcoded formula, applied only when `saveRound = true`.
      let createMode : rounding_mode :=
        if saveRound then (if roundUp != resultNegative then .upward else .downward)
        else mode
      match STAmount.checked asset amount offset resultNegative createMode with
      | .error e => .error e
      | .ok result =>
        if roundUp && !resultNegative && result.isZero then
          if asset.integral then
            STAmount.checked asset 1 0 resultNegative mode
          else
            STAmount.checked asset kMinValue kMinOffset resultNegative mode
        else .ok result

def STAmount.divRound (num den : STAmount) (asset : Asset) (roundUp : Bool) (mode : rounding_mode)
    : Except String STAmount :=
  STAmount.divRoundImpl num den asset roundUp mode (saveRound := false)

def STAmount.divRoundStrict (num den : STAmount) (asset : Asset) (roundUp : Bool) (mode : rounding_mode)
    : Except String STAmount :=
  STAmount.divRoundImpl num den asset roundUp mode (saveRound := true)

def STAmount.canAdd (a b : STAmount) (mode : rounding_mode) : Except String Bool :=
  if !STAmount.areComparable a b then .ok false
  else if a.isZero || b.isZero then .ok true
  else if a.native && b.native then
    match a.xrp, b.xrp with
    | .error e, _ => .error e
    | _, .error e => .error e
    | .ok aXrp, .ok bXrp =>
      let aVal : Int := aXrp.value.toInt
      let bVal : Int := bXrp.value.toInt
      .ok (!((bVal > 0 && aVal > int64Max - bVal) ||
             (bVal < 0 && aVal < int64Min - bVal)))
  else if a.holdsMPTIssue && b.holdsMPTIssue then
    match a.mpt, b.mpt with
    | .error e, _ => .error e
    | _, .error e => .error e
    | .ok aMpt, .ok bMpt =>
      let aVal : Int := aMpt.value.toInt
      let bVal : Int := bMpt.value.toInt
      .ok (!((bVal > 0 && aVal > int64Max - bVal) ||
             (bVal < 0 && aVal < int64Min - bVal)))
  else
    -- IOU precision check: |divide((a-b)+b, a) - 1| + |divide((b-a)+a, b) - 1| ≤ 1e-4.
    match STAmount.ofIOUAmount STAmount.iouOne noIssue mode with
    | .error e => .error e
    | .ok kOne =>
      match STAmount.ofIOUAmount STAmount.iouMaxLoss noIssue mode with
      | .error e => .error e
      | .ok kMaxLoss =>
        match STAmount.operator_sub a b mode with
        | .error e => .error e
        | .ok aSubB =>
          match STAmount.operator_add aSubB b mode with
          | .error e => .error e
          | .ok aSubBAddB =>
            match STAmount.divide aSubBAddB a (.issue noIssue) mode with
            | .error e => .error e
            | .ok dl =>
              match STAmount.operator_sub dl kOne mode with
              | .error e => .error e
              | .ok lhs =>
                match STAmount.operator_sub b a mode with
                | .error e => .error e
                | .ok bSubA =>
                  match STAmount.operator_add bSubA a mode with
                  | .error e => .error e
                  | .ok bSubAAddA =>
                    match STAmount.divide bSubAAddA b (.issue noIssue) mode with
                    | .error e => .error e
                    | .ok dr =>
                      match STAmount.operator_sub dr kOne mode with
                      | .error e => .error e
                      | .ok rhs =>
                        let absLhs : STAmount := if lhs.negative then lhs.operator_neg else lhs
                        let absRhs : STAmount := if rhs.negative then rhs.operator_neg else rhs
                        match STAmount.operator_add absRhs absLhs mode with
                        | .error e => .error e
                        | .ok lossSum => STAmount.operator_le lossSum kMaxLoss

def STAmount.canSubtract (a b : STAmount) : Except String Bool :=
  if !STAmount.areComparable a b then .ok false
  else if b.isZero then .ok true
  else if a.native && b.native then
    match a.xrp, b.xrp with
    | .error e, _ => .error e
    | _, .error e => .error e
    | .ok aXrp, .ok bXrp =>
      let aVal : Int := aXrp.value.toInt
      let bVal : Int := bXrp.value.toInt
      .ok (!(bVal > 0 && aVal < bVal) && !(bVal < 0 && aVal > int64Max + bVal))
  else if a.holdsMPTIssue && b.holdsMPTIssue then
    match a.mpt, b.mpt with
    | .error e, _ => .error e
    | _, .error e => .error e
    | .ok aMpt, .ok bMpt =>
      let aVal : Int := aMpt.value.toInt
      let bVal : Int := bMpt.value.toInt
      .ok (!(bVal > 0 && aVal < bVal) && !(bVal < 0 && aVal > int64Max + bVal))
  else
    -- IOU subtraction can never underflow.
    .ok true

def STAmount.roundToScale (value : STAmount) (scale : Int) (rounding : rounding_mode)
    : Except String STAmount :=
  if value.integral then .ok value
  else if value.isZero then .ok value
  else if value.exponent ≥ scale then .ok value
  else
    match STAmount.checked value.asset kMinValue scale value.negative rounding with
    | .error e => .error e
    | .ok referenceValue =>
      match STAmount.operator_add value referenceValue rounding with
      | .error e => .error e
      | .ok sum => STAmount.operator_sub sum referenceValue rounding

-- Unifies the two C++ `roundToAsset` overloads: `scale = none` is the in-place
-- 2-arg form (no IOU truncation), `scale = some s` is the 4-arg form.
def STAmount.roundToAsset (asset : Asset) (value : Number) (rounding : rounding_mode)
    (scale : Option Int := none) : Except String Number :=
  match STAmount.ofNumber asset value rounding with
  | .error e => .error e
  | .ok ret =>
    match scale with
    | none => ret.toNumber rounding
    | some s =>
      if ret.integral then ret.toNumber rounding
      else
        match STAmount.roundToScale ret s rounding with
        | .error e => .error e
        | .ok rounded => rounded.toNumber rounding

def STAmount.getRate (offerOut offerIn : STAmount) (rounding : rounding_mode) : UInt64 :=
  if offerOut.isZero then 0
  else
    match STAmount.divide offerIn offerOut (.issue noIssue) rounding with
    | .error _ => 0
    | .ok r =>
      if r.isZero then 0
      else
        let ret : UInt64 := (r.exponent + 100).toInt64.toUInt64
        (ret <<< 56) ||| r.mantissa

def kURateOne : UInt64 :=
  let one : STAmount := STAmount.unchecked xrpAsset 1 0 false
  STAmount.getRate one one .to_nearest

end XRPL.Model.Protocol

import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.STAmount


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_st_amount_build]
def lean_st_amount_build (asset : Asset) (mantissa : UInt64) (offset : Int64) (negative : UInt8) : STAmount :=
  STAmount.unchecked asset mantissa offset.toInt (negative != 0)
@[export lean_st_amount_asset]
def lean_st_amount_asset (s : STAmount) : Asset := s.asset
@[export lean_st_amount_mantissa]
def lean_st_amount_mantissa (s : STAmount) : UInt64 := s.mantissa
@[export lean_st_amount_offset]
def lean_st_amount_offset (s : STAmount) : Int64 := s.exponent.toInt64
@[export lean_st_amount_negative]
def lean_st_amount_negative (s : STAmount) : UInt8 := if s.negative then 1 else 0

@[export lean_stamount_is_legal_net]
def lean_stamount_is_legal_net (kind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) : UInt8 :=
  if (decodeSTAmount kind mValue mOffset mIsNegative).isLegalNet then 1 else 0

@[export lean_stamount_are_comparable]
def lean_stamount_are_comparable
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8) : UInt8 :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  if STAmount.areComparable a b then 1 else 0

@[export lean_stamount_xrp]
def lean_stamount_xrp (kind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) : FFIXRPResult :=
  encodeXRPResult (decodeSTAmount kind mValue mOffset mIsNegative).xrp

@[export lean_stamount_iou]
def lean_stamount_iou (kind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) (mode : UInt8) : FFIIOUResult :=
  encodeIOUResult ((decodeSTAmount kind mValue mOffset mIsNegative).iou (decodeMode mode))

@[export lean_stamount_mpt]
def lean_stamount_mpt (kind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) : FFIMPTResult :=
  encodeMPTResult (decodeSTAmount kind mValue mOffset mIsNegative).mpt

@[export lean_stamount_to_number]
def lean_stamount_to_number (kind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) (mode : UInt8) : FFINumberResult :=
  encodeResult ((decodeSTAmount kind mValue mOffset mIsNegative).toNumber (decodeMode mode))

@[export lean_stamount_unchecked_from_int64]
def lean_stamount_unchecked_from_int64 (assetKind : UInt8) (v : Int64) (offset : Int64)
    : FFISTAmountResult :=
  encodeSTAmount (STAmount.uncheckedFromInt64 (decodeAsset assetKind) v offset.toInt)

@[export lean_stamount_checked]
def lean_stamount_checked (assetKind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) (mode : UInt8) : FFISTAmountResult :=
  encodeSTAmountResult
    (STAmount.checked (decodeAsset assetKind) mValue mOffset.toInt (mIsNegative != 0) (decodeMode mode))

@[export lean_stamount_of_int64]
def lean_stamount_of_int64 (assetKind : UInt8) (mantissa : Int64) (exponent : Int64)
    (mode : UInt8) : FFISTAmountResult :=
  encodeSTAmountResult
    (STAmount.ofInt64 (decodeAsset assetKind) mantissa exponent.toInt (decodeMode mode))

@[export lean_stamount_of_native_int64]
def lean_stamount_of_native_int64 (drops : Int64) : FFISTAmountResult :=
  encodeSTAmount (STAmount.ofNativeInt64 drops)

@[export lean_stamount_of_iou_amount]
def lean_stamount_of_iou_amount (m : Int64) (e : Int64) (mode : UInt8) : FFISTAmountResult :=
  encodeSTAmountResult (STAmount.ofIOUAmount (decodeIOU m e) noIssue (decodeMode mode))

@[export lean_stamount_of_xrp_amount]
def lean_stamount_of_xrp_amount (v : Int64) (mode : UInt8) : FFISTAmountResult :=
  encodeSTAmountResult (STAmount.ofXRPAmount (decodeXRP v) (decodeMode mode))

@[export lean_stamount_of_mpt_amount]
def lean_stamount_of_mpt_amount (v : Int64) (mode : UInt8) : FFISTAmountResult :=
  encodeSTAmountResult (STAmount.ofMPTAmount (decodeMPT v) ffiMPTIssue (decodeMode mode))

@[export lean_stamount_of_number]
def lean_stamount_of_number (assetKind : UInt8) (neg : UInt8) (mant : UInt64)
    (exp : Int64) (mode : UInt8) : FFISTAmountResult :=
  encodeSTAmountResult
    (STAmount.ofNumber (decodeAsset assetKind) (decodeNumber neg mant exp) (decodeMode mode))

@[export lean_stamount_eq]
def lean_stamount_eq
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8) : UInt8 :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  if STAmount.operator_eq a b then 1 else 0

@[export lean_stamount_ne]
def lean_stamount_ne
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8) : UInt8 :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  if STAmount.operator_ne a b then 1 else 0

@[export lean_stamount_lt]
def lean_stamount_lt
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    : FFIBoolResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeBoolResult (STAmount.operator_lt a b)

@[export lean_stamount_le]
def lean_stamount_le
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    : FFIBoolResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeBoolResult (STAmount.operator_le a b)

@[export lean_stamount_gt]
def lean_stamount_gt
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    : FFIBoolResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeBoolResult (STAmount.operator_gt a b)

@[export lean_stamount_ge]
def lean_stamount_ge
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    : FFIBoolResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeBoolResult (STAmount.operator_ge a b)

@[export lean_stamount_neg]
def lean_stamount_neg (kind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) : FFISTAmountResult :=
  encodeSTAmount (decodeSTAmount kind mValue mOffset mIsNegative).operator_neg

@[export lean_stamount_add]
def lean_stamount_add
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    (mode : UInt8) : FFISTAmountResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeSTAmountResult (STAmount.operator_add a b (decodeMode mode))

@[export lean_stamount_sub]
def lean_stamount_sub
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    (mode : UInt8) : FFISTAmountResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeSTAmountResult (STAmount.operator_sub a b (decodeMode mode))

@[export lean_stamount_divide]
def lean_stamount_divide
    (kindN : UInt8) (mValueN : UInt64) (mOffsetN : Int64) (mIsNegativeN : UInt8)
    (kindD : UInt8) (mValueD : UInt64) (mOffsetD : Int64) (mIsNegativeD : UInt8)
    (assetKind : UInt8) (mode : UInt8) : FFISTAmountResult :=
  let num := decodeSTAmount kindN mValueN mOffsetN mIsNegativeN
  let den := decodeSTAmount kindD mValueD mOffsetD mIsNegativeD
  encodeSTAmountResult (STAmount.divide num den (decodeAsset assetKind) (decodeMode mode))

@[export lean_stamount_multiply]
def lean_stamount_multiply
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    (assetKind : UInt8) (mode : UInt8) : FFISTAmountResult :=
  let v1 := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let v2 := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeSTAmountResult (STAmount.multiply v1 v2 (decodeAsset assetKind) (decodeMode mode))

@[export lean_stamount_mul_round]
def lean_stamount_mul_round
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    (assetKind : UInt8) (roundUp : UInt8) (mode : UInt8) : FFISTAmountResult :=
  let v1 := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let v2 := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeSTAmountResult
    (STAmount.mulRound v1 v2 (decodeAsset assetKind) (roundUp != 0) (decodeMode mode))

@[export lean_stamount_mul_round_strict]
def lean_stamount_mul_round_strict
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    (assetKind : UInt8) (roundUp : UInt8) (mode : UInt8) : FFISTAmountResult :=
  let v1 := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let v2 := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeSTAmountResult
    (STAmount.mulRoundStrict v1 v2 (decodeAsset assetKind) (roundUp != 0) (decodeMode mode))

@[export lean_stamount_div_round]
def lean_stamount_div_round
    (kindN : UInt8) (mValueN : UInt64) (mOffsetN : Int64) (mIsNegativeN : UInt8)
    (kindD : UInt8) (mValueD : UInt64) (mOffsetD : Int64) (mIsNegativeD : UInt8)
    (assetKind : UInt8) (roundUp : UInt8) (mode : UInt8) : FFISTAmountResult :=
  let num := decodeSTAmount kindN mValueN mOffsetN mIsNegativeN
  let den := decodeSTAmount kindD mValueD mOffsetD mIsNegativeD
  encodeSTAmountResult
    (STAmount.divRound num den (decodeAsset assetKind) (roundUp != 0) (decodeMode mode))

@[export lean_stamount_div_round_strict]
def lean_stamount_div_round_strict
    (kindN : UInt8) (mValueN : UInt64) (mOffsetN : Int64) (mIsNegativeN : UInt8)
    (kindD : UInt8) (mValueD : UInt64) (mOffsetD : Int64) (mIsNegativeD : UInt8)
    (assetKind : UInt8) (roundUp : UInt8) (mode : UInt8) : FFISTAmountResult :=
  let num := decodeSTAmount kindN mValueN mOffsetN mIsNegativeN
  let den := decodeSTAmount kindD mValueD mOffsetD mIsNegativeD
  encodeSTAmountResult
    (STAmount.divRoundStrict num den (decodeAsset assetKind) (roundUp != 0) (decodeMode mode))

@[export lean_stamount_can_add]
def lean_stamount_can_add
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    (mode : UInt8) : FFIBoolResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeBoolResult (STAmount.canAdd a b (decodeMode mode))

@[export lean_stamount_can_subtract]
def lean_stamount_can_subtract
    (kind1 : UInt8) (mValue1 : UInt64) (mOffset1 : Int64) (mIsNegative1 : UInt8)
    (kind2 : UInt8) (mValue2 : UInt64) (mOffset2 : Int64) (mIsNegative2 : UInt8)
    : FFIBoolResult :=
  let a := decodeSTAmount kind1 mValue1 mOffset1 mIsNegative1
  let b := decodeSTAmount kind2 mValue2 mOffset2 mIsNegative2
  encodeBoolResult (STAmount.canSubtract a b)

@[export lean_stamount_round_to_scale]
def lean_stamount_round_to_scale
    (kind : UInt8) (mValue : UInt64) (mOffset : Int64) (mIsNegative : UInt8)
    (scale : Int64) (mode : UInt8) : FFISTAmountResult :=
  let s := decodeSTAmount kind mValue mOffset mIsNegative
  encodeSTAmountResult (STAmount.roundToScale s scale.toInt (decodeMode mode))

@[export lean_stamount_get_rate]
def lean_stamount_get_rate
    (kindOut : UInt8) (mValueOut : UInt64) (mOffsetOut : Int64) (mIsNegativeOut : UInt8)
    (kindIn : UInt8) (mValueIn : UInt64) (mOffsetIn : Int64) (mIsNegativeIn : UInt8)
    (mode : UInt8) : UInt64 :=
  let offerOut := decodeSTAmount kindOut mValueOut mOffsetOut mIsNegativeOut
  let offerIn := decodeSTAmount kindIn mValueIn mOffsetIn mIsNegativeIn
  STAmount.getRate offerOut offerIn (decodeMode mode)

end XRPL.FFI

import XRPL.Model.Protocol.IOUAmount
import XRPL.Model.Protocol.MPTAmount
import XRPL.Model.Protocol.STAmount
import XRPL.Model.Protocol.XRPAmount

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol

structure FFINumberResult where
  mantissa : UInt64
  exponent : Int64
  status : UInt8
  negative : UInt8

structure FFIBoolResult where
  value : UInt8
  status : UInt8

structure FFIMPTResult where
  value : Int64
  status : UInt8

structure FFIXRPResult where
  drops : Int64
  status : UInt8

structure FFIIOUResult where
  mantissa : Int64
  exponent : Int64
  status : UInt8

-- assetKind: 0 = XRP, 1 = IOU (noIssue), 2 = MPT (ffiMPTIssue).
structure FFISTAmountResult where
  assetKind : UInt8
  mValue : UInt64
  mOffset : Int64
  mIsNegative : UInt8
  status : UInt8

def ffiMPTIssue : MPTIssue := { mptID := ⟨0⟩ }

def decodeMode (m : UInt8) : rounding_mode := match m.toNat with
  | 0 => .to_nearest | 1 => .towards_zero | 2 => .downward | _ => .upward

def decodeNumber (neg : UInt8) (mant : UInt64) (exp : Int64) : Number :=
  Number.unchecked (neg != 0) mant exp.toInt

def decodeMPT (v : Int64) : MPTAmount := { value_ := v }

def decodeXRP (v : Int64) : XRPAmount := { drops_ := v }

def decodeIOU (m : Int64) (e : Int64) : IOUAmount :=
  { mantissa_ := m, exponent_ := e.toInt }

def decodeAsset (kind : UInt8) : Asset := match kind.toNat with
  | 0 => xrpAsset
  | 1 => .issue noIssue
  | _ => .mptIssue ffiMPTIssue

def decodeSTAmount (kind : UInt8) (mValue : UInt64) (mOffset : Int64)
    (mIsNegative : UInt8) : STAmount :=
  STAmount.unchecked (decodeAsset kind) mValue mOffset.toInt (mIsNegative != 0)

-- `Number.mantissa`/`Number.exponent` apply the C++ transformation
def encodeNumber (n : Number) : FFINumberResult :=
  ⟨n.mantissa.toInt.natAbs.toUInt64, n.exponent.toInt64, 0, if n.negative_ then 1 else 0⟩

def encodeResult (r : Except String Number) : FFINumberResult :=
  match r with
  | .ok n => encodeNumber n
  | .error _ => ⟨0, 0, 1, 0⟩

def encodeMPTResult (r : Except String MPTAmount) : FFIMPTResult :=
  match r with
  | .ok x => ⟨x.value, 0⟩
  | .error _ => ⟨0, 1⟩

def encodeXRPResult (r : Except String XRPAmount) : FFIXRPResult :=
  match r with
  | .ok x => ⟨x.value, 0⟩
  | .error _ => ⟨0, 1⟩

def encodeIOUResult (r : Except String IOUAmount) : FFIIOUResult :=
  match r with
  | .ok x => ⟨x.mantissa, x.exponent.toInt64, 0⟩
  | .error _ => ⟨0, 0, 1⟩

def encodeAsset : Asset → UInt8
  | .issue iss => if iss.isXRP then 0 else 1
  | .mptIssue _ => 2

def encodeSTAmount (s : STAmount) : FFISTAmountResult :=
  let isNegative : UInt8 := if s.negative then 1 else 0
  ⟨encodeAsset s.asset, s.mantissa, s.exponent.toInt64, isNegative, 0⟩

def encodeSTAmountResult (r : Except String STAmount) : FFISTAmountResult :=
  match r with
  | .ok s => encodeSTAmount s
  | .error _ => ⟨0, 0, 0, 0, 1⟩

def encodeBoolResult (r : Except String Bool) : FFIBoolResult :=
  match r with
  | .ok b => ⟨if b then 1 else 0, 0⟩
  | .error _ => ⟨0, 1⟩

end XRPL.FFI

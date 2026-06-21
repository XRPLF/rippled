import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.STAmount


namespace XRPL.Model.Protocol

structure RippleState where
  key : UInt256 := 0
  flags : UInt32 := 0
  balance : STAmount := STAmount.ofNativeInt64 0
  lowLimit : STAmount := STAmount.ofNativeInt64 0
  highLimit : STAmount := STAmount.ofNativeInt64 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  lowNode : Option UInt64 := none
  lowQualityIn : Option UInt32 := none
  lowQualityOut : Option UInt32 := none
  highNode : Option UInt64 := none
  highQualityIn : Option UInt32 := none
  highQualityOut : Option UInt32 := none
  deriving Repr

def RippleState.empty : RippleState := {}

inductive RippleStateAmountField where | sfBalance | sfLowLimit | sfHighLimit
  deriving DecidableEq, Repr

def RippleState.getFieldAmount (sle : RippleState) : RippleStateAmountField → STAmount
  | .sfBalance => sle.balance
  | .sfLowLimit => sle.lowLimit
  | .sfHighLimit => sle.highLimit

def lsfLowReserve : UInt32 := 0x00010000
def lsfHighReserve : UInt32 := 0x00020000
def lsfLowAuth : UInt32 := 0x00040000
def lsfHighAuth : UInt32 := 0x00080000
def lsfLowNoRipple : UInt32 := 0x00100000
def lsfHighNoRipple : UInt32 := 0x00200000
def lsfLowFreeze : UInt32 := 0x00400000
def lsfHighFreeze : UInt32 := 0x00800000
def lsfLowDeepFreeze : UInt32 := 0x02000000
def lsfHighDeepFreeze : UInt32 := 0x04000000

def RippleState.isFlag (sle : RippleState) (f : UInt32) : Bool := (sle.flags &&& f) == f

def RippleState.clearFlag (sle : RippleState) (f : UInt32) : RippleState :=
  { sle with flags := sle.flags &&& ~~~f }

end XRPL.Model.Protocol

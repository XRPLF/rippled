import XRPL.Model.Protocol.XRPAmount


namespace XRPL.Model.Protocol


structure Fees where
  base : XRPAmount
  reserve : XRPAmount
  increment : XRPAmount
  deriving Repr

def Fees.empty : Fees :=
  { base := XRPAmount.zero, reserve := XRPAmount.zero, increment := XRPAmount.zero }

def Fees.accountReserve (fees : Fees) (ownerCount : UInt32) : XRPAmount :=
  fees.reserve.operator_add (fees.increment.operator_mul (Int64.ofNat ownerCount.toNat))

end XRPL.Model.Protocol

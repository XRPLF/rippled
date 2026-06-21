import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.Protocol


namespace XRPL.Model.Protocol

structure LedgerHeader where
  seq : LedgerIndex
  parentCloseTime : NetClock.TimePoint
  parentHash : UInt256
  deriving Repr

def LedgerHeader.empty : LedgerHeader :=
  { seq := 0, parentCloseTime := 0, parentHash := 0 }

end XRPL.Model.Protocol

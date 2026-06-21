import XRPL.Model.Protocol.LedgerHeader


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_ledger_header_build]
def lean_ledger_header_build (seq parentCloseTime : UInt32) (parentHash : UInt256) : LedgerHeader :=
  { seq, parentCloseTime, parentHash }
@[export lean_ledger_header_seq]
def lean_ledger_header_seq (h : LedgerHeader) : UInt32 := h.seq
@[export lean_ledger_header_parent_close_time]
def lean_ledger_header_parent_close_time (h : LedgerHeader) : UInt32 := h.parentCloseTime
@[export lean_ledger_header_parent_hash]
def lean_ledger_header_parent_hash (h : LedgerHeader) : UInt256 := h.parentHash

end XRPL.FFI

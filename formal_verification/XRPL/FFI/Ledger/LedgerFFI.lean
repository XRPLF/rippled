import XRPL.Model.Ledger.Ledger


namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.Ledger

def emptyLedger : Ledger :=
  { entries := ∅, fees := Fees.empty, header := LedgerHeader.empty }

@[export lean_ledger_create_empty]
def lean_ledger_create_empty (_ : Unit) : Ledger := emptyLedger

@[export lean_ledger_entry_code]
def lean_ledger_entry_code (entry : LedgerEntry) : UInt16 := entry.code

@[export lean_ledger_entry_type_of_code]
def lean_ledger_entry_type_of_code (code : UInt16) : LedgerEntryType := LedgerEntryType.ofCode code

@[export lean_ledger_keys]
def lean_ledger_keys (ledger : Ledger) : List UInt256 :=
  ledger.keys

@[export lean_ledger_read]
def lean_ledger_read (ledger : Ledger) (key : UInt256) : Option LedgerEntry :=
  ledger.readUnchecked key

@[export lean_ledger_add]
def lean_ledger_add (ledger : Ledger) (entry : LedgerEntry) : Ledger := ledger.put entry

@[export lean_ledger_header_set]
def lean_ledger_header_set (ledger : Ledger) (header : LedgerHeader) : Ledger :=
  ledger.setHeader header
@[export lean_ledger_header_fetch]
def lean_ledger_header_fetch (ledger : Ledger) : LedgerHeader :=
  ledger.header

@[export lean_ledger_fees_set]
def lean_ledger_fees_set (ledger : Ledger) (fees : Fees) : Ledger :=
  ledger.setFees fees
@[export lean_ledger_fees_fetch]
def lean_ledger_fees_fetch (ledger : Ledger) : Fees :=
  ledger.fees

end XRPL.FFI

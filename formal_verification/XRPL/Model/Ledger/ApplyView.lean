import XRPL.Model.Ledger.ReadView


namespace XRPL.Model.Ledger

open XRPL.Model.Protocol

abbrev ApplyView := StateT Ledger (Except String)

def ApplyView.peek (k : Keylet) : ApplyView (Option LedgerEntry) := return (← get).read k

def ApplyView.insert (e : LedgerEntry) : ApplyView Unit := do
  let sb ← get
  if (sb.read ⟨e.type, e.key⟩).isSome then
    throw "ApplyView.insert: entry already exists"
  modify (·.put e)
def ApplyView.update (e : LedgerEntry) : ApplyView Unit := do
  let sb ← get
  if (sb.read ⟨e.type, e.key⟩).isNone then
    throw "ApplyView.update: entry not present"
  modify (·.put e)
def ApplyView.erase (k : Keylet) : ApplyView Unit := do
  let sb ← get
  if (sb.read k).isNone then
    throw "ApplyView.erase: entry not present"
  modify (·.remove k.key)

-- Run a read-only computation in the mutating view (models C++ `ApplyView : ReadView`).
def ApplyView.ofReadView {α : Type} (x : ReadView α) : ApplyView α := do
  let sb ← get
  match x.run sb with
  | .ok a => return a
  | .error e => throw e

def ApplyView.openStub : ApplyView Bool := pure false
def ApplyView.dirLinkStub {α : Type} (_owner : AccountID) (_object : α) : ApplyView TER := pure .tesSUCCESS
def ApplyView.dirRemoveStub (_owner : AccountID) (_page : UInt64) (_key : UInt256) (_keepRoot : Bool) : ApplyView Bool := pure true
def ApplyView.dirInsertStub (_owner : AccountID) (_key : UInt256) : ApplyView (Option UInt64) := pure (some 0)
def adjustOwnerCountHookStub (_id : AccountID) (_cur _next : UInt32) : ApplyView Unit := pure ()
def creditHookIOUStub (_from _to : AccountID) (_amount _preCreditBalance : STAmount) : ApplyView Unit := pure ()
def creditHookMPTStub (_from _to : AccountID) (_amount : STAmount)
    (_preCreditBalanceHolder : UInt64) (_preCreditBalanceIssuer : Int64) : ApplyView Unit := pure ()
def ownerCountHookStub (_id : AccountID) (count : UInt32) : UInt32 := count
def balanceHookIOUStub (_account _issuer : AccountID) (amount : STAmount) : STAmount := amount
def balanceHookMPTStub (_account : AccountID) (issue : MPTIssue) (amount : Int64)
    : Except String STAmount :=
  -- integral at offset 0: canonicalize exact, mode inert
  STAmount.ofInt64 (.mptIssue issue) amount 0 .to_nearest

def applyTx (initial : Ledger) (computation : ApplyView TER) : TER × Ledger :=
  match computation.run initial with
  | .ok (.tesSUCCESS, final) => (.tesSUCCESS, final)
  | .ok (ter, _)             => (ter, initial)
  | .error _                 => (.tecINTERNAL, initial)

end XRPL.Model.Ledger

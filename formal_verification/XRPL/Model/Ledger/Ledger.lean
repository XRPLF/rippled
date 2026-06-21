import Std.Data.HashMap

import XRPL.Model.Protocol.Fees
import XRPL.Model.Protocol.Indexes
import XRPL.Model.Protocol.LedgerEntries.AccountRoot
import XRPL.Model.Protocol.LedgerEntries.Credential
import XRPL.Model.Protocol.LedgerEntries.DepositPreauth
import XRPL.Model.Protocol.LedgerEntries.Loan
import XRPL.Model.Protocol.LedgerEntries.LoanBroker
import XRPL.Model.Protocol.LedgerEntries.MPToken
import XRPL.Model.Protocol.LedgerEntries.MPTokenIssuance
import XRPL.Model.Protocol.LedgerEntries.PermissionedDomain
import XRPL.Model.Protocol.LedgerEntries.RippleState
import XRPL.Model.Protocol.LedgerEntries.Vault
import XRPL.Model.Protocol.LedgerHeader
import XRPL.Model.Protocol.Number
import XRPL.Model.Protocol.STAmount
import XRPL.Model.Protocol.TER
import XRPL.Model.Protocol.XRPAmount

set_option linter.dupNamespace false

namespace XRPL.Model.Ledger

open XRPL.Model.Protocol
open Std (HashMap)

-- One SHAMap entry, tagged by type (mirrors rippled's single uint256 → SLE map).
inductive LedgerEntry where
  | accountRoot : AccountRoot → LedgerEntry
  | credential : Credential → LedgerEntry
  | depositPreauth : DepositPreauth → LedgerEntry
  | loan : Loan → LedgerEntry
  | loanBroker : LoanBroker → LedgerEntry
  | mptoken : MPToken → LedgerEntry
  | mptokenIssuance : MPTokenIssuance → LedgerEntry
  | permissionedDomain : PermissionedDomain → LedgerEntry
  | rippleState : RippleState → LedgerEntry
  | vault : Vault → LedgerEntry

def LedgerEntry.key : LedgerEntry → UInt256
  | .accountRoot a => a.key
  | .credential c => c.key
  | .depositPreauth d => d.key
  | .loan l => l.key
  | .loanBroker b => b.key
  | .mptoken m => m.key
  | .mptokenIssuance i => i.key
  | .permissionedDomain p => p.key
  | .rippleState r => r.key
  | .vault v => v.key

def LedgerEntry.type : LedgerEntry → LedgerEntryType
  | .accountRoot _ => .accountRoot
  | .credential _ => .credential
  | .depositPreauth _ => .depositPreauth
  | .loan _ => .loan
  | .loanBroker _ => .loanBroker
  | .mptoken _ => .mptoken
  | .mptokenIssuance _ => .mptokenIssuance
  | .permissionedDomain _ => .permissionedDomain
  | .rippleState _ => .rippleState
  | .vault _ => .vault

def LedgerEntry.code (e : LedgerEntry) : UInt16 := e.type.code

def LedgerEntry.asAccountRoot : LedgerEntry → Option AccountRoot
  | .accountRoot a => some a | _ => none
def LedgerEntry.asCredential : LedgerEntry → Option Credential
  | .credential c => some c | _ => none
def LedgerEntry.asDepositPreauth : LedgerEntry → Option DepositPreauth
  | .depositPreauth d => some d | _ => none
def LedgerEntry.asLoan : LedgerEntry → Option Loan
  | .loan l => some l | _ => none
def LedgerEntry.asLoanBroker : LedgerEntry → Option LoanBroker
  | .loanBroker b => some b | _ => none
def LedgerEntry.asMPToken : LedgerEntry → Option MPToken
  | .mptoken m => some m | _ => none
def LedgerEntry.asMPTokenIssuance : LedgerEntry → Option MPTokenIssuance
  | .mptokenIssuance i => some i | _ => none
def LedgerEntry.asPermissionedDomain : LedgerEntry → Option PermissionedDomain
  | .permissionedDomain p => some p | _ => none
def LedgerEntry.asRippleState : LedgerEntry → Option RippleState
  | .rippleState r => some r | _ => none
def LedgerEntry.asVault : LedgerEntry → Option Vault
  | .vault v => some v | _ => none

structure Ledger where
  entries : HashMap UInt256 LedgerEntry
  fees : Fees
  header : LedgerHeader

def Ledger.parentCloseTime (sb : Ledger) : NetClock.TimePoint :=
  sb.header.parentCloseTime

def Ledger.put (sb : Ledger) (e : LedgerEntry) : Ledger :=
  { sb with entries := sb.entries.insert e.key e }
def Ledger.remove (sb : Ledger) (key : UInt256) : Ledger :=
  { sb with entries := sb.entries.erase key }
def Ledger.setHeader (sb : Ledger) (header : LedgerHeader) : Ledger := { sb with header }
def Ledger.setFees (sb : Ledger) (fees : Fees) : Ledger := { sb with fees }

def Ledger.read (sb : Ledger) (k : Keylet) : Option LedgerEntry :=
  match sb.entries[k.key]? with
  | some e => if e.type = k.type then some e else none
  | none => none

-- keylet::unchecked: read whatever entry sits at a key, ignoring its type.
def Ledger.readUnchecked (sb : Ledger) (key : UInt256) : Option LedgerEntry :=
  sb.entries[key]?

def Ledger.size (sb : Ledger) : Nat := sb.entries.size

def Ledger.keys (sb : Ledger) : List UInt256 := sb.entries.keys

end XRPL.Model.Ledger

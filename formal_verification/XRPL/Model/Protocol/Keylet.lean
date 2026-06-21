import XRPL.Model.Basics.BaseUInt

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

inductive LedgerEntryType where
  | accountRoot | credential | depositPreauth | loan | loanBroker
  | mptoken | mptokenIssuance | permissionedDomain | rippleState | vault
  deriving DecidableEq, Repr, BEq, Inhabited

-- rippled's on-the-wire LedgerEntryType code (the shared FFI identifier).
def LedgerEntryType.code : LedgerEntryType → UInt16
  | .accountRoot => 0x0061 | .credential => 0x0081 | .depositPreauth => 0x0070
  | .loan => 0x0089 | .loanBroker => 0x0088 | .mptoken => 0x007f
  | .mptokenIssuance => 0x007e | .permissionedDomain => 0x0082
  | .rippleState => 0x0072 | .vault => 0x0084

def LedgerEntryType.ofCode : UInt16 → LedgerEntryType
  | 0x0061 => .accountRoot | 0x0081 => .credential | 0x0070 => .depositPreauth
  | 0x0089 => .loan | 0x0088 => .loanBroker | 0x007f => .mptoken
  | 0x007e => .mptokenIssuance | 0x0082 => .permissionedDomain
  | 0x0072 => .rippleState | 0x0084 => .vault
  -- warns + returns .accountRoot; set LEAN_ABORT_ON_PANIC=1 to abort instead.
  | code => panic! s!"LedgerEntryType.ofCode: unknown ledger entry code {code}"

structure Keylet where
  type : LedgerEntryType
  key : UInt256
  deriving DecidableEq, Repr

end XRPL.Model.Protocol

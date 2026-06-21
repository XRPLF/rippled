import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.UintTypes


namespace XRPL.Model.Protocol

structure MPTIssue where
  mptID : MPTID
  deriving DecidableEq, Repr, BEq, Hashable

def MPTIssue.native (_ : MPTIssue) : Bool := false
def MPTIssue.integral (_ : MPTIssue) : Bool := true

def MPTIssue.getMptID (m : MPTIssue) : MPTID := m.mptID
-- Trailing 160 bits of the 192-bit MPTID are the issuer.
def MPTIssue.getIssuer (m : MPTIssue) : AccountID := ⟨m.mptID.val.truncate 160⟩

end XRPL.Model.Protocol

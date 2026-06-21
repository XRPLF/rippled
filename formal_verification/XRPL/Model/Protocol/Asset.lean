import XRPL.Model.Protocol.Issue
import XRPL.Model.Protocol.MPTIssue


namespace XRPL.Model.Protocol

-- C++ `Asset` is `variant<Issue, MPTIssue>`; `operator==` is `Asset.equiv`.
inductive Asset where
  | issue : Issue → Asset
  | mptIssue : MPTIssue → Asset
  deriving DecidableEq, Repr, Hashable

def xrpAsset : Asset := .issue xrpIssue

def Asset.isNative : Asset → Bool
  | .issue i => i.native
  | .mptIssue m => m.native

def Asset.integral : Asset → Bool
  | .issue i => i.integral
  | .mptIssue m => m.integral

def Asset.equiv : Asset → Asset → Bool
  | .issue a, .issue b => a.equiv b
  | .mptIssue a, .mptIssue b => a == b
  | _, _ => false

def Asset.equalTokens : Asset → Asset → Bool
  | .issue a, .issue b => a.currency == b.currency
  | .mptIssue a, .mptIssue b => a.mptID == b.mptID
  | _, _ => false

def Asset.holdsIssue : Asset → Bool
  | .issue _ => true
  | _ => false

def Asset.holdsMPTIssue : Asset → Bool
  | .mptIssue _ => true
  | _ => false

def Asset.getIssue : Asset → Except String Issue
  | .issue i => .ok i
  | .mptIssue _ => .error "Asset is not a requested issue"

def Asset.getMPTIssue : Asset → Except String MPTIssue
  | .mptIssue m => .ok m
  | .issue _ => .error "Asset is not a requested issue"

def Asset.getIssuer : Asset → AccountID
  | .issue i => i.account
  | .mptIssue m => m.getIssuer

def Asset.areComparable : Asset → Asset → Bool
  | .issue i1, .issue i2 =>
    (i1.native == i2.native) && (i1.currency == i2.currency)
  | .mptIssue m1, .mptIssue m2 => m1 == m2
  | _, _ => false

def Asset.eq : Asset → Asset → Bool
  | .issue i1, .issue i2 => i1 == i2
  | .mptIssue m1, .mptIssue m2 => m1 == m2
  | _, _ => false

def Asset.ne : Asset → Asset → Bool
  | a1, a2 => !(a1.eq a2)

end XRPL.Model.Protocol

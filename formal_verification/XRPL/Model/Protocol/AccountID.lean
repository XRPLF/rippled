import Mathlib.Tactic
import XRPL.Model.Basics.BaseUInt

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

structure AccountID where
  val : BitVec 160
  deriving DecidableEq, Repr, BEq, Hashable

def xrpAccount : AccountID := ⟨0⟩
def noAccount : AccountID := ⟨1⟩

def AccountID.isXRP (a : AccountID) : Bool := a == xrpAccount

def AccountID.lt (a b : AccountID) : Bool := decide (a.val.toNat < b.val.toNat)
def AccountID.le (a b : AccountID) : Bool := decide (a.val.toNat ≤ b.val.toNat)
def AccountID.min (a b : AccountID) : AccountID := if a.le b then a else b
def AccountID.max (a b : AccountID) : AccountID := if a.le b then b else a

def AccountID.fromRaw (bytes : ByteArray) : AccountID := ⟨bytesToBitVec 160 bytes⟩

end XRPL.Model.Protocol

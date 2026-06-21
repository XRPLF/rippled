import Mathlib.Tactic


namespace XRPL.Model.Protocol

structure Currency where
  val : BitVec 160
  deriving DecidableEq, Repr, BEq, Hashable

-- 192-bit MPT issuance id: 32-bit sequence + 160-bit issuer account.
structure MPTID where
  val : BitVec 192
  deriving DecidableEq, Repr, BEq, Hashable

def xrpCurrency : Currency := ⟨0⟩
def noCurrency : Currency := ⟨1⟩
def badCurrency : Currency := ⟨0x5852500000000000⟩

def Currency.isXRP (c : Currency) : Bool := c == xrpCurrency

end XRPL.Model.Protocol

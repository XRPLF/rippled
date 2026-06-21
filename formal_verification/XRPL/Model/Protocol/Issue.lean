import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.UintTypes


namespace XRPL.Model.Protocol

-- Structural eq is field-by-field; C++ `operator==` is `Issue.equiv` below.
structure Issue where
  currency : Currency
  account : AccountID
  deriving DecidableEq, Repr, Hashable

def xrpIssue : Issue := { currency := xrpCurrency, account := xrpAccount }
def noIssue : Issue := { currency := noCurrency, account := noAccount }

-- Same currency AND (XRP-currency OR same account); account ignored for XRP.
def Issue.equiv (a b : Issue) : Bool :=
  a.currency == b.currency && (a.currency.isXRP || a.account == b.account)

def Issue.native (i : Issue) : Bool := i.equiv xrpIssue
def Issue.integral (i : Issue) : Bool := i.native
def Issue.isXRP (i : Issue) : Bool := i.native

def Issue.isConsistent (i : Issue) : Bool := i.currency.isXRP == i.account.isXRP
def Issue.getIssuer (i : Issue) : AccountID := i.account

end XRPL.Model.Protocol

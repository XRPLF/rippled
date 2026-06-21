import XRPL.Model.Protocol.Issue


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_issue_build]
def lean_issue_build (currency : Currency) (account : AccountID) : Issue := ⟨currency, account⟩
@[export lean_issue_currency]
def lean_issue_currency (i : Issue) : Currency := i.currency
@[export lean_issue_account]
def lean_issue_account (i : Issue) : AccountID := i.account

end XRPL.FFI

import XRPL.Model.Protocol.Indexes

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_keylet_account]
def lean_keylet_account (id : AccountID) : UInt256 := (Keylet.account id).key

@[export lean_keylet_credential]
def lean_keylet_credential (subject issuer : AccountID) (credType : ByteArray) : UInt256 :=
  (Keylet.credential subject issuer credType.toList).key

@[export lean_keylet_deposit_preauth_account]
def lean_keylet_deposit_preauth_account (owner authorized : AccountID) : UInt256 :=
  (Keylet.depositPreauthAccount owner authorized).key

@[export lean_keylet_deposit_preauth_creds]
def lean_keylet_deposit_preauth_creds (owner : AccountID) (authCreds : List AcceptedCredential) : UInt256 :=
  (Keylet.depositPreauthCreds owner authCreds).key

@[export lean_keylet_line]
def lean_keylet_line (id0 id1 : AccountID) (currency : Currency) : UInt256 :=
  (Keylet.line id0 id1 currency).key

@[export lean_keylet_loan]
def lean_keylet_loan (loanBrokerID : UInt256) (loanSeq : UInt32) : UInt256 :=
  (Keylet.loan loanBrokerID loanSeq).key

@[export lean_keylet_loan_broker]
def lean_keylet_loan_broker (owner : AccountID) (seq : UInt32) : UInt256 :=
  (Keylet.loanBroker owner seq).key

@[export lean_keylet_mpt_issuance]
def lean_keylet_mpt_issuance (mptID : MPTID) : UInt256 := (Keylet.mptIssuance mptID).key

@[export lean_keylet_mptoken]
def lean_keylet_mptoken (mptID : MPTID) (holder : AccountID) : UInt256 :=
  (Keylet.mptoken mptID holder).key

@[export lean_keylet_permissioned_domain]
def lean_keylet_permissioned_domain (owner : AccountID) (seq : UInt32) : UInt256 :=
  (Keylet.permissionedDomainFromSeq owner seq).key

@[export lean_keylet_vault]
def lean_keylet_vault (owner : AccountID) (seq : UInt32) : UInt256 := (Keylet.vault owner seq).key

end XRPL.FFI
